[CmdletBinding()]
param(
    [string]$Preset = "",
    [string]$BuildDir = "",
    [string]$ServerHost = "127.0.0.1",
    [string]$Port = "9002"
)

$ErrorActionPreference = "Stop"

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

Write-Host "[chat-quick-check] Rebuild..."
& pwsh @rebuildArgs
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
& pwsh @smokeArgs
exit $LASTEXITCODE
