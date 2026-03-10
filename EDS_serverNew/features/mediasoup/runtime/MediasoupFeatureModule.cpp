#include "features/mediasoup/runtime/MediasoupFeatureModule.h"

#include "Bridge/Mediasoup/runtime/MediasoupCommand.h"
#include "contracts/TypedMessage.h"

namespace eds::server_new::features::mediasoup {

std::string_view MediasoupFeatureModule::objectType() const {
    return eds::server_new::mediasoup::kRouteObject;
}

std::string_view MediasoupFeatureModule::defaultAgent() const {
    return eds::server_new::mediasoup::kDefaultAgent;
}

core::contracts::OperationStatus MediasoupFeatureModule::ensureRegistered(core::runtime::MessageDispatcher& dispatcher) {
    if (registered_) {
        return core::contracts::OperationStatus::success();
    }

    if (!stateStore_) {
        stateStore_ = std::make_shared<eds::server_new::mediasoup::MediasoupStateStore>();
    }
    if (!rtcBridge_) {
        rtcBridge_ = std::make_shared<eds::server_new::mediasoup::MediasoupRtcBridge>();
    }
    if (!manager_) {
        manager_ = std::make_shared<eds::server_new::mediasoup::MediasoupFeatureManager>(stateStore_, rtcBridge_);
    }

    auto status = dispatcher.registerManager(std::string(objectType()), manager_);
    if (!status.ok) {
        return status;
    }

    registered_ = true;
    return core::contracts::OperationStatus::success();
}

eds::server_new::features::runtime::FeatureDispatchResult MediasoupFeatureModule::dispatch(
    const eds::server_new::features::runtime::FeatureDispatchRequest& request,
    core::runtime::MessageDispatcher& dispatcher) {
    eds::server_new::features::runtime::FeatureDispatchResult result;
    result.status = ensureRegistered(dispatcher);
    if (!result.status.ok) {
        return result;
    }

    eds::server_new::mediasoup::MediasoupCommand command;
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

    if (command.peerId != request.peerId) {
        result.status = core::contracts::OperationStatus::failure("peer impersonation detected.");
        return result;
    }

    const core::contracts::MessageRoute route{
        std::string(objectType()),
        request.agentType,
        request.actionType
    };
    core::contracts::TypedMessage<eds::server_new::mediasoup::MediasoupCommand> payload(command);
    result.status = dispatcher.dispatch(route, payload);

    if (rtcBridge_) {
        const auto events = rtcBridge_->consumeEventsForPeer(request.peerId);
        for (const auto& event : events) {
            result.outboundEvents.push_back({
                { "type", event.type },
                { "peer", event.peerId },
                { "sdp", event.sdp },
                { "sdpMid", event.sdpMid },
                { "candidate", event.candidate }
            });
        }
    }

    return result;
}

void MediasoupFeatureModule::onSessionDisconnected(std::string_view peerId, std::uintptr_t sessionHandle) {
    if (rtcBridge_) {
        rtcBridge_->onSessionDisconnected(peerId, sessionHandle);
    }
}


} // namespace eds::server_new::features::mediasoup
