[CmdletBinding()]
param(
    [string]$ServerRoot = "",
    [string]$PublicIp = "31.177.83.146",
    [int]$SignalingPort = 9002,
    [string]$Preset = "x64-release",
    [string]$Target = "eds_server_new_mediasoup_app",
    [string]$SupabaseUrl = "",
    [string]$SupabaseAnonKey = "",
    [string]$PostgresConninfo = "",
    [string]$PostgresPoolSize = "",
    [string]$SignalingTlsCertFile = "",
    [string]$SignalingTlsKeyFile = "",
    [string]$SignalingTlsCaFile = "",
    [switch]$DisableAutoTlsCertificateGeneration
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($ServerRoot)) {
    $ServerRoot = $PSScriptRoot
}

$resolvedRoot = (Resolve-Path $ServerRoot).Path
$buildScript = Join-Path $resolvedRoot "build.ps1"
$backendCmd = Join-Path $resolvedRoot "apps\mediasoup-server\run-backend.cmd"

if (!(Test-Path $buildScript)) {
    throw "build.ps1 not found: $buildScript"
}
if (!(Test-Path $backendCmd)) {
    throw "Mediasoup backend runner not found: $backendCmd"
}

function Resolve-ConfigValue {
    param(
        [string]$ParamValue = "",
        [string[]]$EnvNames = @(),
        [string]$Fallback = ""
    )

    if (-not [string]::IsNullOrWhiteSpace($ParamValue)) {
        return $ParamValue
    }

    foreach ($envName in $EnvNames) {
        if ([string]::IsNullOrWhiteSpace($envName)) {
            continue
        }
        $envValue = [Environment]::GetEnvironmentVariable($envName, "Process")
        if ([string]::IsNullOrWhiteSpace($envValue)) {
            $envValue = [Environment]::GetEnvironmentVariable($envName, "Machine")
        }
        if ([string]::IsNullOrWhiteSpace($envValue)) {
            $envValue = [Environment]::GetEnvironmentVariable($envName, "User")
        }
        if (-not [string]::IsNullOrWhiteSpace($envValue)) {
            return $envValue
        }
    }

    return $Fallback
}

function Convert-BytesToPem {
    param(
        [Parameter(Mandatory = $true)]
        [byte[]]$Bytes,
        [Parameter(Mandatory = $true)]
        [string]$Label
    )

    $base64 = [Convert]::ToBase64String(
        $Bytes,
        [System.Base64FormattingOptions]::InsertLineBreaks)
    return "-----BEGIN $Label-----`n$base64`n-----END $Label-----`n"
}

