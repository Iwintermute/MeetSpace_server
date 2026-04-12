#include "features/conference/runtime/ConferenceMembershipAgent.h"

#include "features/conference/runtime/ConferenceActions.h"
#include "features/conference/runtime/ConferenceCommand.h"
#include "features/runtime/AgentActionRegistration.h"

#include <stdexcept>
#include <utility>

namespace eds::server_new::features::conference {

ConferenceMembershipAgent::ConferenceMembershipAgent(std::shared_ptr<ConferenceStateStore> stateStore)
    : BaseAgent("ConferenceMembershipAgent", static_cast<ModuleId>(-1)),
      stateStore_(std::move(stateStore)) {
    if (!stateStore_) {
        throw std::invalid_argument("ConferenceMembershipAgent requires a state store.");
    }
    eds::server_new::features::runtime::registerActionOrThrow(*this, std::string(kActionJoinConference), [stateStore = stateStore_]() {
        return std::make_unique<JoinConferenceAction>(stateStore);
    });
    eds::server_new::features::runtime::registerActionOrThrow(*this, std::string(kActionLeaveConference), [stateStore = stateStore_]() {
        return std::make_unique<LeaveConferenceAction>(stateStore);
    });
    eds::server_new::features::runtime::registerActionOrThrow(*this, std::string(kActionListMembers), [stateStore = stateStore_]() {
        return std::make_unique<ListMembersAction>(stateStore);
    });
}

} // namespace eds::server_new::features::conference
