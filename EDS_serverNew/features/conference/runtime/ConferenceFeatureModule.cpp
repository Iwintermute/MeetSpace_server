#include "features/conference/runtime/ConferenceFeatureModule.h"

#include "contracts/TypedMessage.h"
#include "features/conference/runtime/ConferenceCommand.h"

namespace eds::server_new::features::conference {

std::string_view ConferenceFeatureModule::objectType() const {
    return kConferenceRouteObject;
}

std::string_view ConferenceFeatureModule::defaultAgent() const {
    return kConferenceLifecycleAgent;
}

core::contracts::OperationStatus ConferenceFeatureModule::ensureRegistered(core::runtime::MessageDispatcher& dispatcher) {
    if (registered_) {
        return core::contracts::OperationStatus::success();
    }

    if (!stateStore_) {
        stateStore_ = std::make_shared<ConferenceStateStore>();
    }
    if (!manager_) {
        manager_ = std::make_shared<ConferenceFeatureManager>(stateStore_);
    }

    auto status = dispatcher.registerManager(std::string(objectType()), manager_);
    if (!status.ok) {
        return status;
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

    ConferenceCommand command;
    command.sessionHandle = request.sessionHandle;
    command.sessionId = request.peerId;
    command.peerId = request.context.value("peerId", request.context.value("peer", request.peerId));
    command.conferenceId = request.context.value("conferenceId", request.context.value("roomId", std::string{}));
    command.clientRequestId = request.context.value("clientRequestId", std::string{});
    command.targetPeerId = request.context.value("targetPeerId", request.context.value("memberId", std::string{}));

    if (command.peerId != request.peerId) {
        result.status = core::contracts::OperationStatus::failure("peer impersonation detected.");
        return result;
    }

    const core::contracts::MessageRoute route{
        std::string(objectType()),
        request.agentType,
        request.actionType
    };
    core::contracts::TypedMessage<ConferenceCommand> payload(command);
    result.status = dispatcher.dispatch(route, payload);
    return result;
}


} // namespace eds::server_new::features::conference
