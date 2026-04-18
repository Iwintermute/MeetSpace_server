#pragma once

#include "features/direct_chat/runtime/DirectChatCommand.h"
#include "features/runtime/FeatureRegistry.h"
#include "modules/BaseModule.h"
#include <memory>

#include <string_view>

namespace eds::server_new::features::direct_chat {

    class DirectChatStateStore;
    class DirectChatFeatureManager;

    class DirectChatFeatureModule final
        : public BaseModule
        , public eds::server_new::features::runtime::IFeatureModule {
    public:
        DirectChatFeatureModule();
        ~DirectChatFeatureModule() override = default;

        std::string_view objectType() const override;
        std::string_view defaultAgent() const override;

        core::contracts::OperationStatus ensureRegistered(core::runtime::MessageDispatcher& dispatcher) override;
        eds::server_new::features::runtime::FeatureDispatchResult dispatch(
            const eds::server_new::features::runtime::FeatureDispatchRequest& request,
            core::runtime::MessageDispatcher& dispatcher) override;

    private:
        bool onInitialize() override;
        void onShutdown() override;

    private:
        bool registered_ = false;
        std::shared_ptr<DirectChatStateStore> stateStore_;
        std::shared_ptr<DirectChatFeatureManager> manager_;
    };

} // namespace eds::server_new::features::direct_chat