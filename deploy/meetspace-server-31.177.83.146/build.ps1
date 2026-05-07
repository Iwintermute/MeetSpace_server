<#
.SYNOPSIS
Build helper for MeetSpace (MSVC + vcpkg + CMake presets).

.DESCRIPTION
The script auto-detects Visual Studio C++ tools (via vswhere + VsDevCmd),
resolves VCPKG_ROOT (env or common fallback paths), then runs CMake configure/build.

.PARAMETER Preset
CMake configure preset name (default: x64-debug or $env:MEETSPACE_PRESET / $env:EDUSPACE_PRESET).

.PARAMETER BuildDir
Custom binary directory (default: out/build/<preset>-mediasoup or $env:MEETSPACE_BUILD_DIR / $env:EDUSPACE_BUILD_DIR).

.PARAMETER Target
Build target name (default: eds_server_new_mediasoup_app or $env:MEETSPACE_TARGET / $env:EDUSPACE_TARGET).

.PARAMETER ConfigureOnly
Run only CMake configure step.

.PARAMETER BuildOnly
Run only CMake build step.

.PARAMETER Run
Run built executable after successful build.

.EXAMPLE
pwsh -File .\build.ps1

.EXAMPLE
pwsh -File .\build.ps1 -Preset x64-release -Target eds_server_new_mediasoup_app

.EXAMPLE
$env:MEETSPACE_PRESET = "x64-debug"
$env:MEETSPACE_JOBS = "12"
pwsh -File .\build.ps1 -Run
#>
[CmdletBinding()]
param(
    [string]$Preset = "",
    [string]$SourceDir = "",
    [string]$BuildDir = "",
    [string]$Target = "",
    [ValidateSet("ON", "OFF")]
    [string]$BuildCli = "",
    [ValidateSet("ON", "OFF")]
    [string]$BuildServerNew = "",
    [int]$Jobs = 0,
    [switch]$ConfigureOnly,
    [switch]$BuildOnly,
    [switch]$Run,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Preset)) {
    if ($env:MEETSPACE_PRESET) {
        $Preset = $env:MEETSPACE_PRESET
    } elseif ($env:EDUSPACE_PRESET) {
        $Preset = $env:EDUSPACE_PRESET
    } else {
        $Preset = "x64-debug"
    }
}
if ([string]::IsNullOrWhiteSpace($SourceDir)) {
    if ($env:MEETSPACE_SOURCE_DIR) {
        $SourceDir = $env:MEETSPACE_SOURCE_DIR
    } elseif ($env:EDUSPACE_SOURCE_DIR) {
        $SourceDir = $env:EDUSPACE_SOURCE_DIR
    } else {
        $SourceDir = $PSScriptRoot
    }
}
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    if ($env:MEETSPACE_BUILD_DIR) {
        $BuildDir = $env:MEETSPACE_BUILD_DIR
    } elseif ($env:EDUSPACE_BUILD_DIR) {
        $BuildDir = $env:EDUSPACE_BUILD_DIR
    } else {
        $BuildDir = Join-Path $PSScriptRoot ("out/build/{0}-mediasoup" -f $Preset)
    }
}
if ([string]::IsNullOrWhiteSpace($Target)) {
    if ($env:MEETSPACE_TARGET) {
        $Target = $env:MEETSPACE_TARGET
    } elseif ($env:EDUSPACE_TARGET) {
        $Target = $env:EDUSPACE_TARGET
    } else {
        $Target = "eds_server_new_mediasoup_app"
    }
}
if ([string]::IsNullOrWhiteSpace($BuildCli)) {
    if ($env:MEETSPACE_BUILD_CLI) {
        $BuildCli = $env:MEETSPACE_BUILD_CLI
    } elseif ($env:EDUSPACE_BUILD_CLI) {
        $BuildCli = $env:EDUSPACE_BUILD_CLI
    } else {
        $BuildCli = "OFF"
    }
}
if ([string]::IsNullOrWhiteSpace($BuildServerNew)) {
    if ($env:MEETSPACE_BUILD_SERVER_NEW) {
        $BuildServerNew = $env:MEETSPACE_BUILD_SERVER_NEW
    } elseif ($env:EDUSPACE_BUILD_SERVER_NEW) {
        $BuildServerNew = $env:EDUSPACE_BUILD_SERVER_NEW
    } else {
        $BuildServerNew = "ON"
    }
}
if ($Jobs -le 0) {
    if ($env:MEETSPACE_JOBS) {
        $Jobs = [int]$env:MEETSPACE_JOBS
    } elseif ($env:EDUSPACE_JOBS) {
        $Jobs = [int]$env:EDUSPACE_JOBS
    } else {
        $Jobs = 8
    }
}

