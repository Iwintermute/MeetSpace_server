#include "features/conference/runtime/ConferenceFeatureModule.h"

#include "Auth/runtime/AuthServices.h"
#include "Bridge/Mediasoup/service/MediaTransportTypes.h"
#include "Bridge/Mediasoup/service/SharedMediaTransportService.h"
#include "infrastructure/control_plane/runtime/ControlPlaneServices.h"

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace eds::server_new::features::conference {
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

            return core::contracts::OperationStatus::failure("Unsupported conference action.");
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
            command.candidate = request.context.value("candidate", std::string{});
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
            const auto dataIt = parsed.find("data");
            if (dataIt != parsed.end() && dataIt->is_object()) {
                return *dataIt;
            }
            return nlohmann::json::object();
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

    ConferenceFeatureModule::ConferenceFeatureModule()
        : BaseModule("ConferenceFeatureModule", static_cast<core::contracts::ModuleId>(-1)) {
    }

    std::string_view ConferenceFeatureModule::objectType() const {
        return kConferenceRouteObject;
    }

    std::string_view ConferenceFeatureModule::defaultAgent() const {
        return kConferenceLifecycleAgent;
    }

    core::contracts::OperationStatus ConferenceFeatureModule::ensureRegistered(
        core::runtime::MessageDispatcher& dispatcher) {
        static_cast<void>(dispatcher);

        if (registered_) {
            return core::contracts::OperationStatus::success();
        }

        if (!transportService_) {
            transportService_ = eds::server_new::mediasoup::service::sharedMediaTransportService(false);
        }

        registered_ = true;
        return core::contracts::OperationStatus::success();
    }

    eds::server_new::features::runtime::FeatureDispatchResult ConferenceFeatureModule::dispatch(
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
            result.status = core::contracts::OperationStatus::failure("Conference control-plane is not configured.");
            return result;
        }

        const auto authSession = sessionStore->get(request.sessionHandle);
        if (!authSession.has_value() || !authSession->authenticated) {
            result.status = core::contracts::OperationStatus::failure("Unauthorized conference request.");
            return result;
        }

        const auto peerId = request.context.value("peerId", request.context.value("peer", request.peerId));
        if (peerId != request.peerId) {
            result.status = core::contracts::OperationStatus::failure("peer impersonation detected.");
            return result;
        }

        const auto conferenceId =
            request.context.value("conferenceId", request.context.value("roomId", std::string{}));

        if (request.actionType == kActionCreateConference) {
            result.status = repository->createConference(
                authSession->userId,
                conferenceId,
                request.peerId,
                request.sessionHandle);

            appendOutboundEventsFromStatus(result.status, result);

            if (result.status.ok) {
                const auto mediaRoomId = result.status.data.value("mediaRoomId", std::string{});
                if (!mediaRoomId.empty()) {
                    MediaTransportCommand mediaCommand;
                    mediaCommand.sessionHandle = request.sessionHandle;
                    mediaCommand.sessionId = request.peerId;
                    mediaCommand.peerId = request.peerId;
                    mediaCommand.roomId = mediaRoomId;
                    mediaCommand.correlationId =
                        request.context.value("clientRequestId", std::string("conference_create"));

                    executeMediaIntentIgnoreFailure(
                        transportService_,
                        MediaTransportIntent::CreateRoom,
                        mediaCommand);

                    executeMediaIntentIgnoreFailure(
                        transportService_,
                        MediaTransportIntent::JoinSession,
                        mediaCommand);
                }
            }

            return result;
        }

        if (request.actionType == kActionGetConference) {
            result.status = repository->getConference(authSession->userId, conferenceId);
            return result;
        }

        if (request.actionType == kActionCloseConference) {
            result.status = repository->closeConference(authSession->userId, conferenceId);
            appendOutboundEventsFromStatus(result.status, result);

            if (result.status.ok) {
                const auto mediaRoomId = result.status.data.value("mediaRoomId", std::string{});
                const auto activePeerIds =
                    result.status.data.value("activePeerIds", nlohmann::json::array());

                if (!mediaRoomId.empty() && activePeerIds.is_array()) {
                    for (const auto& item : activePeerIds) {
                        if (!item.is_string()) {
                            continue;
                        }

                        MediaTransportCommand mediaCommand;
                        mediaCommand.sessionHandle = request.sessionHandle;
                        mediaCommand.sessionId = item.get<std::string>();
                        mediaCommand.peerId = item.get<std::string>();
                        mediaCommand.roomId = mediaRoomId;
                        mediaCommand.correlationId =
                            request.context.value("clientRequestId", std::string("conference_close"));

                        executeMediaIntentIgnoreFailure(
                            transportService_,
                            MediaTransportIntent::CloseSession,
                            mediaCommand);
                    }
                }
            }

            return result;
        }

        if (request.actionType == kActionJoinConference) {
            result.status = repository->joinConference(
                authSession->userId,
                conferenceId,
                request.peerId,
                request.sessionHandle);

            appendOutboundEventsFromStatus(result.status, result);

            if (result.status.ok) {
                const auto mediaRoomId = result.status.data.value("mediaRoomId", std::string{});
                if (!mediaRoomId.empty()) {
                    MediaTransportCommand mediaCommand;
                    mediaCommand.sessionHandle = request.sessionHandle;
                    mediaCommand.sessionId = request.peerId;
                    mediaCommand.peerId = request.peerId;
                    mediaCommand.roomId = mediaRoomId;
                    mediaCommand.correlationId =
                        request.context.value("clientRequestId", std::string("conference_join"));

                    executeMediaIntentIgnoreFailure(
                        transportService_,
                        MediaTransportIntent::JoinSession,
                        mediaCommand);
                }
            }

            return result;
        }

        if (request.actionType == kActionLeaveConference) {
            result.status = repository->leaveConference(
                authSession->userId,
                conferenceId,
                request.peerId,
                request.sessionHandle);

            appendOutboundEventsFromStatus(result.status, result);

            if (result.status.ok) {
                const auto mediaRoomId = result.status.data.value("mediaRoomId", std::string{});
                if (!mediaRoomId.empty()) {
                    MediaTransportCommand mediaCommand;
                    mediaCommand.sessionHandle = request.sessionHandle;
                    mediaCommand.sessionId = request.peerId;
                    mediaCommand.peerId = request.peerId;
                    mediaCommand.roomId = mediaRoomId;
                    mediaCommand.correlationId =
                        request.context.value("clientRequestId", std::string("conference_leave"));

                    executeMediaIntentIgnoreFailure(
                        transportService_,
                        MediaTransportIntent::LeaveSession,
                        mediaCommand);
                }
            }

            return result;
        }

        if (request.actionType == kActionListMembers) {
            result.status = repository->listConferenceMembers(authSession->userId, conferenceId);
            return result;
        }
        if (request.actionType == kActionListUserConferences) {
            const auto limit = request.context.value("limit", static_cast<std::size_t>(100));
            result.status = repository->listUserConferences(authSession->userId, limit);
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

        if (conferenceId.empty()) {
            result.status = core::contracts::OperationStatus::failure("conferenceId must not be empty for conference media actions.");
            return result;
        }

        const auto mediaContextStatus = repository->resolveConferenceMediaContext(authSession->userId, conferenceId);
        if (!mediaContextStatus.ok) {
            result.status = mediaContextStatus;
            return result;
        }

        const auto roomId = mediaContextStatus.data.value("roomId", std::string{});
        if (roomId.empty()) {
            result.status = core::contracts::OperationStatus::failure("Conference media room is not resolved.");
            return result;
        }

        auto mediaCommand = buildMediaCommand(request, roomId);
        std::vector<MediaTransportEvent> mediaEvents;
        auto mediaStatus = transportService_->execute(mediaIntent, mediaCommand, mediaEvents);

        appendOutboundTransportEvents(
            mediaEvents,
            kConferenceRouteObject,
            nlohmann::json{ { "conferenceId", conferenceId } },
            result.outboundEvents);

        if (!mediaStatus.ok) {
            result.status = mediaStatus;
            return result;
        }

        std::string resolvedMediaMessage;
        auto responseData = parseMediaBackendData(mediaStatus.message, resolvedMediaMessage);
        responseData["conferenceId"] = conferenceId;
        responseData["roomId"] = roomId;
        const auto activePeersIt = mediaContextStatus.data.find("activePeerIds");
        if (activePeersIt != mediaContextStatus.data.end() && activePeersIt->is_array()) {
            responseData["activePeerIds"] = *activePeersIt;
        }
        result.status = core::contracts::OperationStatus::success(
            resolvedMediaMessage.empty() ? std::string("Conference media action processed.") : std::move(resolvedMediaMessage),
            std::move(responseData));
        return result;
    }

    void ConferenceFeatureModule::onSessionDisconnected(
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

        const auto status = repository->handleConferencePeerDisconnected(peerId, sessionHandle);
        if (!status.ok) {
            return;
        }

        const auto it = status.data.find("outboundEvents");
        if (it == status.data.end() || !it->is_array()) {
            return;
        }

        for (const auto& item : *it) {
            if (item.is_object()) {
                outboundEvents.push_back(item);
            }
        }
    }

    bool ConferenceFeatureModule::onInitialize() {
        return true;
    }

    void ConferenceFeatureModule::onShutdown() {
    }

} // namespace eds::server_new::features::conference
