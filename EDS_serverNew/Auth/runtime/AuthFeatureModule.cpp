#include "Auth/runtime/AuthFeatureModule.h"
#include "Auth/runtime/AuthCommand.h"
#include "infrastructure/control_plane/runtime/ControlPlaneServices.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>

#include <utility>

namespace eds::server_new::features::auth {
    namespace {
        std::string trim(std::string value) {
            const auto first = value.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) {
                return {};
            }

            const auto last = value.find_last_not_of(" \t\r\n");
            return value.substr(first, last - first + 1);
        }

        bool isPlaceholderToken(const std::string& value) {
            if (value.empty()) {
                return false;
            }

            std::string lowered = value;
            std::transform(
                lowered.begin(),
                lowered.end(),
                lowered.begin(),
                [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

            return lowered == "null" || lowered == "undefined";
        }
    } // namespace

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

        auto repository = eds::server_new::control_plane::ControlPlaneServices::repository();
        if (!repository || !repository->isReady()) {
            result.status = core::contracts::OperationStatus::failure("Control-plane repository is not configured.");
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
            static_cast<void>(repository->markRealtimeSessionDisconnected(request.sessionHandle, request.peerId));
            sessionStore_->unbind(request.sessionHandle);
            result.status = core::contracts::OperationStatus::success("Session logged out.");
            return result;
        }

        if (request.actionType != kActionBindSession &&
            request.actionType != kActionRestoreSession) {
            result.status = core::contracts::OperationStatus::failure("Unsupported auth action.");
            return result;
        }

        const auto accessToken = trim(request.context.value("accessToken", std::string{}));
        const auto deviceId = request.context.value("deviceId", std::string{});

        if (accessToken.empty()) {
            result.status = core::contracts::OperationStatus::failure("accessToken must not be empty.");
            return result;
        }
        if (isPlaceholderToken(accessToken)) {
            result.status = core::contracts::OperationStatus::failure("accessToken has invalid placeholder value.");
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

        const auto mirrorStatus = repository->mirrorRealtimeSession(session);
        if (!mirrorStatus.ok) {
            result.status = mirrorStatus;
            return result;
        }

        session.dbSessionId = mirrorStatus.data.value("sessionId", std::string{});
        session.dbConnectionId = mirrorStatus.data.value("connectionId", std::string{});

        sessionStore_->bind(std::move(session));

        nlohmann::json responseData = mirrorStatus.data;
        nlohmann::json reconnect = nlohmann::json::object();

        const auto conferencesStatus = repository->listUserConferences(verified->userId, 100);
        reconnect["conferences"] = conferencesStatus.ok
            ? conferencesStatus.data.value("conferences", nlohmann::json::array())
            : nlohmann::json::array();

        const auto directThreadsStatus = repository->listDirectThreads(verified->userId, 100);
        reconnect["directThreads"] = directThreadsStatus.ok
            ? directThreadsStatus.data.value("threads", nlohmann::json::array())
            : nlohmann::json::array();

        const auto activeCallsStatus = repository->listUserActiveDirectCalls(verified->userId, 100);
        reconnect["activeDirectCalls"] = activeCallsStatus.ok
            ? activeCallsStatus.data.value("calls", nlohmann::json::array())
            : nlohmann::json::array();

        responseData["reconnect"] = std::move(reconnect);
        result.status = core::contracts::OperationStatus::success("Session bound.", std::move(responseData));
        return result;
    }

    bool AuthFeatureModule::onInitialize() {
        return true;
    }

    void AuthFeatureModule::onShutdown() {
    }

} // namespace eds::server_new::features::auth