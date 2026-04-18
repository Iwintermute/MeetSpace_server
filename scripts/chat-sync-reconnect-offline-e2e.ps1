[CmdletBinding()]
param(
    [string]$ServerHost = "127.0.0.1",
    [string]$Port = "9002",
    [string]$BuildDir = "",
    [int]$ServerStartupDelayMs = 2000,
    [int]$StepDelayMs = 750,
    [int]$ReconnectDeliveryWaitMs = 4200,
    [int]$ExitTimeoutMs = 9000
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

function Parse-CliIncomingJson {
    param(
        [Parameter(Mandatory = $true)]
        [string]$OutputText
    )

    $events = New-Object System.Collections.Generic.List[object]
    $lines = $OutputText -split "(`r`n|`n|`r)"
    foreach ($line in $lines) {
        if ([string]::IsNullOrWhiteSpace($line)) {
            continue
        }

        $payload = $null
        $index = $line.IndexOf("< ")
        if ($index -ge 0) {
            $candidate = $line.Substring($index + 2).Trim()
            if ($candidate.StartsWith("{") -or $candidate.StartsWith("[")) {
                $payload = $candidate
            }
        }

        if ($null -eq $payload) {
            continue
        }

        try {
            $json = $payload | ConvertFrom-Json -ErrorAction Stop
            $events.Add($json) | Out-Null
        } catch {
        }
    }

    return $events
}

function Wait-CliStep {
    param(
        [int]$DelayMs,
        [System.Diagnostics.Process[]]$Processes
    )

    Start-SleepWithCliDrain -DelayMs $DelayMs -Processes $Processes
}

function Send-CliCommandChecked {
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)]
        [string]$Command,
        [string]$Label = ""
    )

    $ok = Send-CliCommand -Process $Process -Command $Command
    if (-not $ok) {
        if ([string]::IsNullOrWhiteSpace($Label)) {
            $Label = $Command
        }
        throw "Failed to send CLI command: $Label"
    }
}

function Send-CliJsonCommand {
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)]
        [hashtable]$Payload
    )

    $serialized = $Payload | ConvertTo-Json -Depth 14 -Compress
    Send-CliCommandChecked -Process $Process -Command ("send " + $serialized) -Label ("send " + $Payload.action)
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

function Get-ConferenceMessageIdByClientRequestId {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ClientRequestId,
        [Parameter(Mandatory = $true)]
        [string]$SenderUserId
    )

    $py = @'
import os
import sys
import psycopg

client_request_id = sys.argv[1]
sender_user_id = sys.argv[2]
conninfo = os.environ.get("EDUSPACE_POSTGRES_CONNINFO", "")
if not conninfo:
    print("")
    raise SystemExit(0)

with psycopg.connect(conninfo, connect_timeout=10) as conn:
    with conn.cursor() as cur:
        cur.execute(
            """
            select id::text
              from app.conference_messages
             where client_request_id = %s
               and sender_user_id = %s::uuid
             order by created_at desc, id desc
             limit 1
            """,
            (client_request_id, sender_user_id)
        )
        row = cur.fetchone()
        print("" if row is None else row[0])
'@

    $messageId = $py | python - $ClientRequestId $SenderUserId
    if ($null -eq $messageId) {
        return ""
    }
    return $messageId.Trim()
}

function Get-DirectMessageInfoByClientRequestId {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ClientRequestId,
        [Parameter(Mandatory = $true)]
        [string]$SenderUserId
    )

    $py = @'
import json
import os
import sys
import psycopg

client_request_id = sys.argv[1]
sender_user_id = sys.argv[2]
conninfo = os.environ.get("EDUSPACE_POSTGRES_CONNINFO", "")
if not conninfo:
    print("{}")
    raise SystemExit(0)

