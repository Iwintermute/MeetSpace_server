#include "App/ApplicationCore.h"
#include "features/runtime/FeatureModuleRegistration.h"

#include "managers/ModuleRegistry.h"

#include <nlohmann/json.hpp>
#include <utility>

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

        featureModules_.emplace(std::move(objectKey), std::move(module));
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

    auto moduleResult = moduleIt->second->dispatch(request, dispatcher_);
    if (moduleResult.effectiveAgent.empty()) {
        moduleResult.effectiveAgent = result.effectiveAgent;
    }
    return moduleResult;
}

core::contracts::OperationStatus ApplicationApi::dispatchMediasoup(
    const core::contracts::MessageRoute& route,
    const eds::server_new::mediasoup::MediasoupCommand& command) {
    nlohmann::json context{
        { "peerId", command.peerId },
        { "roomId", command.roomId },
        { "transportId", command.transportId },
        { "producerId", command.producerId },
        { "kind", command.kind },
        { "sdp", command.sdp },
        { "sdpMid", command.sdpMid },
        { "candidate", command.candidate }
    };

    eds::server_new::features::runtime::FeatureDispatchRequest request;
    request.sessionHandle = command.sessionHandle;
    request.peerId = command.sessionId.empty() ? command.peerId : command.sessionId;
    request.objectType = route.object;
    request.agentType = route.agent;
    request.actionType = route.action;
    request.context = std::move(context);

    return dispatchFeature(std::move(request)).status;
}

core::contracts::OperationStatus ApplicationApi::dispatchConference(
    const core::contracts::MessageRoute& route,
    const eds::server_new::features::conference::ConferenceCommand& command) {
    nlohmann::json context{
        { "peerId", command.peerId },
        { "conferenceId", command.conferenceId },
        { "clientRequestId", command.clientRequestId },
        { "targetPeerId", command.targetPeerId }
    };

    eds::server_new::features::runtime::FeatureDispatchRequest request;
    request.sessionHandle = command.sessionHandle;
    request.peerId = command.sessionId.empty() ? command.peerId : command.sessionId;
    request.objectType = route.object;
    request.agentType = route.agent;
    request.actionType = route.action;
    request.context = std::move(context);

    return dispatchFeature(std::move(request)).status;
}

bool ApplicationApi::init() {
    if (!CoreRegistry) {
        return false;
    }

    if (!CoreRegistry->initializeAll()) {
        return false;
    }

    const auto featuresStatus = registerFeatures();
    return featuresStatus.ok;
}

bool ApplicationApi::start() {
    if (CoreRegistry == nullptr) {
        return false;
    }
    const auto registrationStatus = registerFeatures();
    return registrationStatus.ok;
}

void ApplicationApi::notifyFeatureSessionDisconnected(const std::string& peerId, std::uintptr_t sessionHandle) {
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
