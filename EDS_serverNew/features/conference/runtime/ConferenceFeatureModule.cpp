#include "features/conference/runtime/ConferenceFeatureModule.h"

#include "contracts/TypedMessage.h"
#include "features/conference/runtime/ConferenceCommand.h"
#include "features/events/ConferenceEvents.h"
#include <utility>

namespace eds::server_new::features::conference {
ConferenceFeatureModule::ConferenceFeatureModule()
    : BaseModule("ConferenceFeatureModule", static_cast<core::contracts::ModuleId>(-1)) {
}

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
    if (!eventBus_) {
        eventBus_ = eds::server_new::features::runtime::FeatureEventBus::instance();
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
    if (result.status.ok && shouldPublishConferenceSnapshot(request.actionType)) {
        publishConferenceSnapshot(command.conferenceId);
    }
    return result;
}
void ConferenceFeatureModule::onSessionDisconnected(std::string_view peerId, std::uintptr_t sessionHandle) {
    static_cast<void>(sessionHandle);

    if (!stateStore_ || peerId.empty()) {
        return;
    }

    auto conferenceIds = stateStore_->listConferenceIdsForPeer(peerId);
    for (const auto& conferenceId : conferenceIds) {
        ConferenceCommand leaveCommand;
        leaveCommand.peerId = std::string(peerId);
        leaveCommand.conferenceId = conferenceId;
        auto leaveStatus = stateStore_->leaveConference(leaveCommand);
        if (leaveStatus.ok) {
            publishConferenceSnapshot(conferenceId);
        }
    }
}

void ConferenceFeatureModule::publishConferenceSnapshot(std::string_view conferenceId) {
    if (!eventBus_ || !stateStore_ || conferenceId.empty()) {
        return;
    }

    ConferenceStateStore::ConferenceSnapshot snapshot;
    if (!stateStore_->tryGetConferenceSnapshot(conferenceId, snapshot)) {
        return;
    }

    eds::server_new::features::events::ConferenceMembersSnapshotEvent event;
    event.conferenceId = snapshot.conferenceId;
    event.ownerPeerId = snapshot.ownerPeerId;
    event.isClosed = snapshot.isClosed;
    event.revision = snapshot.revision;
    event.memberPeerIds = std::move(snapshot.memberPeerIds);
    eventBus_->publish(event);
}

bool ConferenceFeatureModule::shouldPublishConferenceSnapshot(std::string_view actionType) const {
    return actionType == kActionCreateConference
        || actionType == kActionCloseConference
        || actionType == kActionJoinConference
        || actionType == kActionLeaveConference;
}
bool ConferenceFeatureModule::onInitialize() {
    return true;
}

void ConferenceFeatureModule::onShutdown() {
}


} // namespace eds::server_new::features::conference
