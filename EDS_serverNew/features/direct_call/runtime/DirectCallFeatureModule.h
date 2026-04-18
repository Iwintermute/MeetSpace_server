#pragma once

#include "Bridge/Mediasoup/service/IMediaTransportService.h"
#include "features/direct_call/runtime/DirectCallCommand.h"
#include "features/runtime/FeatureRegistry.h"
#include "modules/BaseModule.h"

#include <memory>
#include <string_view>

namespace eds::server_new::features::direct_call {

    class DirectCallFeatureModule final
        : public BaseModule
        , public eds::server_new::features::runtime::IFeatureModule {
    public:
        DirectCallFeatureModule();
        ~DirectCallFeatureModule() override = default;

        std::string_view objectType() const override;
        std::string_view defaultAgent() const override;

        core::contracts::OperationStatus ensureRegistered(core::runtime::MessageDispatcher& dispatcher) override;
        eds::server_new::features::runtime::FeatureDispatchResult dispatch(
            const eds::server_new::features::runtime::FeatureDispatchRequest& request,
            core::runtime::MessageDispatcher& dispatcher) override;

        void onSessionDisconnected(
            std::string_view peerId,
            std::uintptr_t sessionHandle,
            std::vector<nlohmann::json>& outboundEvents) override;

    private:
        bool onInitialize() override;
        void onShutdown() override;

    private:
        bool registered_ = false;
        std::shared_ptr<eds::server_new::mediasoup::service::IMediaTransportService> transportService_;
    };

} // namespace eds::server_new::features::direct_call