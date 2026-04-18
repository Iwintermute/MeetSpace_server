#include "features/direct_call/runtime/DirectCallFeatureManager.h"

#include "features/direct_call/runtime/DirectCallCommand.h"
#include "features/direct_call/runtime/DirectCallLifecycleAgent.h"

#include <stdexcept>
#include <utility>

namespace eds::server_new::features::direct_call {

    DirectCallFeatureManager::DirectCallFeatureManager(std::shared_ptr<DirectCallStateStore> stateStore)
        : BaseFeatureManager("DirectCallFeatureManager", static_cast<ModuleId>(-1)),
        stateStore_(std::move(stateStore)) {
        if (!stateStore_) {
            throw std::invalid_argument("DirectCallFeatureManager requires a state store.");
        }

        auto lifecycleStatus = registerAgent(std::string(kDirectCallLifecycleAgent), [stateStore = stateStore_]() {
            return std::make_unique<DirectCallLifecycleAgent>(stateStore);
            });
        if (!lifecycleStatus.ok) {
            throw std::runtime_error(lifecycleStatus.message);
        }
    }

} // namespace eds::server_new::features::direct_call