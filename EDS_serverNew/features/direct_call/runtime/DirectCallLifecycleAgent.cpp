#include "features/direct_call/runtime/DirectCallLifecycleAgent.h"

#include "features/direct_call/runtime/DirectCallActions.h"
#include "features/direct_call/runtime/DirectCallCommand.h"
#include "features/runtime/AgentActionRegistration.h"

#include <stdexcept>
#include <utility>

namespace eds::server_new::features::direct_call {

    DirectCallLifecycleAgent::DirectCallLifecycleAgent(std::shared_ptr<DirectCallStateStore> stateStore)
        : BaseAgent("DirectCallLifecycleAgent", static_cast<ModuleId>(-1)),
        stateStore_(std::move(stateStore)) {
        if (!stateStore_) {
            throw std::invalid_argument("DirectCallLifecycleAgent requires a state store.");
        }

        eds::server_new::features::runtime::registerActionOrThrow(*this, std::string(kActionCreateDirectCall), [stateStore = stateStore_]() {
            return std::make_unique<CreateDirectCallAction>(stateStore);
            });
        eds::server_new::features::runtime::registerActionOrThrow(*this, std::string(kActionAcceptDirectCall), [stateStore = stateStore_]() {
            return std::make_unique<AcceptDirectCallAction>(stateStore);
            });
        eds::server_new::features::runtime::registerActionOrThrow(*this, std::string(kActionDeclineDirectCall), [stateStore = stateStore_]() {
            return std::make_unique<DeclineDirectCallAction>(stateStore);
            });
        eds::server_new::features::runtime::registerActionOrThrow(*this, std::string(kActionHangupDirectCall), [stateStore = stateStore_]() {
            return std::make_unique<HangupDirectCallAction>(stateStore);
            });
    }

} // namespace eds::server_new::features::direct_call