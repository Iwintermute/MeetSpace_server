#include "features/direct_call/runtime/DirectCallFeatureModule.h"

#include "Auth/runtime/AuthServices.h"
#include "Bridge/Mediasoup/service/MediaTransportTypes.h"
#include "Bridge/Mediasoup/service/SharedMediaTransportService.h"
#include "infrastructure/control_plane/runtime/ControlPlaneServices.h"

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace eds::server_new::features::direct_call {
    namespace {

        using eds::server_new::mediasoup::service::IMediaTransportService;
        using eds::server_new::mediasoup::service::MediaTransportCommand;
        using eds::server_new::mediasoup::service::MediaTransportEvent;
        using eds::server_new::mediasoup::service::MediaTransportEventType;
        using eds::server_new::mediasoup::service::MediaTransportIntent;

        void appendOutboundEventsFromStatus(
            const core::contracts::OperationStatus& status,
            eds::server_new::features::runtime::FeatureDispatchResult& result) {
            const auto it = status.data.find("outboundEvents");
            if (it == status.data.end() || !it->is_array()) {
                return;
            }

            for (const auto& item : *it) {
                if (item.is_object()) {
                    result.outboundEvents.push_back(item);
                }
            }
        }

        void executeMediaIntentIgnoreFailure(
            const std::shared_ptr<IMediaTransportService>& service,
            MediaTransportIntent intent,
            const MediaTransportCommand& command) {
            if (!service) {
                return;
            }

            std::vector<MediaTransportEvent> events;
            static_cast<void>(service->execute(intent, command, events));
        }

        std::string extractJsonPayloadField(const nlohmann::json& context, std::string_view fieldName) {
            const auto iterator = context.find(std::string(fieldName));
            if (iterator == context.end() || iterator->is_null()) {
                return {};
            }
            if (iterator->is_string()) {
                return iterator->get<std::string>();
            }
            if (iterator->is_object() || iterator->is_array()) {
                return iterator->dump();
            }
            return {};
        }

        core::contracts::OperationStatus resolveMediaIntent(
            std::string_view actionType,
            MediaTransportIntent& intent) {
            if (actionType == kActionOpenTransport) {
                intent = MediaTransportIntent::OpenTransport;
                return core::contracts::OperationStatus::success();
            }
            if (actionType == kActionPublishTrack) {
                intent = MediaTransportIntent::PublishTrack;
                return core::contracts::OperationStatus::success();
            }
            if (actionType == kActionPauseTrack) {
                intent = MediaTransportIntent::PauseTrack;
                return core::contracts::OperationStatus::success();
            }
            if (actionType == kActionResumeTrack) {
                intent = MediaTransportIntent::ResumeTrack;
                return core::contracts::OperationStatus::success();
            }
            if (actionType == kActionCloseTrack) {
                intent = MediaTransportIntent::CloseTrack;
                return core::contracts::OperationStatus::success();
            }
            if (actionType == kActionConsumeTrack) {
                intent = MediaTransportIntent::ConsumeTrack;
                return core::contracts::OperationStatus::success();
            }
            if (actionType == kActionConsumerReady) {
                intent = MediaTransportIntent::ResumeConsumer;
                return core::contracts::OperationStatus::success();
            }
            if (actionType == kActionWebrtcOffer) {
                intent = MediaTransportIntent::ApplyOffer;
                return core::contracts::OperationStatus::success();
            }
            if (actionType == kActionWebrtcIce) {
                intent = MediaTransportIntent::ApplyIce;
                return core::contracts::OperationStatus::success();
            }
            if (actionType == kActionWebrtcClose) {
                intent = MediaTransportIntent::CloseSession;
                return core::contracts::OperationStatus::success();
            }
            if (actionType == kActionMediaStats) {
                intent = MediaTransportIntent::ReadStats;
                return core::contracts::OperationStatus::success();
            }

            return core::contracts::OperationStatus::failure("Unsupported direct call action.");
        }

        std::string_view resolveDirectCallFileEventType(std::string_view actionType) {
            if (actionType == kActionFileOffer) {
                return kEventDirectCallFileOffer;
            }
            if (actionType == kActionFileAccept) {
                return kEventDirectCallFileAccept;
            }
            if (actionType == kActionFileChunk) {
                return kEventDirectCallFileChunk;
            }
            if (actionType == kActionFileComplete) {
                return kEventDirectCallFileComplete;
            }
            if (actionType == kActionFileCancel) {
                return kEventDirectCallFileCancel;
            }

            return {};
        }

        MediaTransportCommand buildMediaCommand(
            const eds::server_new::features::runtime::FeatureDispatchRequest& request,
            std::string roomId) {
            MediaTransportCommand command;
            command.sessionHandle = request.sessionHandle;
            command.sessionId = request.peerId;
            command.peerId = request.peerId;
            command.roomId = std::move(roomId);
            command.transportId = request.context.value("transportId", std::string{});
            command.producerId = request.context.value("producerId", std::string{});
            command.consumerId = request.context.value("consumerId", std::string{});
            command.kind = request.context.value("kind", std::string{});
            command.trackType = request.context.value("trackType", std::string{});
            command.sdp = request.context.value("sdp", std::string{});
            command.sdpMid = request.context.value("sdpMid", std::string{});
            command.candidate = extractJsonPayloadField(request.context, "candidate");
            command.dtlsParameters = extractJsonPayloadField(request.context, "dtlsParameters");
            command.rtpParameters = extractJsonPayloadField(request.context, "rtpParameters");
            command.rtpCapabilities = extractJsonPayloadField(request.context, "rtpCapabilities");
            command.injectTestRtp = request.context.value("injectTestRtp", false);
            const auto testRtpIt = request.context.find("testRtp");
            if (testRtpIt != request.context.end() && testRtpIt->is_object()) {
                command.testRtpPacketCount = testRtpIt->value("packetCount", 0);
                command.testRtpPayloadSize = testRtpIt->value("payloadSize", 0);
                command.testRtpTimestampStep = testRtpIt->value("timestampStep", 0);
            }
            command.correlationId = request.context.value(
                "correlationId",
                request.context.value("clientRequestId", request.context.value("messageId", std::string{})));
            return command;
        }

        nlohmann::json parseMediaBackendData(std::string_view message, std::string& resolvedMessage) {
            resolvedMessage.clear();
            const auto parsed = nlohmann::json::parse(std::string(message), nullptr, false);
            if (!parsed.is_object()) {
                if (!message.empty()) {
                    resolvedMessage = std::string(message);
                }
                return nlohmann::json::object();
            }

            resolvedMessage = parsed.value("message", std::string{});
            nlohmann::json responseData = nlohmann::json::object();
            const auto dataIt = parsed.find("data");
            if (dataIt != parsed.end() && dataIt->is_object()) {
                responseData = *dataIt;
            }

            const auto backendIt = parsed.find("backend");
            if (backendIt != parsed.end() && backendIt->is_object()) {
                responseData["backend"] = *backendIt;

                const auto backendCapsIt = backendIt->find("routerRtpCapabilities");
                if (backendCapsIt != backendIt->end() && backendCapsIt->is_object()) {
                    responseData["routerRtpCapabilities"] = *backendCapsIt;
                }
            }

            const auto topLevelCapsIt = parsed.find("routerRtpCapabilities");
            if (!responseData.contains("routerRtpCapabilities")
                && topLevelCapsIt != parsed.end()
                && topLevelCapsIt->is_object()) {
                responseData["routerRtpCapabilities"] = *topLevelCapsIt;
            }

            return responseData;
        }

        bool shouldRetryOpenTransportRecovery(const core::contracts::OperationStatus& status) {
            return !status.ok
                && (status.message.find("Peer must join room before opening transport.") != std::string::npos
                    || status.message.find("Room not found:") != std::string::npos);
        }

        bool shouldRecreateRoomBeforeRetry(const core::contracts::OperationStatus& status) {
            return !status.ok && status.message.find("Room not found:") != std::string::npos;
        }

        MediaTransportCommand buildCreateRoomCommand(
            const eds::server_new::features::runtime::FeatureDispatchRequest& request,
            std::string_view roomId) {
            MediaTransportCommand command;
            command.sessionHandle = request.sessionHandle;
            command.sessionId = request.peerId;
            command.peerId = request.peerId;
            command.roomId = std::string(roomId);
            command.correlationId = request.context.value(
                "correlationId",
                request.context.value("clientRequestId", std::string("direct_call_open_transport_create_room")));
            return command;
        }

        MediaTransportCommand buildJoinSessionCommand(
            const eds::server_new::features::runtime::FeatureDispatchRequest& request,
            std::string_view roomId) {
            MediaTransportCommand command;
            command.sessionHandle = request.sessionHandle;
            command.sessionId = request.peerId;
            command.peerId = request.peerId;
            command.roomId = std::string(roomId);
            command.correlationId = request.context.value(
                "correlationId",
                request.context.value("clientRequestId", std::string("direct_call_open_transport_rejoin")));
            return command;
        }

        void appendOutboundTransportEvents(
            const std::vector<MediaTransportEvent>& events,
            std::string_view objectType,
            const nlohmann::json& extraContext,
            std::vector<nlohmann::json>& outboundEvents) {
            for (const auto& event : events) {
                if (event.notifyPeerIds.empty()) {
                    continue;
                }

                nlohmann::json payload{
                    { "type", std::string(eds::server_new::mediasoup::service::toString(event.type)) },
                    { "object", std::string(objectType) },
                    { "roomId", event.roomId },
                    { "peerId", event.peerId },
                    { "memberPeerIds", event.memberPeerIds },
                    { "reason", event.reason }
                };

                if (extraContext.is_object()) {
                    for (auto it = extraContext.begin(); it != extraContext.end(); ++it) {
                        payload[it.key()] = it.value();
                    }
                }

                switch (event.type) {
                case MediaTransportEventType::RoomState:
                    break;
                case MediaTransportEventType::PeerJoined:
                case MediaTransportEventType::PeerLeft:
                    break;
                case MediaTransportEventType::TransportOpened:
                    payload["transportId"] = event.transportId;
                    break;
                case MediaTransportEventType::TrackPublished:
                    payload["producerId"] = event.producerId;
                    payload["producerPeerId"] = event.peerId;
                    payload["kind"] = event.kind;
                    payload["trackType"] = event.trackType;
                    break;
                case MediaTransportEventType::TrackClosed:
                    payload["producerId"] = event.producerId;
                    payload["kind"] = event.kind;
                    payload["trackType"] = event.trackType;
                    break;
                case MediaTransportEventType::ConsumerResumed:
                    payload["consumerId"] = event.consumerId;
                    payload["producerId"] = event.producerId;
                    payload["producerPeerId"] = event.producerPeerId;
                    payload["kind"] = event.kind;
                    payload["trackType"] = event.trackType;
                    payload["paused"] = event.paused;
                    break;
                case MediaTransportEventType::SessionStarted:
                    payload["started"] = event.started;
                    break;
                case MediaTransportEventType::SessionEnded:
                    payload["ended"] = event.ended;
                    break;
                case MediaTransportEventType::SessionClosed:
                    break;
                case MediaTransportEventType::TransportError:
                    break;
                }

                if (event.notifyPeerIds.size() == 1) {
                    payload["deliverTo"] = event.notifyPeerIds.front();
                }
                else {
                    payload["deliverTo"] = event.notifyPeerIds;
                }

                outboundEvents.push_back(std::move(payload));
            }
        }

    } // namespace

    DirectCallFeatureModule::DirectCallFeatureModule()
        : BaseModule("DirectCallFeatureModule", static_cast<core::contracts::ModuleId>(-1)) {
    }

    std::string_view DirectCallFeatureModule::objectType() const {
        return kDirectCallRouteObject;
    }

    std::string_view DirectCallFeatureModule::defaultAgent() const {
        return kDirectCallLifecycleAgent;
    }

    core::contracts::OperationStatus DirectCallFeatureModule::ensureRegistered(core::runtime::MessageDispatcher& dispatcher) {
        static_cast<void>(dispatcher);

        if (!transportService_) {
            transportService_ = eds::server_new::mediasoup::service::sharedMediaTransportService(false);
        }

        registered_ = true;
        return core::contracts::OperationStatus::success();
    }

    eds::server_new::features::runtime::FeatureDispatchResult DirectCallFeatureModule::dispatch(
        const eds::server_new::features::runtime::FeatureDispatchRequest& request,
        core::runtime::MessageDispatcher& dispatcher) {
        eds::server_new::features::runtime::FeatureDispatchResult result;
        result.status = ensureRegistered(dispatcher);
        if (!result.status.ok) {
            return result;
        }

        auto sessionStore = eds::server_new::auth::AuthServices::sessionStore();
        auto repository = eds::server_new::control_plane::ControlPlaneServices::repository();

        if (!sessionStore || !repository || !repository->isReady()) {
            result.status = core::contracts::OperationStatus::failure("Direct call control-plane is not configured.");
            return result;
        }

        const auto authSession = sessionStore->get(request.sessionHandle);
        if (!authSession.has_value() || !authSession->authenticated) {
            result.status = core::contracts::OperationStatus::failure("Unauthorized direct call request.");
            return result;
        }

        const auto peerId = request.context.value("peerId", request.context.value("peer", request.peerId));
        if (peerId != request.peerId) {
            result.status = core::contracts::OperationStatus::failure("peer impersonation detected.");
            return result;
        }

        if (request.actionType == kActionCreateDirectCall) {
            const auto targetUserId = request.context.value("targetUserId", request.context.value("userIdTo", std::string{}));
            const auto clientRequestId = request.context.value("clientRequestId", request.context.value("messageId", std::string{}));

            result.status = repository->createDirectCall(
                authSession->userId,
                request.peerId,
                targetUserId,
                clientRequestId);

            appendOutboundEventsFromStatus(result.status, result);

            if (result.status.ok && transportService_) {
                const auto roomId = result.status.data.value("roomId", std::string{});
                if (!roomId.empty()) {
                    MediaTransportCommand mediaCommand;
                    mediaCommand.sessionHandle = request.sessionHandle;
                    mediaCommand.sessionId = request.peerId;
                    mediaCommand.peerId = request.peerId;
                    mediaCommand.roomId = roomId;
                    mediaCommand.correlationId =
                        request.context.value("clientRequestId", std::string("direct_call_create"));

                    executeMediaIntentIgnoreFailure(transportService_, MediaTransportIntent::CreateRoom, mediaCommand);
                    executeMediaIntentIgnoreFailure(transportService_, MediaTransportIntent::JoinSession, mediaCommand);
                }
            }

            return result;
        }

        if (request.actionType == kActionAcceptDirectCall) {
            const auto callId = request.context.value("callId", std::string{});
            result.status = repository->acceptDirectCall(authSession->userId, request.peerId, callId);
            appendOutboundEventsFromStatus(result.status, result);

            if (result.status.ok && transportService_) {
                const auto roomId = result.status.data.value("roomId", std::string{});
                if (!roomId.empty()) {
                    MediaTransportCommand mediaCommand;
                    mediaCommand.sessionHandle = request.sessionHandle;
                    mediaCommand.sessionId = request.peerId;
                    mediaCommand.peerId = request.peerId;
                    mediaCommand.roomId = roomId;
                    mediaCommand.correlationId =
                        request.context.value("clientRequestId", std::string("direct_call_accept"));

                    executeMediaIntentIgnoreFailure(transportService_, MediaTransportIntent::JoinSession, mediaCommand);
                }
            }

            return result;
        }

        if (request.actionType == kActionDeclineDirectCall) {
            const auto callId = request.context.value("callId", std::string{});
            result.status = repository->declineDirectCall(authSession->userId, request.peerId, callId);
            appendOutboundEventsFromStatus(result.status, result);
            return result;
        }

        if (request.actionType == kActionHangupDirectCall) {
            const auto callId = request.context.value("callId", std::string{});
            result.status = repository->hangupDirectCall(authSession->userId, request.peerId, callId);
            appendOutboundEventsFromStatus(result.status, result);

            if (result.status.ok && transportService_) {
                const auto roomId = result.status.data.value("roomId", std::string{});
                const auto participantPeerIds =
                    result.status.data.value("participantPeerIds", nlohmann::json::array());

                if (!roomId.empty() && participantPeerIds.is_array()) {
                    for (const auto& item : participantPeerIds) {
                        if (!item.is_string()) {
                            continue;
                        }

                        MediaTransportCommand mediaCommand;
                        mediaCommand.sessionHandle = request.sessionHandle;
                        mediaCommand.sessionId = item.get<std::string>();
                        mediaCommand.peerId = item.get<std::string>();
                        mediaCommand.roomId = roomId;
                        mediaCommand.correlationId =
                            request.context.value("clientRequestId", std::string("direct_call_hangup"));

                        executeMediaIntentIgnoreFailure(
                            transportService_,
                            MediaTransportIntent::CloseSession,
                            mediaCommand);
                    }
                }
            }

            return result;
        }
        if (request.actionType == kActionListActiveCalls) {
            const auto limit = request.context.value("limit", static_cast<std::size_t>(100));
            result.status = repository->listUserActiveDirectCalls(authSession->userId, limit);
            return result;
        }

        const auto fileEventType = resolveDirectCallFileEventType(request.actionType);
        if (!fileEventType.empty()) {
            const auto callId = request.context.value("callId", std::string{});
            if (callId.empty()) {
                result.status = core::contracts::OperationStatus::failure(
                    "callId must not be empty for direct call file actions.");
                return result;
            }

            const auto targetPeerId = request.context.value(
                "targetPeerId",
                request.context.value("peerIdTo", std::string{}));
            result.status = repository->relayDirectCallFileEvent(
                authSession->userId,
                request.peerId,
                callId,
                fileEventType,
                request.context,
                targetPeerId);
            appendOutboundEventsFromStatus(result.status, result);
            return result;
        }

        MediaTransportIntent mediaIntent = MediaTransportIntent::ReadStats;
        auto intentStatus = resolveMediaIntent(request.actionType, mediaIntent);
        if (!intentStatus.ok) {
            result.status = intentStatus;
            return result;
        }
        if (!transportService_) {
            result.status = core::contracts::OperationStatus::failure("Media transport service is not configured.");
            return result;
        }

        const auto callId = request.context.value("callId", std::string{});
        if (callId.empty()) {
            result.status = core::contracts::OperationStatus::failure("callId must not be empty for direct call media actions.");
            return result;
        }

        const auto mediaContextStatus = repository->resolveDirectCallMediaContext(authSession->userId, callId);
        if (!mediaContextStatus.ok) {
            result.status = mediaContextStatus;
            return result;
        }

        const auto roomId = mediaContextStatus.data.value("roomId", std::string{});
        if (roomId.empty()) {
            result.status = core::contracts::OperationStatus::failure("Direct call media room is not resolved.");
            return result;
        }

        auto mediaCommand = buildMediaCommand(request, roomId);
        std::vector<MediaTransportEvent> mediaEvents;
        auto mediaStatus = transportService_->execute(mediaIntent, mediaCommand, mediaEvents);

        if (mediaIntent == MediaTransportIntent::OpenTransport
            && shouldRetryOpenTransportRecovery(mediaStatus)) {
            auto joinCommand = buildJoinSessionCommand(request, roomId);
            std::vector<MediaTransportEvent> joinEvents;
            auto joinStatus = transportService_->execute(
                MediaTransportIntent::JoinSession,
                joinCommand,
                joinEvents);

            if (!joinStatus.ok && shouldRecreateRoomBeforeRetry(joinStatus)) {
                auto createCommand = buildCreateRoomCommand(request, roomId);
                std::vector<MediaTransportEvent> createEvents;
                const auto createStatus = transportService_->execute(
                    MediaTransportIntent::CreateRoom,
                    createCommand,
                    createEvents);
                appendOutboundTransportEvents(
                    createEvents,
                    kDirectCallRouteObject,
                    nlohmann::json{ { "callId", callId } },
                    result.outboundEvents);

                if (createStatus.ok
                    || createStatus.message.find("Room already exists") != std::string::npos) {
                    joinEvents.clear();
                    joinStatus = transportService_->execute(
                        MediaTransportIntent::JoinSession,
                        joinCommand,
                        joinEvents);
                }
                else {
                    joinStatus = createStatus;
                }
            }

            appendOutboundTransportEvents(
                joinEvents,
                kDirectCallRouteObject,
                nlohmann::json{ { "callId", callId } },
                result.outboundEvents);

            if (joinStatus.ok) {
                mediaEvents.clear();
                mediaStatus = transportService_->execute(mediaIntent, mediaCommand, mediaEvents);
            }
            else {
                mediaStatus = joinStatus;
            }
        }

        appendOutboundTransportEvents(
            mediaEvents,
            kDirectCallRouteObject,
            nlohmann::json{ { "callId", callId } },
            result.outboundEvents);

        if (!mediaStatus.ok) {
            result.status = mediaStatus;
            return result;
        }

        std::string resolvedMediaMessage;
        auto responseData = parseMediaBackendData(mediaStatus.message, resolvedMediaMessage);
        responseData["callId"] = callId;
        responseData["roomId"] = roomId;
        const auto participantPeersIt = mediaContextStatus.data.find("participantPeerIds");
        if (participantPeersIt != mediaContextStatus.data.end() && participantPeersIt->is_array()) {
            responseData["participantPeerIds"] = *participantPeersIt;
            responseData["participant_peer_ids"] = *participantPeersIt;
            responseData["memberPeerIds"] = *participantPeersIt;
            responseData["member_peer_ids"] = *participantPeersIt;
            responseData["activePeerIds"] = *participantPeersIt;
            responseData["active_peer_ids"] = *participantPeersIt;
        }

        if (request.actionType == kActionOpenTransport) {
            const auto routerCapsIt = responseData.find("routerRtpCapabilities");
            const bool missingRouterCaps = routerCapsIt == responseData.end()
                || !routerCapsIt->is_object()
                || routerCapsIt->empty();
            if (missingRouterCaps) {
                result.status = core::contracts::OperationStatus::failure(
                    "Mediasoup backend did not provide routerRtpCapabilities for open_transport.");
                return result;
            }
        }
        result.status = core::contracts::OperationStatus::success(
            resolvedMediaMessage.empty() ? std::string("Direct call media action processed.") : std::move(resolvedMediaMessage),
            std::move(responseData));
        return result;
    }

    void DirectCallFeatureModule::onSessionDisconnected(
        std::string_view peerId,
        std::uintptr_t sessionHandle,
        std::vector<nlohmann::json>& outboundEvents) {
        if (peerId.empty()) {
            return;
        }

        auto repository = eds::server_new::control_plane::ControlPlaneServices::repository();
        if (!repository || !repository->isReady()) {
            return;
        }

        const auto status = repository->handleDirectCallPeerDisconnected(peerId, sessionHandle);
        if (!status.ok) {
            return;
        }

        const auto outboundIt = status.data.find("outboundEvents");
        if (outboundIt != status.data.end() && outboundIt->is_array()) {
            for (const auto& item : *outboundIt) {
                if (item.is_object()) {
                    outboundEvents.push_back(item);
                }
            }
        }

        if (!transportService_) {
            return;
        }

        const auto callsIt = status.data.find("calls");
        if (callsIt == status.data.end() || !callsIt->is_array()) {
            return;
        }

        using eds::server_new::mediasoup::service::MediaTransportCommand;
        using eds::server_new::mediasoup::service::MediaTransportEvent;
        using eds::server_new::mediasoup::service::MediaTransportIntent;

        for (const auto& callItem : *callsIt) {
            if (!callItem.is_object()) {
                continue;
            }

            const auto roomId = callItem.value("roomId", std::string{});
            if (roomId.empty()) {
                continue;
            }

            const auto peersIt = callItem.find("participantPeerIds");
            if (peersIt == callItem.end() || !peersIt->is_array()) {
                continue;
            }

            for (const auto& peerJson : *peersIt) {
                if (!peerJson.is_string()) {
                    continue;
                }

                MediaTransportCommand mediaCommand;
                mediaCommand.sessionHandle = sessionHandle;
                mediaCommand.sessionId = peerJson.get<std::string>();
                mediaCommand.peerId = peerJson.get<std::string>();
                mediaCommand.roomId = roomId;
                mediaCommand.correlationId = "direct_call_disconnect_cleanup";

                std::vector<MediaTransportEvent> ignoredEvents;
                static_cast<void>(transportService_->execute(
                    MediaTransportIntent::CloseSession,
                    mediaCommand,
                    ignoredEvents));
            }
        }
    }

    bool DirectCallFeatureModule::onInitialize() {
        return true;
    }

    void DirectCallFeatureModule::onShutdown() {
    }

} // namespace eds::server_new::features::direct_call