with psycopg.connect(conninfo, connect_timeout=10) as conn:
    with conn.cursor() as cur:
        cur.execute(
            """
            select m.id::text, m.thread_id::text
              from app.direct_messages m
             where m.client_request_id = %s
               and m.sender_user_id = %s::uuid
             order by m.created_at desc, m.id desc
             limit 1
            """,
            (client_request_id, sender_user_id)
        )
        row = cur.fetchone()
        if row is None:
            print("{}")
        else:
            print(json.dumps({"messageId": row[0], "threadId": row[1]}))
'@

    $jsonText = $py | python - $ClientRequestId $SenderUserId
    if ($null -eq $jsonText) {
        return $null
    }
    $trimmed = $jsonText.Trim()
    if ([string]::IsNullOrWhiteSpace($trimmed)) {
        return $null
    }
    try {
        return ($trimmed | ConvertFrom-Json -ErrorAction Stop)
    } catch {
        return $null
    }
}

function Find-DispatchResult {
    param(
        [Parameter(Mandatory = $true)]
        [object[]]$Events,
        [Parameter(Mandatory = $true)]
        [string]$Object,
        [Parameter(Mandatory = $true)]
        [string]$Action,
        [string]$ClientRequestId = ""
    )

    $matches = @($Events | Where-Object {
            $_.type -eq "dispatch_result" -and
            $_.object -eq $Object -and
            $_.action -eq $Action
        })

    if (-not [string]::IsNullOrWhiteSpace($ClientRequestId)) {
        $matches = @($matches | Where-Object {
                $_.clientRequestId -eq $ClientRequestId -or $_.correlationId -eq $ClientRequestId
            })
    }

    if ($matches.Count -eq 0) {
        return $null
    }
    return $matches[-1]
}