function Decode-PemBody {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PemText
    )

    $normalized = $PemText `
        -replace '-----BEGIN [^-]+-----', '' `
        -replace '-----END [^-]+-----', '' `
        -replace '\s', ''
    if ([string]::IsNullOrWhiteSpace($normalized)) {
        return $null
    }
    try {
        return [Convert]::FromBase64String($normalized)
    }
    catch {
        return $null
    }
}

function Ensure-SignalingTlsCertificateFiles {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Root,
        [Parameter(Mandatory = $true)]
        [string]$HostNameOrIp,
        [string]$CertFile = "",
        [string]$KeyFile = "",
        [switch]$DisableAutoGeneration
    )

    $resolvedCertFile = $CertFile
    $resolvedKeyFile = $KeyFile

    if (-not [string]::IsNullOrWhiteSpace($resolvedCertFile)) {
        if (-not [System.IO.Path]::IsPathRooted($resolvedCertFile)) {
            $resolvedCertFile = Join-Path $Root $resolvedCertFile
        }
        $resolvedCertFile = (Resolve-Path -LiteralPath $resolvedCertFile -ErrorAction Stop).Path
    }

    if (-not [string]::IsNullOrWhiteSpace($resolvedKeyFile)) {
        if (-not [System.IO.Path]::IsPathRooted($resolvedKeyFile)) {
            $resolvedKeyFile = Join-Path $Root $resolvedKeyFile
        }
        $resolvedKeyFile = (Resolve-Path -LiteralPath $resolvedKeyFile -ErrorAction Stop).Path
    }

    if (([string]::IsNullOrWhiteSpace($resolvedCertFile) -and -not [string]::IsNullOrWhiteSpace($resolvedKeyFile)) -or
        (-not [string]::IsNullOrWhiteSpace($resolvedCertFile) -and [string]::IsNullOrWhiteSpace($resolvedKeyFile))) {
        throw "Both signaling TLS cert and key must be provided together."
    }

    if (-not [string]::IsNullOrWhiteSpace($resolvedCertFile) -and -not [string]::IsNullOrWhiteSpace($resolvedKeyFile)) {
        return @{
            CertFile = $resolvedCertFile
            KeyFile = $resolvedKeyFile
            Generated = $false
        }
    }

    if ($DisableAutoGeneration) {
        throw "TLS certificate generation is disabled and no cert/key were provided."
    }

    $safeHostSegment = if ([string]::IsNullOrWhiteSpace($HostNameOrIp)) {
        "public-endpoint"
    }
    else {
        ($HostNameOrIp -replace '[^a-zA-Z0-9\.-]', '_')
    }
    $certDir = Join-Path $Root "certs"
    if (!(Test-Path $certDir)) {
        New-Item -ItemType Directory -Path $certDir | Out-Null
    }

    $generatedCertFile = Join-Path $certDir ("signaling-{0}.pem" -f $safeHostSegment)
    $generatedKeyFile = Join-Path $certDir ("signaling-{0}.key" -f $safeHostSegment)
    if ((Test-Path $generatedCertFile) -and (Test-Path $generatedKeyFile)) {
        $existingKeyPem = Get-Content -Path $generatedKeyFile -Raw -ErrorAction SilentlyContinue
        $existingKeyDer = $null
        if (-not [string]::IsNullOrWhiteSpace($existingKeyPem)) {
            $existingKeyDer = Decode-PemBody -PemText $existingKeyPem
        }
        if ($existingKeyDer -ne $null -and $existingKeyDer.Length -ge 1000) {
            return @{
                CertFile = (Resolve-Path $generatedCertFile).Path
                KeyFile = (Resolve-Path $generatedKeyFile).Path
                Generated = $false
            }
        }
        Write-Host "[run-all] Existing autogenerated TLS key is weak or invalid; regenerating."
    }


    if ([string]::IsNullOrWhiteSpace($HostNameOrIp)) {
        throw "PublicIp cannot be empty when generating signaling TLS certificate."
    }

    Write-Host "[run-all] Generating ECDSA P-384 self-signed TLS certificate for signaling endpoint: $HostNameOrIp"

    $ipAddress = $null
    $isIpAddress = [System.Net.IPAddress]::TryParse($HostNameOrIp, [ref]$ipAddress)

    $ecdsa = [System.Security.Cryptography.ECDsa]::Create(
        [System.Security.Cryptography.ECCurve]::NamedCurves.nistP384)

    try {
        $subject = "CN=$HostNameOrIp"
        $request = New-Object System.Security.Cryptography.X509Certificates.CertificateRequest(
            $subject,
            $ecdsa,
            [System.Security.Cryptography.HashAlgorithmName]::SHA384)

        $sanBuilder = New-Object System.Security.Cryptography.X509Certificates.SubjectAlternativeNameBuilder
        if ($isIpAddress) {
            $sanBuilder.AddIpAddress($ipAddress)
        }
        else {
            $sanBuilder.AddDnsName($HostNameOrIp)
        }
        $request.CertificateExtensions.Add($sanBuilder.Build())

        $request.CertificateExtensions.Add(
            (New-Object System.Security.Cryptography.X509Certificates.X509BasicConstraintsExtension($false, $false, 0, $true)))
        $keyUsageFlags = [System.Security.Cryptography.X509Certificates.X509KeyUsageFlags](([int][System.Security.Cryptography.X509Certificates.X509KeyUsageFlags]::DigitalSignature) -bor ([int][System.Security.Cryptography.X509Certificates.X509KeyUsageFlags]::KeyEncipherment))
        $request.CertificateExtensions.Add(
            (New-Object System.Security.Cryptography.X509Certificates.X509KeyUsageExtension($keyUsageFlags, $true)))
        $eku = New-Object System.Security.Cryptography.OidCollection
        [void]$eku.Add((New-Object System.Security.Cryptography.Oid("1.3.6.1.5.5.7.3.1")))
        $request.CertificateExtensions.Add(
            (New-Object System.Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension($eku, $true)))

        $certificate = $request.CreateSelfSigned(
            [DateTimeOffset]::UtcNow.AddMinutes(-5),
            [DateTimeOffset]::UtcNow.AddYears(3))
        try {
            $certificateDer = $certificate.Export([System.Security.Cryptography.X509Certificates.X509ContentType]::Cert)
            $privateKeyDer = $ecdsa.ExportPkcs8PrivateKey()
        }
        finally {
            $certificate.Dispose()
        }
    }
    finally {
        $ecdsa.Dispose()
    }

    $certificatePem = Convert-BytesToPem -Bytes $certificateDer -Label "CERTIFICATE"
    $privateKeyPem = Convert-BytesToPem -Bytes $privateKeyDer -Label "PRIVATE KEY"

    [System.IO.File]::WriteAllText($generatedCertFile, $certificatePem, [System.Text.Encoding]::ASCII)
    [System.IO.File]::WriteAllText($generatedKeyFile, $privateKeyPem, [System.Text.Encoding]::ASCII)

    return @{
        CertFile = (Resolve-Path $generatedCertFile).Path
        KeyFile = (Resolve-Path $generatedKeyFile).Path
        Generated = $true
    }
}

function Resolve-ServerExecutable {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Root,
        [Parameter(Mandatory = $true)]
        [string]$PresetName,
        [Parameter(Mandatory = $true)]
        [string]$TargetName
    )

    $candidates = @(
        (Join-Path $Root ("out\build\{0}-mediasoup\EDS_serverNew\{1}.exe" -f $PresetName, $TargetName)),
        (Join-Path $Root ("out\build\{0}-mediasoup\{1}.exe" -f $PresetName, $TargetName)),
        (Join-Path $Root "out\build\x64-release-mediasoup\EDS_serverNew\eds_server_new_mediasoup_app.exe"),
        (Join-Path $Root "out\build\x64-debug-mediasoup\EDS_serverNew\eds_server_new_mediasoup_app.exe"),
        (Join-Path $Root "out\x64-release\EDS_serverNew\eds_server_new_mediasoup_app.exe")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }

    return $null
}

function Stop-PreviousStackProcesses {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Root
    )

    $serverProcesses = Get-Process -Name "eds_server_new_mediasoup_app" -ErrorAction SilentlyContinue
    if ($serverProcesses) {
        $serverProcesses | Stop-Process -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 500
    }

    $serverJsMarker = [System.IO.Path]::Combine($Root, "apps", "mediasoup-server", "src", "server.js")
    $nodeProcesses = Get-CimInstance Win32_Process -Filter "Name='node.exe'" -ErrorAction SilentlyContinue |
        Where-Object { $_.CommandLine -and $_.CommandLine.IndexOf($serverJsMarker, [StringComparison]::OrdinalIgnoreCase) -ge 0 }

    foreach ($nodeProcess in $nodeProcesses) {
        Stop-Process -Id $nodeProcess.ProcessId -Force -ErrorAction SilentlyContinue
    }
}

function Ensure-FirewallRule {
    param(
        [Parameter(Mandatory = $true)]
        [string]$DisplayName,
        [Parameter(Mandatory = $true)]
        [string]$Protocol,
        [Parameter(Mandatory = $true)]
        [string]$LocalPort
    )

    $existing = Get-NetFirewallRule -DisplayName $DisplayName -ErrorAction SilentlyContinue
    if ($existing) {
        return
    }

    New-NetFirewallRule `
        -DisplayName $DisplayName `
        -Direction Inbound `
        -Action Allow `
        -Protocol $Protocol `
        -LocalPort $LocalPort | Out-Null
}

