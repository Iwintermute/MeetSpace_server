#include "features/conference/runtime/ConferenceLifecycleAgent.h"

#include "features/conference/runtime/ConferenceActions.h"
#include "features/conference/runtime/ConferenceCommand.h"

#include <stdexcept>
#include <utility>

namespace {
void registerActionOrThrow(BaseAgent& agent, std::string actionKey, iAgent::tActionFactory factory) {
    auto status = agent.registerAction(std::move(actionKey), std::move(factory));
    if (!status.ok) {
        throw std::runtime_error(status.message);
    }
}
}

namespace eds::server_new::features::conference {

ConferenceLifecycleAgent::ConferenceLifecycleAgent(std::shared_ptr<ConferenceStateStore> stateStore)
    : BaseAgent("ConferenceLifecycleAgent", static_cast<ModuleId>(-1)),
      stateStore_(std::move(stateStore)) {
    if (!stateStore_) {
        throw std::invalid_argument("ConferenceLifecycleAgent requires a state store.");
    }

    registerActionOrThrow(*this, std::string(kActionCreateConference), [stateStore = stateStore_]() {
        return std::make_unique<CreateConferenceAction>(stateStore);
    });
    registerActionOrThrow(*this, std::string(kActionGetConference), [stateStore = stateStore_]() {
        return std::make_unique<GetConferenceAction>(stateStore);
    });
    registerActionOrThrow(*this, std::string(kActionCloseConference), [stateStore = stateStore_]() {
        return std::make_unique<CloseConferenceAction>(stateStore);
    });
}

} // namespace eds::server_new::features::conference
