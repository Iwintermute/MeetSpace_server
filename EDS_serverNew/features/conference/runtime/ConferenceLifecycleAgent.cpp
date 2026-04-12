#include "features/conference/runtime/ConferenceLifecycleAgent.h"

#include "features/conference/runtime/ConferenceActions.h"
#include "features/conference/runtime/ConferenceCommand.h"
#include "features/runtime/AgentActionRegistration.h"

#include <stdexcept>
#include <utility>

namespace eds::server_new::features::conference {

ConferenceLifecycleAgent::ConferenceLifecycleAgent(std::shared_ptr<ConferenceStateStore> stateStore)
    : BaseAgent("ConferenceLifecycleAgent", static_cast<ModuleId>(-1)),
      stateStore_(std::move(stateStore)) {
    if (!stateStore_) {
        throw std::invalid_argument("ConferenceLifecycleAgent requires a state store.");
    }
    eds::server_new::features::runtime::registerActionOrThrow(*this, std::string(kActionCreateConference), [stateStore = stateStore_]() {
        return std::make_unique<CreateConferenceAction>(stateStore);
    });
    eds::server_new::features::runtime::registerActionOrThrow(*this, std::string(kActionGetConference), [stateStore = stateStore_]() {
        return std::make_unique<GetConferenceAction>(stateStore);
    });
    eds::server_new::features::runtime::registerActionOrThrow(*this, std::string(kActionCloseConference), [stateStore = stateStore_]() {
        return std::make_unique<CloseConferenceAction>(stateStore);
    });
}

} // namespace eds::server_new::features::conference