function Try-EnsureFirewallRules {
    try {
        $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
        $principal = New-Object Security.Principal.WindowsPrincipal($identity)
        $isAdmin = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
        if (-not $isAdmin) {
            Write-Host "[run-all] Firewall rules skipped (run as Administrator to auto-configure ports)."
            return
        }

        Ensure-FirewallRule -DisplayName "MeetSpace Signaling TCP 9002" -Protocol "TCP" -LocalPort "9002"
        Ensure-FirewallRule -DisplayName "MeetSpace WebRTC UDP 40000-49999" -Protocol "UDP" -LocalPort "40000-49999"
        Ensure-FirewallRule -DisplayName "MeetSpace WebRTC TCP 40000-49999" -Protocol "TCP" -LocalPort "40000-49999"
    } catch {
        Write-Host "[run-all] Firewall rule auto-configuration failed: $($_.Exception.Message)"
    }
}

$env:MEETSPACE_SIGNALING_PORT = "$SignalingPort"
$env:EDUSPACE_SIGNALING_PORT = "$SignalingPort"
$env:MEETSPACE_SIGNALING_TLS_ENABLED = "1"
$env:EDUSPACE_SIGNALING_TLS_ENABLED = "1"

$env:MEETSPACE_MEDIASOUP_BACKEND_URL = "ws://127.0.0.1:5001/ws"
$env:EDUSPACE_MEDIASOUP_BACKEND_URL = $env:MEETSPACE_MEDIASOUP_BACKEND_URL
$env:MEETSPACE_MEDIASOUP_BACKEND_CMD = $backendCmd
$env:EDUSPACE_MEDIASOUP_BACKEND_CMD = $env:MEETSPACE_MEDIASOUP_BACKEND_CMD

