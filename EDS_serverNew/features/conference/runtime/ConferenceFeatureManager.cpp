#include "features/conference/runtime/ConferenceFeatureManager.h"

#include "features/conference/runtime/ConferenceCommand.h"
#include "features/conference/runtime/ConferenceLifecycleAgent.h"
#include "features/conference/runtime/ConferenceMembershipAgent.h"

#include <stdexcept>
#include <utility>

namespace eds::server_new::features::conference {

ConferenceFeatureManager::ConferenceFeatureManager(std::shared_ptr<ConferenceStateStore> stateStore)
    : BaseFeatureManager("ConferenceFeatureManager", static_cast<ModuleId>(-1)),
      stateStore_(std::move(stateStore)) {
    if (!stateStore_) {
        throw std::invalid_argument("ConferenceFeatureManager requires a state store.");
    }

    auto lifecycleStatus = registerAgent(std::string(kConferenceLifecycleAgent), [stateStore = stateStore_]() {
        return std::make_unique<ConferenceLifecycleAgent>(stateStore);
    });
    if (!lifecycleStatus.ok) {
        throw std::runtime_error(lifecycleStatus.message);
    }

    auto membershipStatus = registerAgent(std::string(kConferenceMembershipAgent), [stateStore = stateStore_]() {
        return std::make_unique<ConferenceMembershipAgent>(stateStore);
    });
    if (!membershipStatus.ok) {
        throw std::runtime_error(membershipStatus.message);
    }
}

} // namespace eds::server_new::features::conference
