#pragma once

#include "features/conference/runtime/ConferenceFeatureManager.h"
#include "features/conference/runtime/ConferenceStateStore.h"
#include "features/runtime/FeatureEventBus.h"
#include "features/runtime/FeatureRegistry.h"
#include <cstdint>

#include <memory>
#include <string_view>

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
    void onSessionDisconnected(std::string_view peerId, std::uintptr_t sessionHandle) override;

private:
    void publishConferenceSnapshot(std::string_view conferenceId);
    bool shouldPublishConferenceSnapshot(std::string_view actionType) const;

private:
    bool registered_ = false;
    std::shared_ptr<ConferenceFeatureManager> manager_;
    std::shared_ptr<ConferenceStateStore> stateStore_;
    std::shared_ptr<eds::server_new::features::runtime::FeatureEventBus> eventBus_;
};

} // namespace eds::server_new::features::conference
