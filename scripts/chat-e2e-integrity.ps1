[CmdletBinding()]
param(
    [string]$ServerHost = "127.0.0.1",
    [string]$Port = "9002",
    [string]$ConferenceId = "",
    [string]$BuildDir = "",
    [string]$BackendWsUrl = "ws://127.0.0.1:5001/ws",
    [string]$BackendCommand = "",
    [int]$ServerStartupDelayMs = 2200,
    [int]$StepDelayMs = 650,
    [int]$DeliveryWaitMs = 2200,
    [int]$ExitTimeoutMs = 2200,
    [bool]$EnableMediasoupFlow = $true,
    [bool]$EnableDirectMediasoupTestMode = $true,
    [bool]$AllowMockMode = $false
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "chat-common.ps1")

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

function Convert-ToInt64OrZero {
    param(
        [Parameter(Mandatory = $false)]
        $Value
    )
    if ($null -eq $Value) {
        return [int64]0
    }
    try {
        return [int64]$Value
    } catch {
        return [int64]0
    }
}

function Wait-CliStep {
    param(
        [int]$DelayMs,
        [System.Diagnostics.Process]$CliA,
        [System.Diagnostics.Process]$CliB
    )

    Start-SleepWithCliDrain -DelayMs $DelayMs -Processes @($CliA, $CliB)
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

    $serialized = $Payload | ConvertTo-Json -Depth 12 -Compress
    Send-CliCommandChecked -Process $Process -Command ("send " + $serialized) -Label ("send " + $Payload.action)
}

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
if ([string]::IsNullOrWhiteSpace($ConferenceId)) {
    $ConferenceId = "conf-e2e-" + [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
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

if ([string]::IsNullOrWhiteSpace($BackendCommand)) {
    $BackendCommand = Join-Path $repoRoot "apps\mediasoup-server\run-backend.cmd"
}
if (!(Test-Path $BackendCommand)) {
    throw "Backend command not found: $BackendCommand"
}

$payloadA2B = "E2E_A2B::" + [Guid]::NewGuid().ToString("N") + "::SAME_PAYLOAD_CHECK"
$payloadB2A = "E2E_B2A::" + [Guid]::NewGuid().ToString("N") + "::SAME_PAYLOAD_CHECK"
$legacyAudioProbePayload = '{"type":"audio_data","sequence":1,"timestamp":0,"sampleRate":48000,"channels":1,"frameSize":960,"data":[1,2,3,4]}'
$userAId = "c4487900-a777-45ba-a813-5ddd453d9c4d"
$userBId = "f9a25664-c27d-475c-9a74-105975f77161"
$tokenA = "dev:$userAId|oz.chat.a@example.com"
$tokenB = "dev:$userBId|oz.chat.b@example.com"

$server = $null
$cliA = $null
$cliB = $null

try {
    if ($EnableMediasoupFlow -and -not $EnableDirectMediasoupTestMode) {
        throw "EnableDirectMediasoupTestMode must be true when EnableMediasoupFlow is enabled."
    }
    Write-Output "[e2e] step=server_start"
    $serverArgs = @(
        "--server",
        "--dev-autostart-mediasoup-backend",
        "--mediasoup-backend-url", $BackendWsUrl,
        "--mediasoup-backend-cmd", $BackendCommand,
        "--mediasoup-backend-ready-timeout-ms", "30000",
        "--mediasoup-backend-stop-timeout-ms", "3000"
    )
    if ($EnableDirectMediasoupTestMode) {
        $serverArgs += "--allow-direct-mediasoup"
    }
    $server = Start-Process -FilePath $artifacts.ServerExe -ArgumentList $serverArgs -PassThru -WindowStyle Hidden
    Start-Sleep -Milliseconds $ServerStartupDelayMs

    Write-Output "[e2e] step=cli_start"
    $cliA = Start-CliClient -CliExe $artifacts.CliExe -ServerHost $ServerHost -Port $Port
    $cliB = Start-CliClient -CliExe $artifacts.CliExe -ServerHost $ServerHost -Port $Port
    Wait-CliStep -DelayMs 1500 -CliA $cliA -CliB $cliB

    Write-Output "[e2e] step=auth_bind"
    Send-CliJsonCommand -Process $cliA -Payload @{
        object = "auth"
        agent = "session"
        action = "bind_session"
        ctx = @{
            accessToken = $tokenA
            deviceId = "e2e-device-a"
        }
    }
    Send-CliJsonCommand -Process $cliB -Payload @{
        object = "auth"
        agent = "session"
        action = "bind_session"
        ctx = @{
            accessToken = $tokenB
            deviceId = "e2e-device-b"
        }
    }
    Wait-CliStep -DelayMs ($StepDelayMs + 250) -CliA $cliA -CliB $cliB
    Write-Output "[e2e] step=legacy_audio_signaling_probe"
    Send-CliCommandChecked -Process $cliA -Command ("send " + $legacyAudioProbePayload) -Label "legacy audio_data probe"
    Wait-CliStep -DelayMs ($StepDelayMs + 200) -CliA $cliA -CliB $cliB

    Write-Output "[e2e] step=conference_setup conferenceId=$ConferenceId"
    Send-CliCommandChecked -Process $cliA -Command ("chat.create " + $ConferenceId) -Label "chat.create"
    Wait-CliStep -DelayMs $StepDelayMs -CliA $cliA -CliB $cliB
    Send-CliCommandChecked -Process $cliB -Command ("chat.join " + $ConferenceId) -Label "chat.join"
    Wait-CliStep -DelayMs ($StepDelayMs + 300) -CliA $cliA -CliB $cliB

    Write-Output "[e2e] step=send_payloads"
    Send-CliCommandChecked -Process $cliA -Command ("chat.send " + $ConferenceId + " " + $payloadA2B) -Label "chat.send A"
    Wait-CliStep -DelayMs ($StepDelayMs + 400) -CliA $cliA -CliB $cliB
    Send-CliCommandChecked -Process $cliB -Command ("chat.send " + $ConferenceId + " " + $payloadB2A) -Label "chat.send B"
    Wait-CliStep -DelayMs $DeliveryWaitMs -CliA $cliA -CliB $cliB
    $mediaRoomId = ""
    $transportA = ""
    $transportB = ""
    $producerA = ""
    $producerB = ""
    if ($EnableMediasoupFlow) {
        $mediaRoomId = "media-" + $ConferenceId
        $transportA = "transport-a-" + [Guid]::NewGuid().ToString("N").Substring(0, 10)
        $transportB = "transport-b-" + [Guid]::NewGuid().ToString("N").Substring(0, 10)
        $producerA = "producer-a-" + [Guid]::NewGuid().ToString("N").Substring(0, 10)
        $producerB = "producer-b-" + [Guid]::NewGuid().ToString("N").Substring(0, 10)
        $defaultOffer = "v=0`r`no=- 0 0 IN IP4 127.0.0.1`r`ns=e2e`r`nt=0 0`r`na=group:BUNDLE 0`r`nm=application 9 UDP/DTLS/SCTP webrtc-datachannel`r`nc=IN IP4 0.0.0.0`r`na=mid:0`r`na=sctp-port:5000`r`n"
        $fingerprintBytes = New-Object byte[] 32
        $rng = [System.Security.Cryptography.RandomNumberGenerator]::Create()
        try {
            $rng.GetBytes($fingerprintBytes)
        } finally {
            if ($null -ne $rng) {
                $rng.Dispose()
            }
        }
        $fingerprint = ($fingerprintBytes | ForEach-Object { $_.ToString("X2") }) -join ":"
        $dtlsParameters = @{
            role = "auto"
            fingerprints = @(
                @{
                    algorithm = "sha-256"
                    value = $fingerprint
                }
            )
        }
        $audioCodecParameters = @{
            mimeType = "audio/opus"
            payloadType = 100
            clockRate = 48000
            channels = 2
            parameters = @{}
            rtcpFeedback = @()
        }
        $rtpCapabilities = @{
            codecs = @(
                @{
                    kind = "audio"
                    mimeType = "audio/opus"
                    preferredPayloadType = 100
                    clockRate = 48000
                    channels = 2
                    parameters = @{}
                    rtcpFeedback = @()
                }
            )
            headerExtensions = @()
        }
        $rtpParametersA = @{
            codecs = @($audioCodecParameters)
            headerExtensions = @()
            encodings = @(@{ ssrc = (Get-Random -Minimum 1000000 -Maximum 2000000000) })
            rtcp = @{ cname = "e2e-a"; reducedSize = $true; mux = $true }
        }
        $rtpParametersB = @{
            codecs = @($audioCodecParameters)
            headerExtensions = @()
            encodings = @(@{ ssrc = (Get-Random -Minimum 1000000 -Maximum 2000000000) })
            rtcp = @{ cname = "e2e-b"; reducedSize = $true; mux = $true }
        }

        Write-Output "[e2e] step=mediasoup_setup roomId=$mediaRoomId"
        Send-CliJsonCommand -Process $cliA -Payload @{
            object = "mediasoup"
            agent = "signaling"
            action = "create_room"
            ctx = @{ roomId = $mediaRoomId }
        }
        Wait-CliStep -DelayMs $StepDelayMs -CliA $cliA -CliB $cliB

        Send-CliJsonCommand -Process $cliA -Payload @{
            object = "mediasoup"
            agent = "signaling"
            action = "join_room"
            ctx = @{ roomId = $mediaRoomId }
        }
        Wait-CliStep -DelayMs $StepDelayMs -CliA $cliA -CliB $cliB

        Send-CliJsonCommand -Process $cliB -Payload @{
            object = "mediasoup"
            agent = "signaling"
            action = "join_room"
            ctx = @{ roomId = $mediaRoomId }
        }
        Wait-CliStep -DelayMs $StepDelayMs -CliA $cliA -CliB $cliB

        Send-CliJsonCommand -Process $cliA -Payload @{
            object = "mediasoup"
            agent = "signaling"
            action = "open_transport"
            ctx = @{ roomId = $mediaRoomId; transportId = $transportA }
        }
        Wait-CliStep -DelayMs $StepDelayMs -CliA $cliA -CliB $cliB

        Send-CliJsonCommand -Process $cliB -Payload @{
            object = "mediasoup"
            agent = "signaling"
            action = "open_transport"
            ctx = @{ roomId = $mediaRoomId; transportId = $transportB }
        }
        Wait-CliStep -DelayMs $StepDelayMs -CliA $cliA -CliB $cliB

        Send-CliJsonCommand -Process $cliA -Payload @{
            object = "mediasoup"
            agent = "signaling"
            action = "produce"
            ctx = @{ roomId = $mediaRoomId; transportId = $transportA; producerId = $producerA; kind = "audio"; rtpParameters = $rtpParametersA }
        }
        Wait-CliStep -DelayMs $StepDelayMs -CliA $cliA -CliB $cliB

        Send-CliJsonCommand -Process $cliB -Payload @{
            object = "mediasoup"
            agent = "signaling"
            action = "consume"
            ctx = @{
                roomId = $mediaRoomId
                transportId = $transportB
                producerId = $producerA
                rtpCapabilities = $rtpCapabilities
                injectTestRtp = $true
                testRtp = @{
                    packetCount = 18
                    payloadSize = 32
                    timestampStep = 960
                }
            }
        }
        Wait-CliStep -DelayMs $StepDelayMs -CliA $cliA -CliB $cliB

        Send-CliJsonCommand -Process $cliB -Payload @{
            object = "mediasoup"
            agent = "signaling"
            action = "produce"
            ctx = @{ roomId = $mediaRoomId; transportId = $transportB; producerId = $producerB; kind = "audio"; rtpParameters = $rtpParametersB }
        }
        Wait-CliStep -DelayMs $StepDelayMs -CliA $cliA -CliB $cliB

        Send-CliJsonCommand -Process $cliA -Payload @{
            object = "mediasoup"
            agent = "signaling"
            action = "consume"
            ctx = @{
                roomId = $mediaRoomId
                transportId = $transportA
                producerId = $producerB
                rtpCapabilities = $rtpCapabilities
                injectTestRtp = $true
                testRtp = @{
                    packetCount = 18
                    payloadSize = 32
                    timestampStep = 960
                }
            }
        }
        Wait-CliStep -DelayMs $StepDelayMs -CliA $cliA -CliB $cliB

        Send-CliJsonCommand -Process $cliA -Payload @{
            object = "mediasoup"
            agent = "signaling"
            action = "webrtc_offer"
            ctx = @{ roomId = $mediaRoomId; transportId = $transportA; sdp = $defaultOffer; dtlsParameters = $dtlsParameters }
        }
        Wait-CliStep -DelayMs $StepDelayMs -CliA $cliA -CliB $cliB

        Send-CliJsonCommand -Process $cliA -Payload @{
            object = "mediasoup"
            agent = "signaling"
            action = "webrtc_ice"
            ctx = @{ roomId = $mediaRoomId; transportId = $transportA; sdpMid = "0"; candidate = "candidate:0 1 UDP 2122252543 127.0.0.1 5000 typ host" }
        }
        Wait-CliStep -DelayMs $StepDelayMs -CliA $cliA -CliB $cliB

        Send-CliJsonCommand -Process $cliA -Payload @{
            object = "mediasoup"
            agent = "signaling"
            action = "stats"
            ctx = @{ roomId = $mediaRoomId }
        }
        Wait-CliStep -DelayMs ($StepDelayMs + 300) -CliA $cliA -CliB $cliB
    }

    Write-Output "[e2e] step=collect_output"
    Send-CliCommandChecked -Process $cliA -Command "quit" -Label "quit A"
    Send-CliCommandChecked -Process $cliB -Command "quit" -Label "quit B"
    Wait-CliStep -DelayMs 300 -CliA $cliA -CliB $cliB

    $cliAExited = Wait-CliExit -Process $cliA -TimeoutMs $ExitTimeoutMs
    $cliBExited = Wait-CliExit -Process $cliB -TimeoutMs $ExitTimeoutMs
    $outA = Read-CliOutput -Process $cliA
    $outB = Read-CliOutput -Process $cliB

    $eventsA = Parse-CliIncomingJson -OutputText $outA
    $eventsB = Parse-CliIncomingJson -OutputText $outB
    $authA = @($eventsA | Where-Object {
            $_.type -eq "dispatch_result" -and
            $_.object -eq "auth" -and
            $_.action -eq "bind_session" -and
            $_.ok -eq $true
        })
    $authB = @($eventsB | Where-Object {
            $_.type -eq "dispatch_result" -and
            $_.object -eq "auth" -and
            $_.action -eq "bind_session" -and
            $_.ok -eq $true
        })

    $chatA = @($eventsA | Where-Object { $_.type -eq "chat_message" })
    $chatB = @($eventsB | Where-Object { $_.type -eq "chat_message" })
    $ackA = @($eventsA | Where-Object {
            $_.type -eq "dispatch_result" -and
            $_.object -eq "chat" -and
            $_.action -eq "send_message" -and
            $_.ok -eq $true
        })
    $ackB = @($eventsB | Where-Object {
            $_.type -eq "dispatch_result" -and
            $_.object -eq "chat" -and
            $_.action -eq "send_message" -and
            $_.ok -eq $true
        })

    $receivedA2BOnB = @($chatB | Where-Object { $_.text -eq $payloadA2B })
    $receivedB2AOnA = @($chatA | Where-Object { $_.text -eq $payloadB2A })

    $mediaDispatchA = @($eventsA | Where-Object { $_.type -eq "dispatch_result" -and $_.object -eq "mediasoup" })
    $mediaDispatchB = @($eventsB | Where-Object { $_.type -eq "dispatch_result" -and $_.object -eq "mediasoup" })
    $mediaDispatch = @($mediaDispatchA + $mediaDispatchB)
    $mediaErrors = @($mediaDispatch | Where-Object { $_.ok -ne $true })
    $mediaConfigErrors = @($mediaErrors | Where-Object {
            ([string]$_.message).ToLower().Contains("backend endpoint is not configured")
        })
    $mediaTransportIdErrors = @($mediaErrors | Where-Object {
            ([string]$_.message) -match "transportId|Transport not found"
        })
    $mediaMockHints = @($mediaDispatch | Where-Object {
            $message = [string]$_.message
            ($message -match 'mock mode|"mocked":true') -or
            (($_.action -ne "webrtc_ice") -and ($message -match 'mode":"noop|mode": "noop'))
        })
    $mediaStatsDispatchA = @($mediaDispatchA | Where-Object { $_.action -eq "stats" -and $_.ok -eq $true })
    $mediaStatsResponse = $null
    if ($mediaStatsDispatchA.Count -ge 1) {
        $mediaStatsEvent = $mediaStatsDispatchA[-1]
        if ($null -ne $mediaStatsEvent.data) {
            $mediaStatsResponse = $mediaStatsEvent.data
        } else {
            $mediaStatsMessage = $mediaStatsEvent.message
            if ($mediaStatsMessage -is [string]) {
                if (-not [string]::IsNullOrWhiteSpace($mediaStatsMessage)) {
                    try {
                        $mediaStatsResponse = $mediaStatsMessage | ConvertFrom-Json -ErrorAction Stop
                    } catch {
                        $mediaStatsResponse = $null
                    }
                }
            } elseif ($null -ne $mediaStatsMessage) {
                $mediaStatsResponse = $mediaStatsMessage
            }
        }
    }
    $mediaStatsProducerPackets = [int64]0
    $mediaStatsConsumerPackets = [int64]0
    $mediaStatsProducerBytes = [int64]0
    $mediaStatsConsumerBytes = [int64]0
    if ($null -ne $mediaStatsResponse) {
        $mediaStatsSource = $mediaStatsResponse
        if ($null -ne $mediaStatsResponse.data) {
            $mediaStatsSource = $mediaStatsResponse.data
        }
        $mediaStatsProducerPackets = Convert-ToInt64OrZero -Value $mediaStatsSource.totalProducerPackets
        $mediaStatsConsumerPackets = Convert-ToInt64OrZero -Value $mediaStatsSource.totalConsumerPackets
        $mediaStatsProducerBytes = Convert-ToInt64OrZero -Value $mediaStatsSource.totalProducerBytes
        $mediaStatsConsumerBytes = Convert-ToInt64OrZero -Value $mediaStatsSource.totalConsumerBytes
    }
    $audioSignalingRejectEvents = @($eventsA | Where-Object {
            $_.type -eq "dispatch_result" -and
            $_.ok -eq $false -and
            ([string]$_.message).ToLower().Contains("audio_data over signaling websocket is forbidden")
        })
    $audioSignalingRejected = ($audioSignalingRejectEvents.Count -ge 1)

    $mediaCreateRoomOk = $true
    $mediaJoinAOk = $true
    $mediaJoinBOk = $true
    $mediaTransportAOk = $true
    $mediaTransportBOk = $true
    $mediaProduceAOk = $true
    $mediaProduceBOk = $true
    $mediaConsumeAOk = $true
    $mediaConsumeBOk = $true
    $mediaOfferOk = $true
    $mediaIceOk = $true
    $mediaStatsOk = $true
    if ($EnableMediasoupFlow) {
        $mediaCreateRoomOk = (@($mediaDispatchA | Where-Object { $_.action -eq "create_room" -and $_.ok -eq $true }).Count -ge 1)
        $mediaJoinAOk = (@($mediaDispatchA | Where-Object { $_.action -eq "join_room" -and $_.ok -eq $true }).Count -ge 1)
        $mediaJoinBOk = (@($mediaDispatchB | Where-Object { $_.action -eq "join_room" -and $_.ok -eq $true }).Count -ge 1)
        $mediaTransportAOk = (@($mediaDispatchA | Where-Object { $_.action -eq "open_transport" -and $_.ok -eq $true }).Count -ge 1)
        $mediaTransportBOk = (@($mediaDispatchB | Where-Object { $_.action -eq "open_transport" -and $_.ok -eq $true }).Count -ge 1)
        $mediaProduceAOk = (@($mediaDispatchA | Where-Object { $_.action -eq "produce" -and $_.ok -eq $true }).Count -ge 1)
        $mediaProduceBOk = (@($mediaDispatchB | Where-Object { $_.action -eq "produce" -and $_.ok -eq $true }).Count -ge 1)
        $mediaConsumeAOk = (@($mediaDispatchA | Where-Object { $_.action -eq "consume" -and $_.ok -eq $true }).Count -ge 1)
        $mediaConsumeBOk = (@($mediaDispatchB | Where-Object { $_.action -eq "consume" -and $_.ok -eq $true }).Count -ge 1)
        $mediaOfferOk = (@($mediaDispatchA | Where-Object { $_.action -eq "webrtc_offer" -and $_.ok -eq $true }).Count -ge 1)
        $mediaIceOk = (@($mediaDispatchA | Where-Object { $_.action -eq "webrtc_ice" -and $_.ok -eq $true }).Count -ge 1)
        $mediaStatsOk =
            ($mediaStatsDispatchA.Count -ge 1) -and
            ($mediaStatsProducerPackets -gt 0) -and
            ($mediaStatsConsumerPackets -gt 0) -and
            ($mediaStatsProducerBytes -gt 0) -and
            ($mediaStatsConsumerBytes -gt 0)
    }

    $isMediaOk =
        (-not $EnableMediasoupFlow) -or (
            $mediaCreateRoomOk -and
            $mediaJoinAOk -and
            $mediaJoinBOk -and
            $mediaTransportAOk -and
            $mediaTransportBOk -and
            $mediaProduceAOk -and
            $mediaProduceBOk -and
            $mediaConsumeAOk -and
            $mediaConsumeBOk -and
            $mediaOfferOk -and
            $mediaIceOk -and
            $mediaStatsOk -and
            ($mediaConfigErrors.Count -eq 0) -and
            ($mediaTransportIdErrors.Count -eq 0) -and
            ($AllowMockMode -or ($mediaMockHints.Count -eq 0))
        )

    $result = [ordered]@{
        conferenceId = $ConferenceId
        backendWsUrl = $BackendWsUrl
        mediasoupFlowEnabled = $EnableMediasoupFlow
        directMediasoupModeEnabled = $EnableDirectMediasoupTestMode
        allowMockMode = $AllowMockMode
        mediasoupRoomId = $mediaRoomId
        mediasoupTransportA = $transportA
        mediasoupTransportB = $transportB
        mediasoupProducerA = $producerA
        mediasoupProducerB = $producerB
        payloadA2B = $payloadA2B
        payloadB2A = $payloadB2A
        cliAExited = $cliAExited
        cliBExited = $cliBExited
        hasAbort = ((Test-HasAbort -Text $outA) -or (Test-HasAbort -Text $outB))
        authAOk = ($authA.Count -ge 1)
        authBOk = ($authB.Count -ge 1)
        ackSendMessageA = ($ackA.Count -ge 1)
        ackSendMessageB = ($ackB.Count -ge 1)
        exactA2BDelivered = ($receivedA2BOnB.Count -ge 1)
        exactB2ADelivered = ($receivedB2AOnA.Count -ge 1)
        duplicateA2BCountOnB = $receivedA2BOnB.Count
        duplicateB2ACountOnA = $receivedB2AOnA.Count
        rawChatMessagesSeenOnA = $chatA.Count
        rawChatMessagesSeenOnB = $chatB.Count
        mediasoupDispatchSeenOnA = $mediaDispatchA.Count
        mediasoupDispatchSeenOnB = $mediaDispatchB.Count
        mediasoupCreateRoomOk = $mediaCreateRoomOk
        mediasoupJoinAOk = $mediaJoinAOk
        mediasoupJoinBOk = $mediaJoinBOk
        mediasoupOpenTransportAOk = $mediaTransportAOk
        mediasoupOpenTransportBOk = $mediaTransportBOk
        mediasoupProduceAOk = $mediaProduceAOk
        mediasoupProduceBOk = $mediaProduceBOk
        mediasoupConsumeAOk = $mediaConsumeAOk
        mediasoupConsumeBOk = $mediaConsumeBOk
        mediasoupOfferOk = $mediaOfferOk
        mediasoupIceOk = $mediaIceOk
        mediasoupStatsOk = $mediaStatsOk
        mediasoupStatsDispatchCount = $mediaStatsDispatchA.Count
        mediasoupStatsProducerPackets = $mediaStatsProducerPackets
        mediasoupStatsConsumerPackets = $mediaStatsConsumerPackets
        mediasoupStatsProducerBytes = $mediaStatsProducerBytes
        mediasoupStatsConsumerBytes = $mediaStatsConsumerBytes
        mediasoupErrorCount = $mediaErrors.Count
        mediasoupConfigErrorCount = $mediaConfigErrors.Count
        mediasoupTransportIdErrorCount = $mediaTransportIdErrors.Count
        mediasoupMockHintCount = $mediaMockHints.Count
        signalingAudioDataRejected = $audioSignalingRejected
        signalingAudioDataRejectCount = $audioSignalingRejectEvents.Count
        mediasoupFlowOk = $isMediaOk
        outputTailA = (($outA -split "(`r`n|`n|`r)") | Select-Object -Last 20)
        outputTailB = (($outB -split "(`r`n|`n|`r)") | Select-Object -Last 20)
    }

    $result | ConvertTo-Json -Depth 6

    $isOk =
        (-not $result.hasAbort) -and
        $result.authAOk -and
        $result.authBOk -and
        $result.ackSendMessageA -and
        $result.ackSendMessageB -and
        $result.exactA2BDelivered -and
        $result.exactB2ADelivered -and
        ($result.duplicateA2BCountOnB -eq 1) -and
        ($result.duplicateB2ACountOnA -eq 1) -and
        $result.signalingAudioDataRejected -and
        $result.mediasoupFlowOk

    if ($isOk) {
        exit 0
    }
    exit 1
}
finally {
    Stop-ProcessSafe -Process $cliA
    Stop-ProcessSafe -Process $cliB
    Stop-ProcessSafe -Process $server
}
