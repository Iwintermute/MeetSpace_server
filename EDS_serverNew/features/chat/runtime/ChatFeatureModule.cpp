#include "features/chat/runtime/ChatFeatureModule.h"

#include "contracts/TypedMessage.h"
#include "features/chat/runtime/ChatCommand.h"
#include "features/events/ConferenceEvents.h"
#include <string>
#include <utility>

namespace eds::server_new::features::chat {
ChatFeatureModule::ChatFeatureModule()
    : BaseModule("ChatFeatureModule", static_cast<core::contracts::ModuleId>(-1)) {
}

std::string_view ChatFeatureModule::objectType() const {
    return kChatRouteObject;
}

std::string_view ChatFeatureModule::defaultAgent() const {
    return kChatMessagingAgent;
}

core::contracts::OperationStatus ChatFeatureModule::ensureRegistered(core::runtime::MessageDispatcher& dispatcher) {
    if (registered_) {
        return core::contracts::OperationStatus::success();
    }

    if (!stateStore_) {
        stateStore_ = std::make_shared<ChatStateStore>();
    }
    if (!manager_) {
        manager_ = std::make_shared<ChatFeatureManager>(stateStore_);
    }
    if (!eventBus_) {
        eventBus_ = eds::server_new::features::runtime::FeatureEventBus::instance();
    }
    if (!conferenceSnapshotSubscription_.connected() && eventBus_) {
        conferenceSnapshotSubscription_ = eventBus_->subscribe<eds::server_new::features::events::ConferenceMembersSnapshotEvent>(
            [stateStore = stateStore_](const auto& event) {
                if (stateStore) {
                    stateStore->applyConferenceSnapshot(event);
                }
            });
    }

    auto status = dispatcher.registerManager(std::string(objectType()), manager_);
    if (!status.ok) {
        return status;
    }

    registered_ = true;
    return core::contracts::OperationStatus::success();
}

eds::server_new::features::runtime::FeatureDispatchResult ChatFeatureModule::dispatch(
    const eds::server_new::features::runtime::FeatureDispatchRequest& request,
    core::runtime::MessageDispatcher& dispatcher) {
    eds::server_new::features::runtime::FeatureDispatchResult result;
    result.status = ensureRegistered(dispatcher);
    if (!result.status.ok) {
        return result;
    }

    ChatCommand command;
    command.sessionHandle = request.sessionHandle;
    command.sessionId = request.peerId;
    command.peerId = request.context.value("peerId", request.context.value("peer", request.peerId));
    command.conferenceId = request.context.value("conferenceId", request.context.value("roomId", std::string{}));
    command.targetPeerId = request.context.value("targetPeerId", request.context.value("peerTo", std::string{}));
    command.clientRequestId = request.context.value("clientRequestId", request.context.value("messageId", std::string{}));
    command.text = request.context.value("text", request.context.value("message", std::string{}));

    if (command.peerId != request.peerId) {
        result.status = core::contracts::OperationStatus::failure("peer impersonation detected.");
        return result;
    }

    const core::contracts::MessageRoute route{
        std::string(objectType()),
        request.agentType,
        request.actionType
    };
    core::contracts::TypedMessage<ChatCommand> payload(command);
    result.status = dispatcher.dispatch(route, payload);
    if (!result.status.ok || !stateStore_) {
        return result;
    }

    auto outbound = stateStore_->consumeOutboundEventsForPeer(request.peerId);
    result.outboundEvents.reserve(result.outboundEvents.size() + outbound.size());
    for (auto& item : outbound) {
        nlohmann::json event = std::move(item.payload);
        event["deliverTo"] = item.targetPeerId;
        result.outboundEvents.push_back(std::move(event));
    }
    return result;
}

bool ChatFeatureModule::onInitialize() {
    return true;
}

void ChatFeatureModule::onShutdown() {
}

} // namespace eds::server_new::features::chat
