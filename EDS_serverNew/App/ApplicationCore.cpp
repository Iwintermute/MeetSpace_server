#include "App/ApplicationCore.h"

#include "Auth/runtime/AuthCommand.h"
#include "Auth/runtime/AuthServices.h"
#include "features/runtime/FeatureModuleRegistration.h"
#include "infrastructure/control_plane/runtime/ControlPlaneServices.h"
#include "managers/ModuleRegistry.h"

#include <mutex>
#include <string_view>
#include <utility>

namespace {
    bool isAuthObject(std::string_view objectType) {
        return objectType == eds::server_new::features::auth::kAuthRouteObject;
    }

    core::contracts::OperationStatus requireAuthenticatedSession(
        const eds::server_new::features::runtime::FeatureDispatchRequest& request) {
        if (isAuthObject(request.objectType)) {
            return core::contracts::OperationStatus::success();
        }

        auto sessionStore = eds::server_new::auth::AuthServices::sessionStore();
        if (!sessionStore) {
            return core::contracts::OperationStatus::failure("Auth session store is not configured.");
        }

        const auto authSession = sessionStore->get(request.sessionHandle);
        if (!authSession.has_value() || !authSession->authenticated) {
            return core::contracts::OperationStatus::failure(
                "Unauthorized. Call auth.bind_session before non-auth actions.");
        }
        if (authSession->peerId != request.peerId) {
            return core::contracts::OperationStatus::failure("Auth session peer mismatch.");
        }
        return core::contracts::OperationStatus::success();
    }
} // namespace

ApplicationApi::ApplicationApi(std::shared_ptr<core::contracts::IModuleRegistry> registry)
    : coreRegistry_(std::move(registry)) {
    if (!coreRegistry_) {
        coreRegistry_ = ModuleRegistry::instance();
    }
}

core::contracts::OperationStatus ApplicationApi::registerFeatures() {
    std::lock_guard<std::mutex> lock(featuresMutex_);
    if (featuresRegistered_) {
        return core::contracts::OperationStatus::success();
    }

    static std::once_flag onceFlag;
    std::call_once(onceFlag, []() {
        eds::server_new::features::runtime::registerBuiltInFeatureModules(
            eds::server_new::features::runtime::FeatureRegistry::instance());
        });

    auto modules = eds::server_new::features::runtime::FeatureRegistry::instance().instantiateModules();
    if (modules.empty()) {
        return core::contracts::OperationStatus::failure("No feature modules were registered.");
    }

    for (auto& module : modules) {
        if (!module) {
            continue;
        }

        const auto objectKey = std::string(module->objectType());
        if (featureModules_.find(objectKey) != featureModules_.end()) {
            return core::contracts::OperationStatus::failure("Duplicate feature module objectType: " + objectKey);
        }

        auto status = module->ensureRegistered(dispatcher_);
        if (!status.ok) {
            return status;
        }

        const auto moduleId =
            coreRegistry_->registerModule(std::unique_ptr<core::contracts::IModule>(module.release()));
        if (moduleId == 0) {
            return core::contracts::OperationStatus::failure("Failed to register feature module: " + objectKey);
        }

        auto* registeredModule =
            dynamic_cast<eds::server_new::features::runtime::IFeatureModule*>(coreRegistry_->getModule(moduleId));
        if (!registeredModule) {
            return core::contracts::OperationStatus::failure(
                "Registered module does not implement IFeatureModule: " + objectKey);
        }

        featureModules_.emplace(objectKey, registeredModule);
    }

    featuresRegistered_ = true;
    return core::contracts::OperationStatus::success();
}

eds::server_new::features::runtime::FeatureDispatchResult ApplicationApi::dispatchFeature(
    eds::server_new::features::runtime::FeatureDispatchRequest request) {
    eds::server_new::features::runtime::FeatureDispatchResult result;

    const auto registrationStatus = registerFeatures();
    if (!registrationStatus.ok) {
        result.status = registrationStatus;
        return result;
    }

    if (request.objectType.empty() || request.actionType.empty()) {
        result.status = core::contracts::OperationStatus::failure("object and action must not be empty.");
        return result;
    }

    const auto moduleIt = featureModules_.find(request.objectType);
    if (moduleIt == featureModules_.end()) {
        result.status = core::contracts::OperationStatus::failure("No feature module for object: " + request.objectType);
        return result;
    }

    if (request.agentType.empty()) {
        request.agentType = std::string(moduleIt->second->defaultAgent());
    }
    result.effectiveAgent = request.agentType;

    const auto authStatus = requireAuthenticatedSession(request);
    if (!authStatus.ok) {
        result.status = authStatus;
        return result;
    }

    auto moduleResult = moduleIt->second->dispatch(request, dispatcher_);
    if (moduleResult.effectiveAgent.empty()) {
        moduleResult.effectiveAgent = result.effectiveAgent;
    }
    return moduleResult;
}

bool ApplicationApi::init() {
    if (!coreRegistry_) {
        return false;
    }

    const auto status = registerFeatures();
    if (!status.ok) {
        return false;
    }

    return coreRegistry_->initializeAll();
}

bool ApplicationApi::start() {
    return registerFeatures().ok;
}

std::vector<nlohmann::json> ApplicationApi::notifyFeatureSessionDisconnected(
    const std::string& peerId,
    std::uintptr_t sessionHandle) {
    std::vector<nlohmann::json> outboundEvents;

    auto sessionStore = eds::server_new::auth::AuthServices::sessionStore();

    std::uintptr_t resolvedHandle = sessionHandle;
    if (sessionStore && resolvedHandle == 0 && !peerId.empty()) {
        const auto resolved = sessionStore->resolvePeer(peerId);
        if (resolved.has_value()) {
            resolvedHandle = *resolved;
        }
    }

    auto repository = eds::server_new::control_plane::ControlPlaneServices::repository();
    if (repository && repository->isReady() && !peerId.empty()) {
        static_cast<void>(repository->markRealtimeSessionDisconnected(resolvedHandle, peerId));
    }

    if (sessionStore) {
        if (resolvedHandle != 0) {
            sessionStore->unbind(resolvedHandle);
        }
        else if (!peerId.empty()) {
            sessionStore->unbindPeer(peerId);
        }
    }

    const auto status = registerFeatures();
    if (!status.ok) {
        return outboundEvents;
    }

    for (auto& [_, module] : featureModules_) {
        if (module) {
            module->onSessionDisconnected(peerId, resolvedHandle, outboundEvents);
        }
    }

    return outboundEvents;
}