$env:MEDIASOUP_BACKEND_HOST = "127.0.0.1"
$env:MEDIASOUP_BACKEND_PORT = "5001"
$env:MEDIASOUP_BACKEND_PATH = "/ws"
$env:MEDIASOUP_RTC_LISTEN_IP = "0.0.0.0"
$env:MEDIASOUP_ANNOUNCED_IP = $PublicIp
$env:MEDIASOUP_RTC_MIN_PORT = "40000"
$env:MEDIASOUP_RTC_MAX_PORT = "49999"
$effectiveSupabaseUrl = Resolve-ConfigValue `
    -ParamValue $SupabaseUrl `
    -EnvNames @("MEETSPACE_SUPABASE_URL", "SUPABASE_URL") `
    -Fallback "https://mtbbcaykjomycovrxdya.supabase.co"
$effectiveSupabaseAnonKey = Resolve-ConfigValue `
    -ParamValue $SupabaseAnonKey `
    -EnvNames @("MEETSPACE_SUPABASE_ANON_KEY", "SUPABASE_ANON_KEY") `
    -Fallback "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6Im10YmJjYXlram9teWNvdnJ4ZHlhIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NzQ5MDkyODUsImV4cCI6MjA5MDQ4NTI4NX0.AKhEpGPBoiLDfUqAu1-MUgvDDrYlw_M0N_wHdXS9Cx4"
$effectivePostgresConninfo = Resolve-ConfigValue `
    -ParamValue $PostgresConninfo `
    -EnvNames @("MEETSPACE_POSTGRES_CONNINFO", "EDUSPACE_POSTGRES_CONNINFO", "POSTGRES_CONNINFO") `
    -Fallback "postgresql://postgres.mtbbcaykjomycovrxdya:Bg2WDduIXyEsd53R@aws-1-eu-west-2.pooler.supabase.com:5432/postgres"
$effectivePostgresPoolSize = Resolve-ConfigValue `
    -ParamValue $PostgresPoolSize `
    -EnvNames @("MEETSPACE_POSTGRES_POOL_SIZE", "EDUSPACE_POSTGRES_POOL_SIZE") `
    -Fallback "4"
$effectiveSignalingTlsCertFile = Resolve-ConfigValue `
    -ParamValue $SignalingTlsCertFile `
    -EnvNames @("MEETSPACE_SIGNALING_TLS_CERT_FILE", "EDUSPACE_SIGNALING_TLS_CERT_FILE") `
    -Fallback ""
$effectiveSignalingTlsKeyFile = Resolve-ConfigValue `
    -ParamValue $SignalingTlsKeyFile `
    -EnvNames @("MEETSPACE_SIGNALING_TLS_KEY_FILE", "EDUSPACE_SIGNALING_TLS_KEY_FILE") `
    -Fallback ""
$effectiveSignalingTlsCaFile = Resolve-ConfigValue `
    -ParamValue $SignalingTlsCaFile `
    -EnvNames @("MEETSPACE_SIGNALING_TLS_CA_FILE", "EDUSPACE_SIGNALING_TLS_CA_FILE") `
    -Fallback ""

$tlsMaterial = Ensure-SignalingTlsCertificateFiles `
    -Root $resolvedRoot `
    -HostNameOrIp $PublicIp `
    -CertFile $effectiveSignalingTlsCertFile `
    -KeyFile $effectiveSignalingTlsKeyFile `
    -DisableAutoGeneration:$DisableAutoTlsCertificateGeneration
$effectiveSignalingTlsCertFile = $tlsMaterial.CertFile
$effectiveSignalingTlsKeyFile = $tlsMaterial.KeyFile

if (-not [string]::IsNullOrWhiteSpace($effectiveSignalingTlsCaFile)) {
    if (-not [System.IO.Path]::IsPathRooted($effectiveSignalingTlsCaFile)) {
        $effectiveSignalingTlsCaFile = Join-Path $resolvedRoot $effectiveSignalingTlsCaFile
    }
    $effectiveSignalingTlsCaFile = (Resolve-Path -LiteralPath $effectiveSignalingTlsCaFile -ErrorAction Stop).Path
}

