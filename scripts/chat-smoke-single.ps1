[CmdletBinding()]
param(
    [string]$ServerHost = "127.0.0.1",
    [string]$Port = "9002",
    [string]$ConferenceId = "",
    [string]$BuildDir = "",
    [int]$ServerStartupDelayMs = 1500,
    [int]$SmokeWaitMs = 4500,
    [int]$ExitTimeoutMs = 10000
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "chat-common.ps1")

$repoRoot = Get-ChatRepoRoot -ScriptPath $PSCommandPath
$artifacts = Resolve-ChatArtifacts -RepoRoot $repoRoot -BuildDir $BuildDir

if ([string]::IsNullOrWhiteSpace($ConferenceId)) {
    $ConferenceId = "conf-smoke-" + [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
}

$server = $null
$cli = $null

try {
    $server = Start-ChatServer -ServerExe $artifacts.ServerExe
    Start-Sleep -Milliseconds $ServerStartupDelayMs

    $cli = Start-CliClient -CliExe $artifacts.CliExe -ServerHost $ServerHost -Port $Port
    Start-Sleep -Milliseconds 1200

    Send-CliCommand -Process $cli -Command ("testChatSmoke " + $ConferenceId) | Out-Null
    Start-Sleep -Milliseconds $SmokeWaitMs

    Send-CliCommand -Process $cli -Command "quit" | Out-Null
    $exited = Wait-CliExit -Process $cli -TimeoutMs $ExitTimeoutMs
    $output = Read-CliOutput -Process $cli

    $result = [ordered]@{
        conferenceId = $ConferenceId
        exited = $exited
        hasAbort = (Test-HasAbort -Text $output)
        createOk = ($output.Contains('"action":"create_conference"') -and $output.Contains('"ok":true'))
        joinOk = ($output.Contains('"action":"join_conference"') -and $output.Contains('"ok":true'))
        output = $output
    }

    $result | ConvertTo-Json -Depth 5

    if (-not $result.hasAbort -and $result.exited -and $result.createOk -and $result.joinOk) {
        exit 0
    }
    exit 1
}
finally {
    Stop-ProcessSafe -Process $cli
    Stop-ProcessSafe -Process $server
}
