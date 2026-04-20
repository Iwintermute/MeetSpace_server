[CmdletBinding()]
param(
    [string]$Preset = "",
    [string]$BuildDir = ""
)

$ErrorActionPreference = "Stop"

function Resolve-PowerShellHost {
    $currentProcessPath = [System.Diagnostics.Process]::GetCurrentProcess().Path
    if (-not [string]::IsNullOrWhiteSpace($currentProcessPath) -and (Test-Path $currentProcessPath)) {
        return $currentProcessPath
    }

    $pwsh = Get-Command pwsh -ErrorAction SilentlyContinue
    if ($null -ne $pwsh -and -not [string]::IsNullOrWhiteSpace($pwsh.Source)) {
        return $pwsh.Source
    }

    $powershell = Get-Command powershell -ErrorAction SilentlyContinue
    if ($null -ne $powershell -and -not [string]::IsNullOrWhiteSpace($powershell.Source)) {
        return $powershell.Source
    }

    throw "PowerShell host not found (neither current process path, pwsh, nor powershell)."
}

$repoRoot = (& {
    $scriptDir = Split-Path -Parent $PSCommandPath
    (Resolve-Path (Join-Path $scriptDir "..")).Path
})

$buildScript = Join-Path $repoRoot "build.ps1"
if (!(Test-Path $buildScript)) {
    throw "build.ps1 not found: $buildScript"
}

$commonArgs = @(
    "-File", $buildScript,
    "-BuildCli", "ON",
    "-BuildServerNew", "ON"
)

if (![string]::IsNullOrWhiteSpace($Preset)) {
    $commonArgs += @("-Preset", $Preset)
}
if (![string]::IsNullOrWhiteSpace($BuildDir)) {
    $commonArgs += @("-BuildDir", $BuildDir)
}
$psHost = Resolve-PowerShellHost

Write-Host "[chat-rebuild] Build server target..."
& $psHost @commonArgs -Target "eds_server_new_mediasoup_app"
if ($LASTEXITCODE -ne 0) {
    throw "Server build failed with exit code $LASTEXITCODE"
}

Write-Host "[chat-rebuild] Build CLI target..."
& $psHost @commonArgs -BuildOnly -Target "Cli"
if ($LASTEXITCODE -ne 0) {
    throw "Cli build failed with exit code $LASTEXITCODE"
}

Write-Host "[chat-rebuild] done"