function Has-CallId {
    param(
        [Parameter(Mandatory = $false)]
        $Calls,
        [Parameter(Mandatory = $true)]
        [string]$CallId
    )
    if ([string]::IsNullOrWhiteSpace($CallId)) {
        return $false
    }
    foreach ($call in @($Calls)) {
        if ($null -ne $call -and $call.callId -eq $CallId) {
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

$artifacts = Resolve-ChatArtifacts -RepoRoot $repoRoot -BuildDir $BuildDir

$userAId = "c4487900-a777-45ba-a813-5ddd453d9c4d"
$userBId = "f9a25664-c27d-475c-9a74-105975f77161"
$tokenA = "dev:$userAId|oz.chat.a@example.com"
$tokenB = "dev:$userBId|oz.chat.b@example.com"

$suffix = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
$conferenceId = "conf-sync-$suffix"
$conferenceMsgReq = "conf-msg-$suffix"
$conferenceMsgText = "SYNC_CONF::$suffix"
$directMsgReq = "dm-msg-$suffix"
$directMsgText = "SYNC_DM::$suffix"
$activeCallReq = "call-active-$suffix"
$offlineMsgReq = "dm-offline-$suffix"
$offlineMsgText = "OFFLINE_DM::$suffix"
$offlineCallReq = "call-offline-$suffix"

$server = $null
$cliA = $null
$cliB = $null
$cliBReconnect = $null
$cliBOffline = $null
$outA = ""
$outBPrimary = ""
$outBReconnect = ""
$outBOffline = ""

try {
    Write-Output "[sync-e2e] step=server_start"
    $server = Start-ChatServer -ServerExe $artifacts.ServerExe
    Start-Sleep -Milliseconds $ServerStartupDelayMs

    Write-Output "[sync-e2e] step=cli_start_primary"
    $cliA = Start-CliClient -CliExe $artifacts.CliExe -ServerHost $ServerHost -Port $Port
    $cliB = Start-CliClient -CliExe $artifacts.CliExe -ServerHost $ServerHost -Port $Port
    Wait-CliStep -DelayMs 1400 -Processes @($cliA, $cliB)

    Write-Output "[sync-e2e] step=auth_bind_primary"
    Send-CliJsonCommand -Process $cliA -Payload @{
        object = "auth"; agent = "session"; action = "bind_session"; ctx = @{ accessToken = $tokenA; deviceId = "sync-device-a" }
    }
    Send-CliJsonCommand -Process $cliB -Payload @{
        object = "auth"; agent = "session"; action = "bind_session"; ctx = @{ accessToken = $tokenB; deviceId = "sync-device-b" }
    }
    Wait-CliStep -DelayMs ($StepDelayMs + 250) -Processes @($cliA, $cliB)

    Write-Output "[sync-e2e] step=conference_create_join"
    Send-CliJsonCommand -Process $cliA -Payload @{
        object = "conference"; agent = "lifecycle"; action = "create_conference"; ctx = @{ conferenceId = $conferenceId; clientRequestId = "conf-create-$suffix" }
    }
    Wait-CliStep -DelayMs $StepDelayMs -Processes @($cliA, $cliB)
    Send-CliJsonCommand -Process $cliB -Payload @{
        object = "conference"; agent = "lifecycle"; action = "join_conference"; ctx = @{ conferenceId = $conferenceId; clientRequestId = "conf-join-$suffix" }
    }
    Wait-CliStep -DelayMs ($StepDelayMs + 250) -Processes @($cliA, $cliB)

    Write-Output "[sync-e2e] step=conference_sync_ack"
    Send-CliJsonCommand -Process $cliA -Payload @{
        object = "chat"; agent = "messaging"; action = "send_message"; ctx = @{ conferenceId = $conferenceId; clientRequestId = $conferenceMsgReq; text = $conferenceMsgText }
    }
    Wait-CliStep -DelayMs ($StepDelayMs + 300) -Processes @($cliA, $cliB)
    $conferenceMessageId = Get-ConferenceMessageIdByClientRequestId -ClientRequestId $conferenceMsgReq -SenderUserId $userAId
    if ([string]::IsNullOrWhiteSpace($conferenceMessageId)) {
        throw "Unable to resolve conference messageId for sync/ack validation."
    }
    Send-CliJsonCommand -Process $cliB -Payload @{
        object = "chat"; agent = "messaging"; action = "sync_messages"; ctx = @{ conferenceId = $conferenceId; limit = 30 }
    }
    Wait-CliStep -DelayMs $StepDelayMs -Processes @($cliA, $cliB)
    Send-CliJsonCommand -Process $cliB -Payload @{
        object = "chat"; agent = "messaging"; action = "ack_messages"; ctx = @{ conferenceId = $conferenceId; messageIds = @($conferenceMessageId); markRead = $true }
    }
    Wait-CliStep -DelayMs $StepDelayMs -Processes @($cliA, $cliB)

    Write-Output "[sync-e2e] step=direct_chat_sync_ack"
    Send-CliJsonCommand -Process $cliA -Payload @{
        object = "direct_chat"; agent = "messaging"; action = "send_message"; ctx = @{ targetUserId = $userBId; clientRequestId = $directMsgReq; text = $directMsgText }
    }
    Wait-CliStep -DelayMs ($StepDelayMs + 300) -Processes @($cliA, $cliB)
    $directMsgInfo = Get-DirectMessageInfoByClientRequestId -ClientRequestId $directMsgReq -SenderUserId $userAId
    if ($null -eq $directMsgInfo -or [string]::IsNullOrWhiteSpace($directMsgInfo.messageId)) {
        throw "Unable to resolve direct messageId for sync/ack validation."
    }
    Send-CliJsonCommand -Process $cliB -Payload @{
        object = "direct_chat"; agent = "messaging"; action = "list_threads"; ctx = @{ limit = 30 }
    }
    Wait-CliStep -DelayMs $StepDelayMs -Processes @($cliA, $cliB)
    Send-CliJsonCommand -Process $cliB -Payload @{
        object = "direct_chat"; agent = "messaging"; action = "sync_messages"; ctx = @{ targetUserId = $userAId; limit = 30 }
    }
    Wait-CliStep -DelayMs $StepDelayMs -Processes @($cliA, $cliB)
    Send-CliJsonCommand -Process $cliB -Payload @{
        object = "direct_chat"; agent = "messaging"; action = "ack_messages"; ctx = @{ targetUserId = $userAId; messageIds = @($directMsgInfo.messageId); markRead = $true }
    }
    Wait-CliStep -DelayMs $StepDelayMs -Processes @($cliA, $cliB)

    Write-Output "[sync-e2e] step=list_active_calls_and_conferences"
    Send-CliJsonCommand -Process $cliA -Payload @{
        object = "direct_call"; agent = "lifecycle"; action = "create_call"; ctx = @{ targetUserId = $userBId; clientRequestId = $activeCallReq }
    }
    Wait-CliStep -DelayMs ($StepDelayMs + 500) -Processes @($cliA, $cliB)
    $activeCallId = Get-CallIdByClientRequestId -ClientRequestId $activeCallReq -InitiatorUserId $userAId
    if ([string]::IsNullOrWhiteSpace($activeCallId)) {
        throw "Unable to resolve active callId for reconnect/list validation."
    }
    Send-CliJsonCommand -Process $cliB -Payload @{
        object = "direct_call"; agent = "lifecycle"; action = "list_active_calls"; ctx = @{ limit = 30 }
    }
    Wait-CliStep -DelayMs $StepDelayMs -Processes @($cliA, $cliB)
    Send-CliJsonCommand -Process $cliA -Payload @{
        object = "conference"; agent = "lifecycle"; action = "list_user_conferences"; ctx = @{ limit = 30 }
    }
    Wait-CliStep -DelayMs $StepDelayMs -Processes @($cliA, $cliB)

    Write-Output "[sync-e2e] step=reconnect_snapshot_check"
    Send-CliJsonCommand -Process $cliB -Payload @{
        object = "auth"; agent = "session"; action = "logout_session"; ctx = @{}
    }
    Wait-CliStep -DelayMs $StepDelayMs -Processes @($cliA, $cliB)
    Send-CliCommandChecked -Process $cliB -Command "quit" -Label "quit primary B"
    [void](Wait-CliExit -Process $cliB -TimeoutMs $ExitTimeoutMs)
    $outBPrimary = Read-CliOutput -Process $cliB
    $cliB = $null

    $cliBReconnect = Start-CliClient -CliExe $artifacts.CliExe -ServerHost $ServerHost -Port $Port
    Wait-CliStep -DelayMs 1300 -Processes @($cliA, $cliBReconnect)
    Send-CliJsonCommand -Process $cliBReconnect -Payload @{
        object = "auth"; agent = "session"; action = "bind_session"; ctx = @{ accessToken = $tokenB; deviceId = "sync-device-b-reconnect" }
    }
    Wait-CliStep -DelayMs ($StepDelayMs + 450) -Processes @($cliA, $cliBReconnect)
    Send-CliJsonCommand -Process $cliBReconnect -Payload @{
        object = "auth"; agent = "session"; action = "logout_session"; ctx = @{}
    }
    Wait-CliStep -DelayMs $StepDelayMs -Processes @($cliA, $cliBReconnect)
    Send-CliCommandChecked -Process $cliBReconnect -Command "quit" -Label "quit reconnect B"
    [void](Wait-CliExit -Process $cliBReconnect -TimeoutMs $ExitTimeoutMs)
    $outBReconnect = Read-CliOutput -Process $cliBReconnect
    $cliBReconnect = $null

    Write-Output "[sync-e2e] step=offline_delivery_check"
    Send-CliJsonCommand -Process $cliA -Payload @{
        object = "direct_chat"; agent = "messaging"; action = "send_message"; ctx = @{ targetUserId = $userBId; clientRequestId = $offlineMsgReq; text = $offlineMsgText }
    }
    Wait-CliStep -DelayMs ($StepDelayMs + 300) -Processes @($cliA)
    $offlineMsgInfo = Get-DirectMessageInfoByClientRequestId -ClientRequestId $offlineMsgReq -SenderUserId $userAId
    if ($null -eq $offlineMsgInfo -or [string]::IsNullOrWhiteSpace($offlineMsgInfo.messageId)) {
        throw "Unable to resolve offline direct message id."
    }
    Send-CliJsonCommand -Process $cliA -Payload @{
        object = "direct_call"; agent = "lifecycle"; action = "create_call"; ctx = @{ targetUserId = $userBId; clientRequestId = $offlineCallReq }
    }
    Wait-CliStep -DelayMs ($StepDelayMs + 500) -Processes @($cliA)
    $offlineCallId = Get-CallIdByClientRequestId -ClientRequestId $offlineCallReq -InitiatorUserId $userAId
    if ([string]::IsNullOrWhiteSpace($offlineCallId)) {
        throw "Unable to resolve offline callId."
    }

    $cliBOffline = Start-CliClient -CliExe $artifacts.CliExe -ServerHost $ServerHost -Port $Port
    Wait-CliStep -DelayMs 1300 -Processes @($cliA, $cliBOffline)
    Send-CliJsonCommand -Process $cliBOffline -Payload @{
        object = "auth"; agent = "session"; action = "bind_session"; ctx = @{ accessToken = $tokenB; deviceId = "sync-device-b-offline" }
    }
    Wait-CliStep -DelayMs $ReconnectDeliveryWaitMs -Processes @($cliA, $cliBOffline)
    Send-CliJsonCommand -Process $cliBOffline -Payload @{
        object = "direct_chat"; agent = "messaging"; action = "sync_messages"; ctx = @{ targetUserId = $userAId; limit = 50 }
    }
    Wait-CliStep -DelayMs $StepDelayMs -Processes @($cliA, $cliBOffline)
    Send-CliJsonCommand -Process $cliBOffline -Payload @{
        object = "direct_call"; agent = "lifecycle"; action = "list_active_calls"; ctx = @{ limit = 50 }
    }
    Wait-CliStep -DelayMs $StepDelayMs -Processes @($cliA, $cliBOffline)

    Send-CliCommandChecked -Process $cliA -Command "quit" -Label "quit A"
    Send-CliCommandChecked -Process $cliBOffline -Command "quit" -Label "quit offline B"
    [void](Wait-CliExit -Process $cliA -TimeoutMs $ExitTimeoutMs)
    [void](Wait-CliExit -Process $cliBOffline -TimeoutMs $ExitTimeoutMs)
    $outA = Read-CliOutput -Process $cliA
    $outBOffline = Read-CliOutput -Process $cliBOffline
    $cliA = $null
    $cliBOffline = $null

    Write-Output "[sync-e2e] step=assertions"
    $eventsA = Parse-CliIncomingJson -OutputText $outA
    $eventsBPrimary = Parse-CliIncomingJson -OutputText $outBPrimary
    $eventsBReconnect = Parse-CliIncomingJson -OutputText $outBReconnect
    $eventsBOffline = Parse-CliIncomingJson -OutputText $outBOffline

    $authA = Find-DispatchResult -Events $eventsA -Object "auth" -Action "bind_session"
    $authBPrimary = Find-DispatchResult -Events $eventsBPrimary -Object "auth" -Action "bind_session"
    $authBReconnect = Find-DispatchResult -Events $eventsBReconnect -Object "auth" -Action "bind_session"
    $authBOffline = Find-DispatchResult -Events $eventsBOffline -Object "auth" -Action "bind_session"

    $conferenceCreate = Find-DispatchResult -Events $eventsA -Object "conference" -Action "create_conference"
    $conferenceJoin = Find-DispatchResult -Events $eventsBPrimary -Object "conference" -Action "join_conference"
    $conferenceSync = Find-DispatchResult -Events $eventsBPrimary -Object "chat" -Action "sync_messages"
    $conferenceAck = Find-DispatchResult -Events $eventsBPrimary -Object "chat" -Action "ack_messages"

    $directListThreads = Find-DispatchResult -Events $eventsBPrimary -Object "direct_chat" -Action "list_threads"
    $directSync = Find-DispatchResult -Events $eventsBPrimary -Object "direct_chat" -Action "sync_messages"
    $directAck = Find-DispatchResult -Events $eventsBPrimary -Object "direct_chat" -Action "ack_messages"
    $primaryListCalls = Find-DispatchResult -Events $eventsBPrimary -Object "direct_call" -Action "list_active_calls"
    $offlineListCalls = Find-DispatchResult -Events $eventsBOffline -Object "direct_call" -Action "list_active_calls"
    $listConferences = Find-DispatchResult -Events $eventsA -Object "conference" -Action "list_user_conferences"

    $reconnectSnapshot = $null
    if ($null -ne $authBReconnect -and $null -ne $authBReconnect.data) {
        $reconnectSnapshot = $authBReconnect.data.reconnect
    }

    $reconnectConferences = @()
    $reconnectThreads = @()
    $reconnectCalls = @()
    if ($null -ne $reconnectSnapshot) {
        $reconnectConferences = @($reconnectSnapshot.conferences)
        $reconnectThreads = @($reconnectSnapshot.directThreads)
        $reconnectCalls = @($reconnectSnapshot.activeDirectCalls)
    }

    $conferenceListData = @()
    if ($null -ne $listConferences -and $null -ne $listConferences.data -and $null -ne $listConferences.data.conferences) {
        $conferenceListData = @($listConferences.data.conferences)
    }

    $primaryCalls = @()
    if ($null -ne $primaryListCalls -and $null -ne $primaryListCalls.data -and $null -ne $primaryListCalls.data.calls) {
        $primaryCalls = @($primaryListCalls.data.calls)
    }
    $offlineCalls = @()
    if ($null -ne $offlineListCalls -and $null -ne $offlineListCalls.data -and $null -ne $offlineListCalls.data.calls) {
        $offlineCalls = @($offlineListCalls.data.calls)
    }

    $offlineDirectMessages = @($eventsBOffline | Where-Object { $_.type -eq "direct_chat_message" -and $_.text -eq $offlineMsgText })
    $offlineCallInvites = @($eventsBOffline | Where-Object { $_.type -eq "direct_call_invite" -and $_.callId -eq $offlineCallId })

    $conferenceAckedCount = 0
    if ($null -ne $conferenceAck -and $null -ne $conferenceAck.data -and $null -ne $conferenceAck.data.ackedCount) {
        $conferenceAckedCount = [int]$conferenceAck.data.ackedCount
    }
    $directAckedCount = 0
    if ($null -ne $directAck -and $null -ne $directAck.data -and $null -ne $directAck.data.ackedCount) {
        $directAckedCount = [int]$directAck.data.ackedCount
    }

    $conferencePresentInList = (@($conferenceListData | Where-Object { $_.conferencePublicId -eq $conferenceId }).Count -ge 1)
    $conferencePresentInReconnect = (@($reconnectConferences | Where-Object { $_.conferencePublicId -eq $conferenceId }).Count -ge 1)
    $threadPresentInReconnect = (@($reconnectThreads | Where-Object { $_.counterpartUserId -eq $userAId }).Count -ge 1)
    $activeCallPresentInReconnect = Has-CallId -Calls $reconnectCalls -CallId $activeCallId
    $activeCallPresentPrimary = Has-CallId -Calls $primaryCalls -CallId $activeCallId
    $offlineCallPresentAfterReconnect = Has-CallId -Calls $offlineCalls -CallId $offlineCallId

    $result = [ordered]@{
        conferenceId = $conferenceId
        conferenceMessageId = $conferenceMessageId
        directThreadId = $directMsgInfo.threadId
        directMessageId = $directMsgInfo.messageId
        activeCallId = $activeCallId
        offlineMessageId = $offlineMsgInfo.messageId
        offlineCallId = $offlineCallId
        authAOk = ($null -ne $authA -and $authA.ok -eq $true)
        authBPrimaryOk = ($null -ne $authBPrimary -and $authBPrimary.ok -eq $true)
        authBReconnectOk = ($null -ne $authBReconnect -and $authBReconnect.ok -eq $true)
        authBOfflineOk = ($null -ne $authBOffline -and $authBOffline.ok -eq $true)
        conferenceCreateOk = ($null -ne $conferenceCreate -and $conferenceCreate.ok -eq $true)
        conferenceJoinOk = ($null -ne $conferenceJoin -and $conferenceJoin.ok -eq $true)
        conferenceSyncOk = ($null -ne $conferenceSync -and $conferenceSync.ok -eq $true)
        conferenceAckOk = ($null -ne $conferenceAck -and $conferenceAck.ok -eq $true -and $conferenceAckedCount -ge 1)
        directListThreadsOk = ($null -ne $directListThreads -and $directListThreads.ok -eq $true)
        directSyncOk = ($null -ne $directSync -and $directSync.ok -eq $true)
        directAckOk = ($null -ne $directAck -and $directAck.ok -eq $true -and $directAckedCount -ge 1)
        activeCallsPrimaryOk = ($null -ne $primaryListCalls -and $primaryListCalls.ok -eq $true -and $activeCallPresentPrimary)
        listConferencesOk = ($null -ne $listConferences -and $listConferences.ok -eq $true -and $conferencePresentInList)
        reconnectConferencePresent = $conferencePresentInReconnect
        reconnectDirectThreadPresent = $threadPresentInReconnect
        reconnectActiveCallPresent = $activeCallPresentInReconnect
        offlineDirectMessageDelivered = ($offlineDirectMessages.Count -ge 1)
        offlineCallInviteDelivered = ($offlineCallInvites.Count -ge 1)
        offlineListActiveCallsContainsCall = ($null -ne $offlineListCalls -and $offlineListCalls.ok -eq $true -and $offlineCallPresentAfterReconnect)
        reconnectConferencesCount = $reconnectConferences.Count
        reconnectDirectThreadsCount = $reconnectThreads.Count
        reconnectActiveCallsCount = $reconnectCalls.Count
        outputTailA = (($outA -split "(`r`n|`n|`r)") | Select-Object -Last 20)
        outputTailBPrimary = (($outBPrimary -split "(`r`n|`n|`r)") | Select-Object -Last 20)
        outputTailBReconnect = (($outBReconnect -split "(`r`n|`n|`r)") | Select-Object -Last 20)
        outputTailBOffline = (($outBOffline -split "(`r`n|`n|`r)") | Select-Object -Last 20)
    }

    $result | ConvertTo-Json -Depth 8

    $isOk =
        $result.authAOk -and
        $result.authBPrimaryOk -and
        $result.authBReconnectOk -and
        $result.authBOfflineOk -and
        $result.conferenceCreateOk -and
        $result.conferenceJoinOk -and
        $result.conferenceSyncOk -and
        $result.conferenceAckOk -and
        $result.directListThreadsOk -and
        $result.directSyncOk -and
        $result.directAckOk -and
        $result.activeCallsPrimaryOk -and
        $result.listConferencesOk -and
        $result.reconnectConferencePresent -and
        $result.reconnectDirectThreadPresent -and
        $result.reconnectActiveCallPresent -and
        $result.offlineDirectMessageDelivered -and
        $result.offlineCallInviteDelivered -and
        $result.offlineListActiveCallsContainsCall

    if ($isOk) {
        exit 0
    }
    exit 1
}
finally {
    Stop-ProcessSafe -Process $cliBOffline
    Stop-ProcessSafe -Process $cliBReconnect
    Stop-ProcessSafe -Process $cliB
    Stop-ProcessSafe -Process $cliA
    Stop-ProcessSafe -Process $server
}