function Resolve-VsWherePath {
    $defaultPath = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $defaultPath) {
        return $defaultPath
    }

    $found = Get-Command vswhere.exe -ErrorAction SilentlyContinue
    if ($null -ne $found) {
        return $found.Source
    }

    throw "vswhere.exe not found."
}

function Resolve-VcpkgRoot {
    if ($env:VCPKG_ROOT -and (Test-Path $env:VCPKG_ROOT)) {
        return (Resolve-Path $env:VCPKG_ROOT).Path
    }

    $candidates = @(
        (Join-Path $PSScriptRoot "vcpkg"),
        (Join-Path $env:USERPROFILE "vcpkg"),
        "C:\vcpkg"
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            $resolved = (Resolve-Path $candidate).Path
            $env:VCPKG_ROOT = $resolved
            return $resolved
        }
    }

    throw "VCPKG_ROOT is not set and no fallback vcpkg path was found."
}

function Import-MsvcEnvironment {
    param(
        [Parameter(Mandatory = $true)]
        [string]$VsDevCmdPath
    )

    $setOutput = & cmd.exe /d /c "`"$VsDevCmdPath`" -arch=x64 -host_arch=x64 >nul && set"
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to initialize MSVC environment via VsDevCmd.bat."
    }

    foreach ($line in $setOutput) {
        if ($line -match "^([^=]+)=(.*)$") {
            $name = $matches[1]
            $value = $matches[2]
            Set-Item -Path "Env:$name" -Value $value
        }
    }
}

function Invoke-External {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Exe,
        [Parameter(Mandatory = $true)]
        [string[]]$Args
    )

    Write-Host ">> $Exe $($Args -join ' ')"
    & $Exe @Args
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed: $Exe (exit code $LASTEXITCODE)"
    }
}

$SourceDir = [System.IO.Path]::GetFullPath($SourceDir)
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host ">> Removing build directory: $BuildDir"
    Remove-Item -Path $BuildDir -Recurse -Force
}

$vswherePath = Resolve-VsWherePath
$vsInstallPath = (& $vswherePath -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
if ([string]::IsNullOrWhiteSpace($vsInstallPath)) {
    throw "No Visual Studio installation with C++ tools was found."
}

$vsDevCmd = Join-Path $vsInstallPath "Common7\Tools\VsDevCmd.bat"
if (-not (Test-Path $vsDevCmd)) {
    throw "VsDevCmd.bat not found at expected path: $vsDevCmd"
}

$resolvedVcpkgRoot = Resolve-VcpkgRoot
Write-Host "Using Visual Studio: $vsInstallPath"
Write-Host "Using VCPKG_ROOT: $resolvedVcpkgRoot"

Import-MsvcEnvironment -VsDevCmdPath $vsDevCmd
$env:VCPKG_ROOT = $resolvedVcpkgRoot

$env:SUPABASE_URL = "https://mtbbcaykjomycovrxdya.supabase.co"
$env:SUPABASE_ANON_KEY = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6Im10YmJjYXlram9teWNvdnJ4ZHlhIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NzQ5MDkyODUsImV4cCI6MjA5MDQ4NTI4NX0.AKhEpGPBoiLDfUqAu1-MUgvDDrYlw_M0N_wHdXS9Cx4"
$env:MEETSPACE_SUPABASE_URL = $env:SUPABASE_URL
$env:MEETSPACE_SUPABASE_ANON_KEY = $env:SUPABASE_ANON_KEY
$env:MEETSPACE_POSTGRES_CONNINFO = "postgresql://postgres:No_exclus1vee@db.mtbbcaykjomycovrxdya.supabase.co:5432/postgres"
$env:EDUSPACE_POSTGRES_CONNINFO = $env:MEETSPACE_POSTGRES_CONNINFO
$env:POSTGRES_CONNINFO = $env:MEETSPACE_POSTGRES_CONNINFO
$env:MEETSPACE_MEDIASOUP_BACKEND_URL = "ws://127.0.0.1:5001/ws"
$env:EDUSPACE_MEDIASOUP_BACKEND_URL = $env:MEETSPACE_MEDIASOUP_BACKEND_URL
$env:MEETSPACE_MEDIASOUP_BACKEND_CMD = ".\apps\mediasoup-server\run-backend.cmd"
$env:EDUSPACE_MEDIASOUP_BACKEND_CMD = $env:MEETSPACE_MEDIASOUP_BACKEND_CMD

# --- Mediasoup VPS / remote media ---
# Auto-detect public IP for ANNOUNCED_IP so remote clients can reach media
if (-not $env:MEDIASOUP_ANNOUNCED_IP) {
    try {
        $detectedPublicIp = (Invoke-WebRequest -Uri 'https://api.ipify.org' -UseBasicParsing -TimeoutSec 5).Content.Trim()
        if ($detectedPublicIp -and $detectedPublicIp -ne '127.0.0.1') {
            $env:MEDIASOUP_ANNOUNCED_IP = $detectedPublicIp
        }
    } catch {
        # Fallback: first non-loopback IPv4 from adapter
        $adapterIp = (Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
            Where-Object { $_.IPAddress -ne '127.0.0.1' -and $_.PrefixOrigin -ne 'WellKnown' } |
            Select-Object -First 1).IPAddress
        if ($adapterIp) {
            $env:MEDIASOUP_ANNOUNCED_IP = $adapterIp
        }
    }
}
# RTC listen IP: 0.0.0.0 when remote (announced IP set), 127.0.0.1 for local
if (-not $env:MEDIASOUP_RTC_LISTEN_IP) {
    if ($env:MEDIASOUP_ANNOUNCED_IP) {
        $env:MEDIASOUP_RTC_LISTEN_IP = "0.0.0.0"
    } else {
        $env:MEDIASOUP_RTC_LISTEN_IP = "127.0.0.1"
    }
}
# RTP port range for media
if (-not $env:MEDIASOUP_RTC_MIN_PORT) { $env:MEDIASOUP_RTC_MIN_PORT = "40000" }
if (-not $env:MEDIASOUP_RTC_MAX_PORT) { $env:MEDIASOUP_RTC_MAX_PORT = "49999" }

Write-Host "Re-applied VCPKG_ROOT after VsDevCmd: $env:VCPKG_ROOT"
Write-Host "Using SUPABASE_URL: $env:SUPABASE_URL"
Write-Host "Using SUPABASE_ANON_KEY: [set]"
Write-Host "Using MEETSPACE_POSTGRES_CONNINFO: $env:MEETSPACE_POSTGRES_CONNINFO"
Write-Host "Using MEETSPACE_MEDIASOUP_BACKEND_URL: $env:MEETSPACE_MEDIASOUP_BACKEND_URL"
Write-Host "Using MEETSPACE_MEDIASOUP_BACKEND_CMD: $env:MEETSPACE_MEDIASOUP_BACKEND_CMD"
$announcedDisplay = if ($env:MEDIASOUP_ANNOUNCED_IP) { $env:MEDIASOUP_ANNOUNCED_IP } else { 'n/a (local mode)' }
Write-Host "Using MEDIASOUP_ANNOUNCED_IP: $announcedDisplay"
Write-Host "Using MEDIASOUP_RTC_LISTEN_IP: $env:MEDIASOUP_RTC_LISTEN_IP"
Write-Host "Using MEDIASOUP_RTC_MIN_PORT: $env:MEDIASOUP_RTC_MIN_PORT"
Write-Host "Using MEDIASOUP_RTC_MAX_PORT: $env:MEDIASOUP_RTC_MAX_PORT"

if (-not $BuildOnly) {
    $configureArgs = @(
        "--preset", $Preset,
        "-S", $SourceDir,
        "-B", $BuildDir,
        "-DMEETSPACE_BUILD_CLI=$BuildCli",
        "-DMEETSPACE_BUILD_SERVER_NEW=$BuildServerNew",
        "-DEDUSPACE_BUILD_CLI=$BuildCli",
        "-DEDUSPACE_BUILD_SERVER_NEW=$BuildServerNew"
    )
    Invoke-External -Exe "cmake" -Args $configureArgs
}

if (-not $ConfigureOnly) {
    $buildArgs = @(
        "--build", $BuildDir,
        "--target", $Target
    )
    if ($Jobs -gt 0) {
        $buildArgs += @("-j", "$Jobs")
    }

    Invoke-External -Exe "cmake" -Args $buildArgs
}

if ($Run) {
    $exeCandidates = @(
        (Join-Path $BuildDir "EDS_serverNew\$Target.exe"),
        (Join-Path $BuildDir "$Target.exe")
    )
    $exePath = $exeCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace($exePath)) {
        throw "Built executable not found. Checked: $($exeCandidates -join ', ')"
    }

    $runArgs = @(
        "--server",
        "--mediasoup-backend-url", $env:MEETSPACE_MEDIASOUP_BACKEND_URL,
        "--mediasoup-backend-cmd", $env:MEETSPACE_MEDIASOUP_BACKEND_CMD
    )

    Write-Host ">> $exePath $($runArgs -join ' ')"
    & $exePath @runArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Executable returned non-zero exit code: $LASTEXITCODE"
    }
}

Write-Host "Build script completed successfully."