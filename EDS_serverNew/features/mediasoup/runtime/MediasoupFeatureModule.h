#pragma once

#include "Bridge/Mediasoup/service/IMediaTransportService.h"
#include "features/runtime/FeatureEventBus.h"
#include "features/runtime/FeatureRegistry.h"
#include "modules/BaseModule.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace eds::server_new::features::mediasoup {

    class MediasoupFeatureModule final
        : public BaseModule
        , public eds::server_new::features::runtime::IFeatureModule {
    public:
        MediasoupFeatureModule();
        ~MediasoupFeatureModule() override = default;

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

        void publishTransportEvents(const std::vector<eds::server_new::mediasoup::service::MediaTransportEvent>& events);

        void appendOutboundTransportEvents(
            const std::vector<eds::server_new::mediasoup::service::MediaTransportEvent>& events,
            std::vector<nlohmann::json>& outboundEvents) const;

        core::contracts::OperationStatus resolveIntent(
            std::string_view actionType,
            eds::server_new::mediasoup::service::MediaTransportIntent& intent) const;

    private:
        bool registered_ = false;
        std::shared_ptr<eds::server_new::mediasoup::service::IMediaTransportService> transportService_;
        std::shared_ptr<eds::server_new::features::runtime::FeatureEventBus> eventBus_;
    };

} // namespace eds::server_new::features::mediasoup