[CmdletBinding()]
param(
    [string]$Preset = "",
    [string]$BuildDir = ""
)

$ErrorActionPreference = "Stop"

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

Write-Host "[chat-rebuild] Build server target..."
& pwsh @commonArgs -Target "eds_server_new_mediasoup_app"
if ($LASTEXITCODE -ne 0) {
    throw "Server build failed with exit code $LASTEXITCODE"
}

Write-Host "[chat-rebuild] Build CLI target..."
& pwsh @commonArgs -BuildOnly -Target "Cli"
if ($LASTEXITCODE -ne 0) {
    throw "Cli build failed with exit code $LASTEXITCODE"
}

Write-Host "[chat-rebuild] done"
