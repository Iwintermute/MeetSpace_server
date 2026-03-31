#pragma once

#include "auth/ISupabaseAuthVerifier.h"
#include "auth/SessionAuthStore.h"
#include "features/runtime/FeatureRegistry.h"
#include "modules/BaseModule.h"

#include <memory>
#include <string_view>

namespace eds::server_new::features::auth {

    class AuthFeatureModule final
        : public BaseModule
        , public eds::server_new::features::runtime::IFeatureModule {
    public:
        AuthFeatureModule(
            std::shared_ptr<eds::server_new::auth::SessionAuthStore> sessionStore,
            std::shared_ptr<eds::server_new::auth::ISupabaseAuthVerifier> verifier);
        ~AuthFeatureModule() override = default;

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
        std::shared_ptr<eds::server_new::auth::SessionAuthStore> sessionStore_;
        std::shared_ptr<eds::server_new::auth::ISupabaseAuthVerifier> verifier_;
    };

} // namespace eds::server_new::features::auth