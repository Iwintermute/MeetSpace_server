#pragma once

#include "Bridge/Mediasoup/runtime/MediasoupFeatureManager.h"
#include "Bridge/Mediasoup/runtime/MediasoupRtcBridge.h"
#include "Bridge/Mediasoup/runtime/MediasoupStateStore.h"
#include "features/runtime/FeatureRegistry.h"

#include <memory>

namespace eds::server_new::features::mediasoup {

class MediasoupFeatureModule final : public eds::server_new::features::runtime::IFeatureModule {
public:
    MediasoupFeatureModule() = default;
    ~MediasoupFeatureModule() override = default;

    std::string_view objectType() const override;
    std::string_view defaultAgent() const override;
    core::contracts::OperationStatus ensureRegistered(core::runtime::MessageDispatcher& dispatcher) override;
    eds::server_new::features::runtime::FeatureDispatchResult dispatch(
        const eds::server_new::features::runtime::FeatureDispatchRequest& request,
        core::runtime::MessageDispatcher& dispatcher) override;
    void onSessionDisconnected(std::string_view peerId, std::uintptr_t sessionHandle) override;

private:
    bool registered_ = false;
    std::shared_ptr<eds::server_new::mediasoup::MediasoupFeatureManager> manager_;
    std::shared_ptr<eds::server_new::mediasoup::MediasoupStateStore> stateStore_;
    std::shared_ptr<eds::server_new::mediasoup::MediasoupRtcBridge> rtcBridge_;
};

} // namespace eds::server_new::features::mediasoup
