#include "features/mediasoup/runtime/MediasoupFeatureModule.h"

#include "Bridge/Mediasoup/runtime/MediasoupCommand.h"
#include "Bridge/Mediasoup/runtime/MediasoupDebugConfig.h"
#include "Bridge/Mediasoup/service/MediaTransportTypes.h"
#include "Bridge/Mediasoup/service/SharedMediaTransportService.h"
#include "features/events/AudioSessionEvents.h"

#include <nlohmann/json.hpp>
#include <utility>
#include <vector>

namespace eds::server_new::features::mediasoup {
    namespace {

        using eds::server_new::mediasoup::service::MediaTransportEvent;
        using eds::server_new::mediasoup::service::MediaTransportEventType;

        eds::server_new::features::events::AudioSessionLifecycleEvent toAudioSessionLifecycleEvent(
            const MediaTransportEvent& transportEvent) {
            eds::server_new::features::events::AudioSessionLifecycleEvent event;
            event.roomId = transportEvent.roomId;
            event.actorPeerId = transportEvent.peerId;
            event.started = transportEvent.started;
            event.ended = transportEvent.ended;
            event.reason = transportEvent.reason;
            event.memberPeerIds = transportEvent.memberPeerIds;
            event.notifyPeerIds = transportEvent.notifyPeerIds;
            if (event.notifyPeerIds.empty() && !transportEvent.peerId.empty()) {
                event.notifyPeerIds.push_back(transportEvent.peerId);
            }
            return event;
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

        nlohmann::json buildProducerArray(const std::vector<eds::server_new::mediasoup::service::MediaProducerSnapshot>& snapshots) {
            nlohmann::json result = nlohmann::json::array();
            for (const auto& snapshot : snapshots) {
                result.push_back({
                    { "producerId", snapshot.producerId },
                    { "peerId", snapshot.peerId },
                    { "kind", snapshot.kind },
                    { "trackType", snapshot.trackType }
                    });
            }
            return result;
        }

    } // namespace

    MediasoupFeatureModule::MediasoupFeatureModule()
        : BaseModule("MediasoupFeatureModule", static_cast<core::contracts::ModuleId>(-1)) {
    }

    std::string_view MediasoupFeatureModule::objectType() const {
        return eds::server_new::mediasoup::kRouteObject;
    }

    std::string_view MediasoupFeatureModule::defaultAgent() const {
        return eds::server_new::mediasoup::kDefaultAgent;
    }

    core::contracts::OperationStatus MediasoupFeatureModule::ensureRegistered(core::runtime::MessageDispatcher& dispatcher) {
        static_cast<void>(dispatcher);
        if (registered_) {
            return core::contracts::OperationStatus::success();
        }

        if (!transportService_) {
            transportService_ = eds::server_new::mediasoup::service::sharedMediaTransportService(
                eds::server_new::mediasoup::debug::isServerDebugEnabled());
        }
        if (!eventBus_) {
            eventBus_ = eds::server_new::features::runtime::FeatureEventBus::instance();
        }

        registered_ = true;
        return core::contracts::OperationStatus::success();
    }

    eds::server_new::features::runtime::FeatureDispatchResult MediasoupFeatureModule::dispatch(
        const eds::server_new::features::runtime::FeatureDispatchRequest& request,
        core::runtime::MessageDispatcher& dispatcher) {
        static_cast<void>(dispatcher);
        eds::server_new::features::runtime::FeatureDispatchResult result;
        result.status = ensureRegistered(dispatcher);
        if (!result.status.ok) {
            return result;
        }

        eds::server_new::mediasoup::service::MediaTransportIntent intent =
            eds::server_new::mediasoup::service::MediaTransportIntent::CreateRoom;
        result.status = resolveIntent(request.actionType, intent);
        if (!result.status.ok) {
            return result;
        }

        eds::server_new::mediasoup::service::MediaTransportCommand command;
        command.sessionHandle = request.sessionHandle;
        command.sessionId = request.peerId;
        command.peerId = request.context.value("peerId", request.context.value("peer", request.peerId));
        command.roomId = request.context.value("roomId", std::string{});
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

        if (command.peerId != request.peerId) {
            result.status = core::contracts::OperationStatus::failure("peer impersonation detected.");
            return result;
        }

        std::vector<MediaTransportEvent> transportEvents;
        result.status = transportService_->execute(intent, command, transportEvents);
        publishTransportEvents(transportEvents);
        appendOutboundTransportEvents(transportEvents, result.outboundEvents);
        if (!result.status.ok) {
            return result;
        }

        const auto signalingEvents = transportService_->consumeSignalingEventsForPeer(request.peerId);
        for (const auto& event : signalingEvents) {
            result.outboundEvents.push_back({
                { "type", event.type },
                { "peer", event.peerId },
                { "sdp", event.sdp },
                { "sdpMid", event.sdpMid },
                { "candidate", event.candidate }
                });
        }

        return result;
    }

