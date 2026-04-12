#include "App/ApplicationCore.h"
#include "Auth/runtime/AuthCommand.h"
#include "Auth/runtime/AuthServices.h"
#include "features/runtime/FeatureModuleRegistration.h"

#include "managers/ModuleRegistry.h"
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
    : CoreRegistry(std::move(registry)) {
    if (!CoreRegistry) {
        CoreRegistry = ModuleRegistry::instance();
    }
}

core::contracts::OperationStatus ApplicationApi::registerFeatures() {
    std::lock_guard<std::mutex> lock(featuresMutex_);
    if (featuresRegistered_) {
        return core::contracts::OperationStatus::success();
    }

    static std::once_flag featureModulesRegistrationFlag;
    std::call_once(featureModulesRegistrationFlag, []() {
        eds::server_new::features::runtime::registerBuiltInFeatureModules(
            eds::server_new::features::runtime::FeatureRegistry::instance());
    });

    auto modules = eds::server_new::features::runtime::FeatureRegistry::instance().instantiateModules();
    if (modules.empty()) {
        return core::contracts::OperationStatus::failure("No feature modules were registered.");
    }
    if (!CoreRegistry) {
        return core::contracts::OperationStatus::failure("Module registry is not configured.");
    }

    for (auto& module : modules) {
        if (!module) {
            continue;
        }

        auto objectKey = std::string(module->objectType());
        if (objectKey.empty()) {
            return core::contracts::OperationStatus::failure("Feature module has empty objectType.");
        }
        if (featureModules_.find(objectKey) != featureModules_.end()) {
            return core::contracts::OperationStatus::failure("Duplicate feature module objectType: " + objectKey);
        }

        auto status = module->ensureRegistered(dispatcher_);
        if (!status.ok) {
            return status;
        }
        auto moduleId = CoreRegistry->registerModule(std::unique_ptr<core::contracts::IModule>(module.release()));
        if (moduleId == 0) {
            return core::contracts::OperationStatus::failure(
                "Failed to register feature module in module registry: " + objectKey);
        }

        auto* registeredModule = dynamic_cast<eds::server_new::features::runtime::IFeatureModule*>(CoreRegistry->getModule(moduleId));
        if (!registeredModule) {
            return core::contracts::OperationStatus::failure(
                "Registered module does not implement IFeatureModule: " + objectKey);
        }

        featureModules_.emplace(objectKey, registeredModule);
        featureModuleIds_.emplace(std::move(objectKey), moduleId);
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

    if (request.objectType.empty()) {
        result.status = core::contracts::OperationStatus::failure("object must not be empty.");
        return result;
    }
    if (request.actionType.empty()) {
        result.status = core::contracts::OperationStatus::failure("action must not be empty.");
        return result;
    }

    auto moduleIt = featureModules_.find(request.objectType);
    if (moduleIt == featureModules_.end()) {
        result.status = core::contracts::OperationStatus::failure("No feature module for object: " + request.objectType);
        return result;
    }
    if (request.agentType.empty()) {
        request.agentType = std::string(moduleIt->second->defaultAgent());
    }
    result.effectiveAgent = request.agentType;

    if (!request.context.is_object()) {
        result.status = core::contracts::OperationStatus::failure("ctx must be a JSON object.");
        return result;
    }

    std::string claimedPeer;
    const auto peerIt = request.context.find("peer");
    if (peerIt != request.context.end() && !peerIt->is_null()) {
        if (!peerIt->is_string()) {
            result.status = core::contracts::OperationStatus::failure("ctx.peer must be a string.");
            return result;
        }
        claimedPeer = peerIt->get<std::string>();
    }
    const auto peerIdIt = request.context.find("peerId");
    if (peerIdIt != request.context.end() && !peerIdIt->is_null()) {
        if (!peerIdIt->is_string()) {
            result.status = core::contracts::OperationStatus::failure("ctx.peerId must be a string.");
            return result;
        }
        const auto claimedPeerId = peerIdIt->get<std::string>();
        if (!claimedPeer.empty() && claimedPeer != claimedPeerId) {
            result.status = core::contracts::OperationStatus::failure("ctx.peer and ctx.peerId mismatch.");
            return result;
        }
        claimedPeer = claimedPeerId;
    }
    if (!claimedPeer.empty() && claimedPeer != request.peerId) {
        result.status = core::contracts::OperationStatus::failure("peer impersonation detected.");
        return result;
    }
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
    if (!CoreRegistry) {
        return false;
    }
    const auto featuresStatus = registerFeatures();
    if (!featuresStatus.ok) {
        return false;
    }
    return CoreRegistry->initializeAll();
}

bool ApplicationApi::start() {
    if (CoreRegistry == nullptr) {
        return false;
    }
    const auto registrationStatus = registerFeatures();
    return registrationStatus.ok;
}

void ApplicationApi::notifyFeatureSessionDisconnected(const std::string& peerId, std::uintptr_t sessionHandle) {
    auto sessionStore = eds::server_new::auth::AuthServices::sessionStore();
    if (sessionStore) {
        if (sessionHandle != 0) {
            sessionStore->unbind(sessionHandle);
        } else if (!peerId.empty()) {
            const auto boundHandle = sessionStore->resolvePeer(peerId);
            if (boundHandle.has_value()) {
                sessionStore->unbind(*boundHandle);
            }
        }
    }
    const auto registrationStatus = registerFeatures();
    if (!registrationStatus.ok) {
        return;
    }

    for (auto& [_, module] : featureModules_) {
        if (!module) {
            continue;
        }
        module->onSessionDisconnected(peerId, sessionHandle);
    }
}
