[CmdletBinding()]
param(
    [int]$Runs = 8,
    [string]$ServerHost = "127.0.0.1",
    [string]$Port = "9002",
    [string]$BuildDir = ""
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "chat-common.ps1")

$repoRoot = Get-ChatRepoRoot -ScriptPath $PSCommandPath
$artifacts = Resolve-ChatArtifacts -RepoRoot $repoRoot -BuildDir $BuildDir

$runResults = New-Object System.Collections.Generic.List[object]

for ($i = 1; $i -le $Runs; $i++) {
    $conferenceId = "conf-stress-$i-" + [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
    $server = $null
    $cliA = $null
    $cliB = $null

    try {
        $server = Start-ChatServer -ServerExe $artifacts.ServerExe
        Start-Sleep -Milliseconds 1100

        $cliA = Start-CliClient -CliExe $artifacts.CliExe -ServerHost $ServerHost -Port $Port
        $cliB = Start-CliClient -CliExe $artifacts.CliExe -ServerHost $ServerHost -Port $Port
        Start-Sleep -Milliseconds 1200

        Send-CliCommand -Process $cliA -Command ("testConfCreate " + $conferenceId) | Out-Null
        Send-CliCommand -Process $cliA -Command ("testConfJoin " + $conferenceId) | Out-Null
        Start-Sleep -Milliseconds 700
        Send-CliCommand -Process $cliB -Command ("testConfJoin " + $conferenceId) | Out-Null

        Start-Sleep -Milliseconds 800
        Send-CliCommand -Process $cliA -Command ("testChat " + $conferenceId + " hello-a-" + $i) | Out-Null
        Send-CliCommand -Process $cliB -Command ("testChat " + $conferenceId + " hello-b-" + $i) | Out-Null
        Send-CliCommand -Process $cliA -Command ("testChatTo " + $conferenceId + " ghost-peer should-fail") | Out-Null

        Start-Sleep -Milliseconds 1300
        Send-CliCommand -Process $cliA -Command "quit" | Out-Null
        Send-CliCommand -Process $cliB -Command "quit" | Out-Null

        $aExited = Wait-CliExit -Process $cliA -TimeoutMs 7000
        $bExited = Wait-CliExit -Process $cliB -TimeoutMs 7000
        $outA = Read-CliOutput -Process $cliA
        $outB = Read-CliOutput -Process $cliB

        $hasAbort = ((Test-HasAbort -Text $outA) -or (Test-HasAbort -Text $outB))
        $receivedByB = ($outB.Contains('"type":"chat_message"') -or $outB.Contains('"type": "chat_message"'))
        $invalidTargetRejected = $outA.Contains("target peer is not conference member")
        $ok = (-not $hasAbort) -and $aExited -and $bExited -and $receivedByB -and $invalidTargetRejected

        $runResults.Add([ordered]@{
            run = $i
            conferenceId = $conferenceId
            ok = $ok
            hasAbort = $hasAbort
            cliAExited = $aExited
            cliBExited = $bExited
            receivedByB = $receivedByB
            invalidTargetRejected = $invalidTargetRejected
        }) | Out-Null
    }
    finally {
        Stop-ProcessSafe -Process $cliA
        Stop-ProcessSafe -Process $cliB
        Stop-ProcessSafe -Process $server
    }
}

$abortCount = ($runResults | Where-Object { $_.hasAbort }).Count
$failedCount = ($runResults | Where-Object { -not $_.ok }).Count

$summary = [ordered]@{
    runs = $Runs
    abortCount = $abortCount
    failedRuns = $failedCount
    results = $runResults
}

$summary | ConvertTo-Json -Depth 7

if ($abortCount -eq 0 -and $failedCount -eq 0) {
    exit 0
}
exit 1
