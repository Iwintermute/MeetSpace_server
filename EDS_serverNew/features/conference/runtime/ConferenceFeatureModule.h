#pragma once

#include "features/conference/runtime/ConferenceFeatureManager.h"
#include "features/conference/runtime/ConferenceStateStore.h"
#include "features/runtime/FeatureRegistry.h"

#include <memory>

namespace eds::server_new::features::conference {

class ConferenceFeatureModule final : public eds::server_new::features::runtime::IFeatureModule {
public:
    ConferenceFeatureModule() = default;
    ~ConferenceFeatureModule() override = default;

    std::string_view objectType() const override;
    std::string_view defaultAgent() const override;
    core::contracts::OperationStatus ensureRegistered(core::runtime::MessageDispatcher& dispatcher) override;
    eds::server_new::features::runtime::FeatureDispatchResult dispatch(
        const eds::server_new::features::runtime::FeatureDispatchRequest& request,
        core::runtime::MessageDispatcher& dispatcher) override;

private:
    bool registered_ = false;
    std::shared_ptr<ConferenceFeatureManager> manager_;
    std::shared_ptr<ConferenceStateStore> stateStore_;
};

} // namespace eds::server_new::features::conference