if ([string]::IsNullOrWhiteSpace($effectiveSupabaseUrl)) {
    throw "Supabase URL is not configured. Set SUPABASE_URL or MEETSPACE_SUPABASE_URL (or pass -SupabaseUrl)."
}
if ([string]::IsNullOrWhiteSpace($effectiveSupabaseAnonKey)) {
    throw "Supabase anon key is not configured. Set SUPABASE_ANON_KEY or MEETSPACE_SUPABASE_ANON_KEY (or pass -SupabaseAnonKey)."
}
if ([string]::IsNullOrWhiteSpace($effectivePostgresConninfo)) {
    throw "Postgres conninfo is not configured. Set POSTGRES_CONNINFO/MEETSPACE_POSTGRES_CONNINFO (or pass -PostgresConninfo)."
}

$env:MEETSPACE_SUPABASE_URL = $effectiveSupabaseUrl
$env:SUPABASE_URL = $effectiveSupabaseUrl
$env:MEETSPACE_SUPABASE_ANON_KEY = $effectiveSupabaseAnonKey
$env:SUPABASE_ANON_KEY = $effectiveSupabaseAnonKey

$env:MEETSPACE_POSTGRES_CONNINFO = $effectivePostgresConninfo
$env:EDUSPACE_POSTGRES_CONNINFO = $effectivePostgresConninfo
$env:POSTGRES_CONNINFO = $effectivePostgresConninfo
$env:MEETSPACE_POSTGRES_POOL_SIZE = $effectivePostgresPoolSize
$env:EDUSPACE_POSTGRES_POOL_SIZE = $effectivePostgresPoolSize
$env:MEETSPACE_SIGNALING_TLS_CERT_FILE = $effectiveSignalingTlsCertFile
$env:EDUSPACE_SIGNALING_TLS_CERT_FILE = $effectiveSignalingTlsCertFile
$env:MEETSPACE_SIGNALING_TLS_KEY_FILE = $effectiveSignalingTlsKeyFile
$env:EDUSPACE_SIGNALING_TLS_KEY_FILE = $effectiveSignalingTlsKeyFile
if (-not [string]::IsNullOrWhiteSpace($effectiveSignalingTlsCaFile)) {
    $env:MEETSPACE_SIGNALING_TLS_CA_FILE = $effectiveSignalingTlsCaFile
    $env:EDUSPACE_SIGNALING_TLS_CA_FILE = $effectiveSignalingTlsCaFile
}
else {
    Remove-Item Env:MEETSPACE_SIGNALING_TLS_CA_FILE -ErrorAction SilentlyContinue
    Remove-Item Env:EDUSPACE_SIGNALING_TLS_CA_FILE -ErrorAction SilentlyContinue
}

Write-Host "[run-all] Starting full stack:"
Write-Host "  signaling: wss://0.0.0.0:$SignalingPort"
Write-Host "  tls cert:  $effectiveSignalingTlsCertFile"
Write-Host "  tls key:   $effectiveSignalingTlsKeyFile"
if ($tlsMaterial.Generated) {
    Write-Host "  tls note:  autogenerated self-signed cert for $PublicIp (replace with trusted CA cert for production clients)."
}
Write-Host "  backend:   ws://127.0.0.1:5001/ws"
Write-Host "  announced: $PublicIp"

Stop-PreviousStackProcesses -Root $resolvedRoot
Try-EnsureFirewallRules

$serverExe = Resolve-ServerExecutable -Root $resolvedRoot -PresetName $Preset -TargetName $Target
if ([string]::IsNullOrWhiteSpace($serverExe)) {
    Write-Host "[run-all] Server binary not found, building first..."
    & powershell -ExecutionPolicy Bypass -File $buildScript -Preset $Preset -Target $Target
    if ($LASTEXITCODE -ne 0) {
        throw "build failed with exit code $LASTEXITCODE"
    }

    $serverExe = Resolve-ServerExecutable -Root $resolvedRoot -PresetName $Preset -TargetName $Target
    if ([string]::IsNullOrWhiteSpace($serverExe)) {
        throw "Server executable not found after build."
    }
}

$runArgs = @(
    "--server",
    "--mediasoup-backend-url", $env:MEETSPACE_MEDIASOUP_BACKEND_URL,
    "--mediasoup-backend-cmd", $env:MEETSPACE_MEDIASOUP_BACKEND_CMD
)

Write-Host "[run-all] Executing: $serverExe $($runArgs -join ' ')"
& $serverExe @runArgs
if ($LASTEXITCODE -ne 0) {
    throw "server exited with code $LASTEXITCODE"
}
