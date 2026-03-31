#include "features/mediasoup/runtime/MediasoupFeatureModule.h"

#include "Bridge/Mediasoup/runtime/MediasoupCommand.h"
#include "Bridge/Mediasoup/runtime/MediasoupDebugConfig.h"
#include "Bridge/Mediasoup/service/MediasoupTransportService.h"
#include "features/events/AudioSessionEvents.h"

#include <utility>
#include <vector>

namespace eds::server_new::features::mediasoup {
namespace {

eds::server_new::features::events::AudioSessionLifecycleEvent toAudioSessionLifecycleEvent(
    const eds::server_new::mediasoup::service::MediaTransportEvent& transportEvent) {
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
        transportService_ = std::make_shared<eds::server_new::mediasoup::service::MediasoupTransportService>(
            nullptr,
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

    eds::server_new::mediasoup::service::MediaTransportIntent intent = eds::server_new::mediasoup::service::MediaTransportIntent::CreateRoom;
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
    command.kind = request.context.value("kind", std::string{});
    command.sdp = request.context.value("sdp", std::string{});
    command.sdpMid = request.context.value("sdpMid", std::string{});
    command.candidate = request.context.value("candidate", std::string{});
    command.correlationId = request.context.value(
        "correlationId",
        request.context.value("clientRequestId", request.context.value("messageId", std::string{})));

    if (command.peerId != request.peerId) {
        result.status = core::contracts::OperationStatus::failure("peer impersonation detected.");
        return result;
    }

    std::vector<eds::server_new::mediasoup::service::MediaTransportEvent> transportEvents;
    result.status = transportService_->execute(intent, command, transportEvents);
    publishTransportEvents(transportEvents);
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

void MediasoupFeatureModule::onSessionDisconnected(std::string_view peerId, std::uintptr_t sessionHandle) {
    if (!transportService_) {
        return;
    }

    std::vector<eds::server_new::mediasoup::service::MediaTransportEvent> transportEvents;
    transportService_->onSessionDisconnected(peerId, sessionHandle, transportEvents);
    publishTransportEvents(transportEvents);
}

bool MediasoupFeatureModule::onInitialize() {
    return true;
}

void MediasoupFeatureModule::onShutdown() {
}

void MediasoupFeatureModule::publishTransportEvents(
    const std::vector<eds::server_new::mediasoup::service::MediaTransportEvent>& events) {
    if (!eventBus_) {
        return;
    }

    for (const auto& event : events) {
        if (event.type != eds::server_new::mediasoup::service::MediaTransportEventType::SessionStarted
            && event.type != eds::server_new::mediasoup::service::MediaTransportEventType::SessionEnded) {
            continue;
        }
        eventBus_->publish(toAudioSessionLifecycleEvent(event));
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
    if (actionType == eds::server_new::mediasoup::kActionWebrtcOffer) {
        intent = MediaTransportIntent::ApplyOffer;
        return core::contracts::OperationStatus::success();
    }
    if (actionType == eds::server_new::mediasoup::kActionWebrtcIce) {
        intent = MediaTransportIntent::ApplyIce;
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
