#pragma once

#include "contracts/IModuleRegistry.h"
#include "contracts/Primitives.h"
#include "features/runtime/FeatureRegistry.h"
#include "runtime/MessageDispatcher.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class ApplicationApi {
public:
    explicit ApplicationApi(std::shared_ptr<core::contracts::IModuleRegistry> registry = nullptr);

    bool init();
    bool start();

    core::contracts::OperationStatus registerFeatures();
    eds::server_new::features::runtime::FeatureDispatchResult dispatchFeature(
        eds::server_new::features::runtime::FeatureDispatchRequest request);

    std::vector<nlohmann::json> notifyFeatureSessionDisconnected(
        const std::string& peerId,
        std::uintptr_t sessionHandle);

private:
    std::shared_ptr<core::contracts::IModuleRegistry> coreRegistry_;
    core::runtime::MessageDispatcher dispatcher_;
    std::unordered_map<std::string, eds::server_new::features::runtime::IFeatureModule*> featureModules_;
    mutable std::mutex featuresMutex_;
    bool featuresRegistered_ = false;
};