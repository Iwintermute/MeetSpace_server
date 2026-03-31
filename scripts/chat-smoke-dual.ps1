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

$repoRoot = Get-ChatRepoRoot -ScriptPath $PSCommandPath
$artifacts = Resolve-ChatArtifacts -RepoRoot $repoRoot -BuildDir $BuildDir

if ([string]::IsNullOrWhiteSpace($ConferenceId)) {
    $ConferenceId = "conf-dual-" + [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
}

$server = $null
$cliA = $null
$cliB = $null

try {
    $server = Start-ChatServer -ServerExe $artifacts.ServerExe
    Start-Sleep -Milliseconds $ServerStartupDelayMs

    $cliA = Start-CliClient -CliExe $artifacts.CliExe -ServerHost $ServerHost -Port $Port
    $cliB = Start-CliClient -CliExe $artifacts.CliExe -ServerHost $ServerHost -Port $Port
    Start-Sleep -Milliseconds 1300

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
        receivedByB = ($outB.Contains('"type":"chat_message"') -or $outB.Contains('"type": "chat_message"'))
        invalidTargetRejected = $outA.Contains("target peer is not conference member")
        outputA = $outA
        outputB = $outB
    }

    $result | ConvertTo-Json -Depth 6

    if (-not $result.hasAbort -and $result.cliAExited -and $result.cliBExited -and $result.receivedByB -and $result.invalidTargetRejected) {
        exit 0
    }
    exit 1
}
finally {
    Stop-ProcessSafe -Process $cliA
    Stop-ProcessSafe -Process $cliB
    Stop-ProcessSafe -Process $server
}