    void MediasoupFeatureModule::onSessionDisconnected(
        std::string_view peerId,
        std::uintptr_t sessionHandle,
        std::vector<nlohmann::json>& outboundEvents) {
        if (!transportService_) {
            return;
        }

        std::vector<MediaTransportEvent> transportEvents;
        transportService_->onSessionDisconnected(peerId, sessionHandle, transportEvents);
        publishTransportEvents(transportEvents);
        appendOutboundTransportEvents(transportEvents, outboundEvents);
    }
    bool MediasoupFeatureModule::onInitialize() {
        return true;
    }

    void MediasoupFeatureModule::onShutdown() {
    }

    void MediasoupFeatureModule::publishTransportEvents(const std::vector<MediaTransportEvent>& events) {
        if (!eventBus_) {
            return;
        }

        for (const auto& event : events) {
            if (event.type != MediaTransportEventType::SessionStarted
                && event.type != MediaTransportEventType::SessionEnded) {
                continue;
            }
            eventBus_->publish(toAudioSessionLifecycleEvent(event));
        }
    }

    void MediasoupFeatureModule::appendOutboundTransportEvents(
        const std::vector<MediaTransportEvent>& events,
        std::vector<nlohmann::json>& outboundEvents) const {
        for (const auto& event : events) {
            if (event.notifyPeerIds.empty()) {
                continue;
            }

            nlohmann::json payload{
                { "type", std::string(eds::server_new::mediasoup::service::toString(event.type)) },
                { "object", std::string(eds::server_new::mediasoup::kRouteObject) },
                { "roomId", event.roomId },
                { "peerId", event.peerId },
                { "memberPeerIds", event.memberPeerIds },
                { "reason", event.reason }
            };

            switch (event.type) {
            case MediaTransportEventType::RoomState:
                payload["activeProducers"] = buildProducerArray(event.activeProducers);
                break;
            case MediaTransportEventType::PeerJoined:
            case MediaTransportEventType::PeerLeft:
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
            case MediaTransportEventType::TransportOpened:
                payload["transportId"] = event.transportId;
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

    core::contracts::OperationStatus MediasoupFeatureModule::resolveIntent(
        std::string_view actionType,
        eds::server_new::mediasoup::service::MediaTransportIntent& intent) const {
        using eds::server_new::mediasoup::service::MediaTransportIntent;
        if (actionType == eds::server_new::mediasoup::kActionCreateRoom) {
            intent = MediaTransportIntent::CreateRoom;
            return core::contracts::OperationStatus::success();
        }
        if (actionType == eds::server_new::mediasoup::kActionJoinRoom
            || actionType == eds::server_new::mediasoup::kActionConnectSession) {
            intent = MediaTransportIntent::JoinSession;
            return core::contracts::OperationStatus::success();
        }
        if (actionType == eds::server_new::mediasoup::kActionLeaveRoom
            || actionType == eds::server_new::mediasoup::kActionDisconnectSession) {
            intent = MediaTransportIntent::LeaveSession;
            return core::contracts::OperationStatus::success();
        }
        if (actionType == eds::server_new::mediasoup::kActionOpenTransport) {
            intent = MediaTransportIntent::OpenTransport;
            return core::contracts::OperationStatus::success();
        }
        if (actionType == eds::server_new::mediasoup::kActionProduce) {
            intent = MediaTransportIntent::PublishTrack;
            return core::contracts::OperationStatus::success();
        }
        if (actionType == eds::server_new::mediasoup::kActionConsume) {
            intent = MediaTransportIntent::ConsumeTrack;
            return core::contracts::OperationStatus::success();
        }
        if (actionType == eds::server_new::mediasoup::kActionConsumerReady) {
            intent = MediaTransportIntent::ResumeConsumer;
            return core::contracts::OperationStatus::success();
        }
        if (actionType == eds::server_new::mediasoup::kActionWebrtcOffer) {
            intent = MediaTransportIntent::ApplyOffer;
            return core::contracts::OperationStatus::success();
        }
        if (actionType == eds::server_new::mediasoup::kActionWebrtcIce) {
            intent = MediaTransportIntent::ApplyIce;
            return core::contracts::OperationStatus::success();
        }
        if (actionType == eds::server_new::mediasoup::kActionStats) {
            intent = MediaTransportIntent::ReadStats;
            return core::contracts::OperationStatus::success();
        }
        if (actionType == eds::server_new::mediasoup::kActionWebrtcClose) {
            intent = MediaTransportIntent::CloseSession;
            return core::contracts::OperationStatus::success();
        }

        return core::contracts::OperationStatus::failure(
            "Unsupported mediasoup action. Direct mediasoup control is reserved for tests.");
    }
} // namespace eds::server_new::features::mediasoup