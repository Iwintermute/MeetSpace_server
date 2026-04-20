[CmdletBinding()]
param(
    [string]$ServerHost = "127.0.0.1",
    [string]$Port = "9002",
    [string]$ConferenceId = "",
    [string]$BuildDir = "",
    [int]$ServerStartupDelayMs = 1500,
    [int]$ExitTimeoutMs = 8000
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "chat-common.ps1")

function Ensure-EnvFromBuild {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [Parameter(Mandatory = $true)]
        [string]$BuildScriptText
    )

    $current = [Environment]::GetEnvironmentVariable($Name, "Process")
    if (-not [string]::IsNullOrWhiteSpace($current)) {
        return
    }

    $pattern = [Regex]::Escape('$env:' + $Name) + '\s*=\s*"([^"]*)"'
    $match = [Regex]::Match($BuildScriptText, $pattern)
    if ($match.Success) {
        [Environment]::SetEnvironmentVariable($Name, $match.Groups[1].Value, "Process")
    }
}

function Test-DispatchResultOk {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Text,
        [Parameter(Mandatory = $true)]
        [string]$Object,
        [Parameter(Mandatory = $true)]
        [string]$Action
    )

    $objectToken = '"object":"' + $Object + '"'
    $actionToken = '"action":"' + $Action + '"'
    $okToken = '"ok":true'
    $typeToken = '"type":"dispatch_result"'

    foreach ($line in ($Text -split "`r?`n")) {
        if ($line.Contains($objectToken) `
            -and $line.Contains($actionToken) `
            -and $line.Contains($okToken) `
            -and $line.Contains($typeToken)) {
            return $true
        }
    }

    return $false
}

$repoRoot = Get-ChatRepoRoot -ScriptPath $PSCommandPath
$buildScriptPath = Join-Path $repoRoot "build.ps1"
$buildScriptText = Get-Content $buildScriptPath -Raw

Ensure-EnvFromBuild -Name "SUPABASE_URL" -BuildScriptText $buildScriptText
Ensure-EnvFromBuild -Name "SUPABASE_ANON_KEY" -BuildScriptText $buildScriptText
Ensure-EnvFromBuild -Name "EDUSPACE_POSTGRES_CONNINFO" -BuildScriptText $buildScriptText
Ensure-EnvFromBuild -Name "EDUSPACE_MEDIASOUP_BACKEND_URL" -BuildScriptText $buildScriptText
Ensure-EnvFromBuild -Name "EDUSPACE_MEDIASOUP_BACKEND_CMD" -BuildScriptText $buildScriptText
[Environment]::SetEnvironmentVariable("EDUSPACE_ALLOW_DEV_AUTH_TOKENS", "1", "Process")

$backendCommand = [Environment]::GetEnvironmentVariable("EDUSPACE_MEDIASOUP_BACKEND_CMD", "Process")
if ([string]::IsNullOrWhiteSpace($backendCommand)) {
    $backendCommand = ".\\apps\\mediasoup-server\\run-backend.cmd"
}
if (-not [System.IO.Path]::IsPathRooted($backendCommand)) {
    $backendCommand = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $backendCommand))
}
if (!(Test-Path $backendCommand)) {
    throw "Mediasoup backend command not found: $backendCommand"
}
[Environment]::SetEnvironmentVariable("EDUSPACE_MEDIASOUP_BACKEND_CMD", $backendCommand, "Process")

$backendUrl = [Environment]::GetEnvironmentVariable("EDUSPACE_MEDIASOUP_BACKEND_URL", "Process")
if ([string]::IsNullOrWhiteSpace($backendUrl)) {
    [Environment]::SetEnvironmentVariable("EDUSPACE_MEDIASOUP_BACKEND_URL", "ws://127.0.0.1:5001/ws", "Process")
}
$artifacts = Resolve-ChatArtifacts -RepoRoot $repoRoot -BuildDir $BuildDir

if ([string]::IsNullOrWhiteSpace($ConferenceId)) {
    $ConferenceId = "conf-dual-" + [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
}

$userAId = "c4487900-a777-45ba-a813-5ddd453d9c4d"
$userBId = "f9a25664-c27d-475c-9a74-105975f77161"
$tokenA = "dev:$userAId|oz.smoke.a@example.com"
$tokenB = "dev:$userBId|oz.smoke.b@example.com"

$server = $null
$cliA = $null
$cliB = $null

try {
    $server = Start-ChatServer -ServerExe $artifacts.ServerExe -WorkingDirectory $repoRoot
    Start-Sleep -Milliseconds $ServerStartupDelayMs
    if ($server.HasExited) {
        throw "Server process exited during startup with code $($server.ExitCode)."
    }

    $cliA = Start-CliClient -CliExe $artifacts.CliExe -ServerHost $ServerHost -Port $Port
    $cliB = Start-CliClient -CliExe $artifacts.CliExe -ServerHost $ServerHost -Port $Port
    Start-Sleep -Milliseconds 1300
    $authA = 'send {"object":"auth","agent":"session","action":"bind_session","ctx":{"accessToken":"' + $tokenA + '","deviceId":"smoke-device-a"}}'
    $authB = 'send {"object":"auth","agent":"session","action":"bind_session","ctx":{"accessToken":"' + $tokenB + '","deviceId":"smoke-device-b"}}'
    Send-CliCommand -Process $cliA -Command $authA | Out-Null
    Send-CliCommand -Process $cliB -Command $authB | Out-Null
    Start-Sleep -Milliseconds 900

    Send-CliCommand -Process $cliA -Command ("testConfCreate " + $ConferenceId) | Out-Null
    Send-CliCommand -Process $cliA -Command ("testConfJoin " + $ConferenceId) | Out-Null
    Start-Sleep -Milliseconds 700
    Send-CliCommand -Process $cliB -Command ("testConfJoin " + $ConferenceId) | Out-Null

    Start-Sleep -Milliseconds 900
    Send-CliCommand -Process $cliA -Command ("testChat " + $ConferenceId + " hello-from-a") | Out-Null
    Send-CliCommand -Process $cliA -Command ("testChatTo " + $ConferenceId + " ghost-peer should-fail") | Out-Null

    Start-Sleep -Milliseconds 1500
    Send-CliCommand -Process $cliA -Command "quit" | Out-Null
    Send-CliCommand -Process $cliB -Command "quit" | Out-Null

    $aExited = Wait-CliExit -Process $cliA -TimeoutMs $ExitTimeoutMs
    $bExited = Wait-CliExit -Process $cliB -TimeoutMs $ExitTimeoutMs
    $outA = Read-CliOutput -Process $cliA
    $outB = Read-CliOutput -Process $cliB

    $result = [ordered]@{
        conferenceId = $ConferenceId
        cliAExited = $aExited
        cliBExited = $bExited
        hasAbort = ((Test-HasAbort -Text $outA) -or (Test-HasAbort -Text $outB))
        authAOk = (Test-DispatchResultOk -Text $outA -Object "auth" -Action "bind_session")
        authBOk = (Test-DispatchResultOk -Text $outB -Object "auth" -Action "bind_session")
        receivedByB = ($outB.Contains('"type":"chat_message"') -or $outB.Contains('"type": "chat_message"'))
        invalidTargetRejected = (
            $outA.Contains("target peer is not conference member") -or
            $outA.Contains("target peer is not connected") -or
            $outB.Contains("target peer is not conference member") -or
            $outB.Contains("target peer is not connected")
        )
        outputA = $outA
        outputB = $outB
    }

    $result | ConvertTo-Json -Depth 6

    if (-not $result.hasAbort -and $result.cliAExited -and $result.cliBExited -and $result.authAOk -and $result.authBOk -and $result.receivedByB -and $result.invalidTargetRejected) {
        exit 0
    }
    exit 1
}
finally {
    Stop-ProcessSafe -Process $cliA
    Stop-ProcessSafe -Process $cliB
    Stop-ProcessSafe -Process $server
}
