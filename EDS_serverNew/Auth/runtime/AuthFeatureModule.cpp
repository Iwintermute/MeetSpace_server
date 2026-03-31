#include "auth/runtime/AuthFeatureModule.h"
#include "auth/runtime/AuthCommand.h"

#include <utility>

namespace eds::server_new::features::auth {

    AuthFeatureModule::AuthFeatureModule(
        std::shared_ptr<eds::server_new::auth::SessionAuthStore> sessionStore,
        std::shared_ptr<eds::server_new::auth::ISupabaseAuthVerifier> verifier)
        : BaseModule("AuthFeatureModule", static_cast<core::contracts::ModuleId>(-1)),
        sessionStore_(std::move(sessionStore)),
        verifier_(std::move(verifier)) {
    }

    std::string_view AuthFeatureModule::objectType() const {
        return kAuthRouteObject;
    }

    std::string_view AuthFeatureModule::defaultAgent() const {
        return kAuthSessionAgent;
    }

    core::contracts::OperationStatus AuthFeatureModule::ensureRegistered(core::runtime::MessageDispatcher& dispatcher) {
        static_cast<void>(dispatcher);
        registered_ = true;
        return core::contracts::OperationStatus::success();
    }

    eds::server_new::features::runtime::FeatureDispatchResult AuthFeatureModule::dispatch(
        const eds::server_new::features::runtime::FeatureDispatchRequest& request,
        core::runtime::MessageDispatcher& dispatcher) {
        static_cast<void>(dispatcher);

        eds::server_new::features::runtime::FeatureDispatchResult result;
        result.status = ensureRegistered(dispatcher);
        if (!result.status.ok) {
            return result;
        }

        if (request.objectType != kAuthRouteObject) {
            result.status = core::contracts::OperationStatus::failure("Invalid auth object type.");
            return result;
        }

        if (request.agentType != kAuthSessionAgent) {
            result.status = core::contracts::OperationStatus::failure("Invalid auth agent type.");
            return result;
        }

        if (!sessionStore_ || !verifier_) {
            result.status = core::contracts::OperationStatus::failure("Auth module is not configured.");
            return result;
        }

        if (request.sessionHandle == 0) {
            result.status = core::contracts::OperationStatus::failure("sessionHandle must not be empty.");
            return result;
        }

        if (request.peerId.empty()) {
            result.status = core::contracts::OperationStatus::failure("trusted peer must not be empty.");
            return result;
        }

        if (request.actionType == kActionLogoutSession) {
            sessionStore_->unbind(request.sessionHandle);
            result.status = { true, "Session logged out." };
            return result;
        }

        if (request.actionType != kActionBindSession &&
            request.actionType != kActionRestoreSession) {
            result.status = core::contracts::OperationStatus::failure("Unsupported auth action.");
            return result;
        }

        const auto accessToken = request.context.value("accessToken", std::string{});
        const auto deviceId = request.context.value("deviceId", std::string{});

        if (accessToken.empty()) {
            result.status = core::contracts::OperationStatus::failure("accessToken must not be empty.");
            return result;
        }

        const auto verified = verifier_->verifyAccessToken(accessToken);
        if (!verified.has_value()) {
            result.status = core::contracts::OperationStatus::failure("Supabase access token is invalid.");
            return result;
        }

        eds::server_new::auth::AuthenticatedSession session;
        session.sessionHandle = request.sessionHandle;
        session.peerId = request.peerId;
        session.userId = verified->userId;
        session.email = verified->email;
        session.accessToken = accessToken;
        session.deviceId = deviceId;
        session.authenticated = true;

        sessionStore_->bind(std::move(session));
        result.status = { true, "Session bound." };
        return result;
    }

    bool AuthFeatureModule::onInitialize() {
        return true;
    }

    void AuthFeatureModule::onShutdown() {
    }

} // namespace eds::server_new::features::auth