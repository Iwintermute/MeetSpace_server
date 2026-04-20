[CmdletBinding()]
param(
    [string]$Preset = "",
    [string]$BuildDir = "",
    [string]$ServerHost = "127.0.0.1",
    [string]$Port = "9002"
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

$rebuildScript = Join-Path $PSScriptRoot "chat-rebuild.ps1"
$smokeScript = Join-Path $PSScriptRoot "chat-smoke-dual.ps1"

if (!(Test-Path $rebuildScript)) {
    throw "chat-rebuild.ps1 not found"
}
if (!(Test-Path $smokeScript)) {
    throw "chat-smoke-dual.ps1 not found"
}

$rebuildArgs = @("-File", $rebuildScript)
if (![string]::IsNullOrWhiteSpace($Preset)) {
    $rebuildArgs += @("-Preset", $Preset)
}
if (![string]::IsNullOrWhiteSpace($BuildDir)) {
    $rebuildArgs += @("-BuildDir", $BuildDir)
}
$psHost = Resolve-PowerShellHost

Write-Host "[chat-quick-check] Rebuild..."
& $psHost @rebuildArgs
if ($LASTEXITCODE -ne 0) {
    throw "Rebuild failed."
}

$smokeArgs = @(
    "-File", $smokeScript,
    "-ServerHost", $ServerHost,
    "-Port", $Port
)
if (![string]::IsNullOrWhiteSpace($BuildDir)) {
    $smokeArgs += @("-BuildDir", $BuildDir)
}

Write-Host "[chat-quick-check] Dual smoke..."
& $psHost @smokeArgs
exit $LASTEXITCODE
