[CmdletBinding()]
param(
    [string]$ServerHost = "127.0.0.1",
    [string]$Port = "9002",
    [string]$BuildDir = "",
    [int]$ServerStartupDelayMs = 1800,
    [int]$ExitTimeoutMs = 10000
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

function Get-CallIdByClientRequestId {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ClientRequestId,
        [Parameter(Mandatory = $true)]
        [string]$InitiatorUserId
    )

    $py = @'
import os
import sys
import psycopg

client_request_id = sys.argv[1]
initiator_user_id = sys.argv[2]
conninfo = os.environ.get("EDUSPACE_POSTGRES_CONNINFO", "")
if not conninfo:
    print("")
    raise SystemExit(0)

with psycopg.connect(conninfo, connect_timeout=10) as conn:
    with conn.cursor() as cur:
        cur.execute(
            """
            select public_id
              from app.calls
             where client_request_id = %s
               and initiator_user_id = %s::uuid
             order by started_at desc
             limit 1
            """,
            (client_request_id, initiator_user_id)
        )
        row = cur.fetchone()
        print("" if row is None else row[0])
'@

    $callId = $py | python - $ClientRequestId $InitiatorUserId
    if ($null -eq $callId) {
        return ""
    }
    return $callId.Trim()
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

$userAId = "c4487900-a777-45ba-a813-5ddd453d9c4d"
$userBId = "f9a25664-c27d-475c-9a74-105975f77161"
$tokenA = "dev:$userAId|oz.chat.a@example.com"
$tokenB = "dev:$userBId|oz.chat.b@example.com"

$suffix = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
$conferenceId = "conf-it-$suffix"
$callClientRequestId = "call-it-$suffix"
$conferenceTransportA = "tr-conf-a-$suffix"
$directTransportA = "tr-call-a-$suffix"
$directTransportB = "tr-call-b-$suffix"

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
    Start-Sleep -Milliseconds 1400

    $authA = 'send {"object":"auth","agent":"session","action":"bind_session","ctx":{"accessToken":"' + $tokenA + '","deviceId":"it-device-a"}}'
    $authB = 'send {"object":"auth","agent":"session","action":"bind_session","ctx":{"accessToken":"' + $tokenB + '","deviceId":"it-device-b"}}'
    Send-CliCommand -Process $cliA -Command $authA | Out-Null
    Send-CliCommand -Process $cliB -Command $authB | Out-Null
    Start-Sleep -Milliseconds 900

    Send-CliCommand -Process $cliA -Command ("testConfCreate " + $conferenceId) | Out-Null
    Send-CliCommand -Process $cliA -Command ("testConfJoin " + $conferenceId) | Out-Null
    Start-Sleep -Milliseconds 800
    Send-CliCommand -Process $cliB -Command ("testConfJoin " + $conferenceId) | Out-Null
    Start-Sleep -Milliseconds 900

    Send-CliCommand -Process $cliA -Command ("testChat " + $conferenceId + " integration-hello") | Out-Null
    Start-Sleep -Milliseconds 900

    $confMedia = 'send {"object":"conference","agent":"lifecycle","action":"open_transport","ctx":{"conferenceId":"' + $conferenceId + '","transportId":"' + $conferenceTransportA + '"}}'
    Send-CliCommand -Process $cliA -Command $confMedia | Out-Null
    Start-Sleep -Milliseconds 900

    $callCreate = 'send {"object":"direct_call","agent":"lifecycle","action":"create_call","ctx":{"targetUserId":"' + $userBId + '","clientRequestId":"' + $callClientRequestId + '"}}'
    Send-CliCommand -Process $cliA -Command $callCreate | Out-Null
    Start-Sleep -Milliseconds 1300

    $callId = Get-CallIdByClientRequestId -ClientRequestId $callClientRequestId -InitiatorUserId $userAId
    $callIdResolved = -not [string]::IsNullOrWhiteSpace($callId)
    if ($callIdResolved) {
        $callAccept = 'send {"object":"direct_call","agent":"lifecycle","action":"accept_call","ctx":{"callId":"' + $callId + '"}}'
        Send-CliCommand -Process $cliB -Command $callAccept | Out-Null
        Start-Sleep -Milliseconds 1000

        $callMediaA = 'send {"object":"direct_call","agent":"lifecycle","action":"open_transport","ctx":{"callId":"' + $callId + '","transportId":"' + $directTransportA + '"}}'
        $callMediaB = 'send {"object":"direct_call","agent":"lifecycle","action":"open_transport","ctx":{"callId":"' + $callId + '","transportId":"' + $directTransportB + '"}}'
        Send-CliCommand -Process $cliA -Command $callMediaA | Out-Null
        Send-CliCommand -Process $cliB -Command $callMediaB | Out-Null
        Start-Sleep -Milliseconds 1000

        $callIceA = 'send {"object":"direct_call","agent":"lifecycle","action":"webrtc_ice","ctx":{"callId":"' + $callId + '","transportId":"' + $directTransportA + '","candidate":"candidate:1 1 udp 2122260223 127.0.0.1 54545 typ host"}}'
        Send-CliCommand -Process $cliA -Command $callIceA | Out-Null
        Start-Sleep -Milliseconds 800

        $callHangup = 'send {"object":"direct_call","agent":"lifecycle","action":"hangup_call","ctx":{"callId":"' + $callId + '"}}'
        Send-CliCommand -Process $cliA -Command $callHangup | Out-Null
        Start-Sleep -Milliseconds 1000
    }

    Send-CliCommand -Process $cliA -Command "quit" | Out-Null
    Send-CliCommand -Process $cliB -Command "quit" | Out-Null

    $aExited = Wait-CliExit -Process $cliA -TimeoutMs $ExitTimeoutMs
    $bExited = Wait-CliExit -Process $cliB -TimeoutMs $ExitTimeoutMs
    $outA = Read-CliOutput -Process $cliA
    $outB = Read-CliOutput -Process $cliB

    $result = [ordered]@{
        conferenceId = $conferenceId
        callId = $callId
        callIdResolved = $callIdResolved
        cliAExited = $aExited
        cliBExited = $bExited
        hasAbort = ((Test-HasAbort -Text $outA) -or (Test-HasAbort -Text $outB))
        authAOk = (Test-DispatchResultOk -Text $outA -Object "auth" -Action "bind_session")
        authBOk = (Test-DispatchResultOk -Text $outB -Object "auth" -Action "bind_session")
        conferenceCreateOk = (Test-DispatchResultOk -Text $outA -Object "conference" -Action "create_conference")
        conferenceJoinAOk = (Test-DispatchResultOk -Text $outA -Object "conference" -Action "join_conference")
        conferenceJoinBOk = (Test-DispatchResultOk -Text $outB -Object "conference" -Action "join_conference")
        chatDeliveredToB = ($outB.Contains('"type":"chat_message"') -or $outB.Contains('"type": "chat_message"'))
        conferenceMediaOpenOk = (Test-DispatchResultOk -Text $outA -Object "conference" -Action "open_transport")
        directCallCreateOk = (Test-DispatchResultOk -Text $outA -Object "direct_call" -Action "create_call")
        directCallInviteToB = $outB.Contains('"type":"direct_call_invite"')
        directCallAcceptOk = (Test-DispatchResultOk -Text $outB -Object "direct_call" -Action "accept_call")
        directCallMediaOpenOk = ((Test-DispatchResultOk -Text $outA -Object "direct_call" -Action "open_transport") -or (Test-DispatchResultOk -Text $outB -Object "direct_call" -Action "open_transport"))
        directCallHangupOk = ((Test-DispatchResultOk -Text $outA -Object "direct_call" -Action "hangup_call") -or (Test-DispatchResultOk -Text $outB -Object "direct_call" -Action "hangup_call"))
        outputA = $outA
        outputB = $outB
    }

    $result | ConvertTo-Json -Depth 8

    if (-not $result.hasAbort `
        -and $result.cliAExited `
        -and $result.cliBExited `
        -and $result.authAOk `
        -and $result.authBOk `
        -and $result.conferenceCreateOk `
        -and $result.conferenceJoinAOk `
        -and $result.conferenceJoinBOk `
        -and $result.chatDeliveredToB `
        -and $result.conferenceMediaOpenOk `
        -and $result.directCallCreateOk `
        -and $result.callIdResolved `
        -and $result.directCallInviteToB `
        -and $result.directCallAcceptOk `
        -and $result.directCallMediaOpenOk `
        -and $result.directCallHangupOk) {
        exit 0
    }

    exit 1
}
finally {
    Stop-ProcessSafe -Process $cliA
    Stop-ProcessSafe -Process $cliB
    Stop-ProcessSafe -Process $server
}
