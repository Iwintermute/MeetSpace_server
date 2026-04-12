#pragma once
#include "features/runtime/FeatureRegistry.h"

#include "contracts/IModuleRegistry.h"
#include "contracts/Primitives.h"
#include "runtime/MessageDispatcher.h"
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

class ApplicationApi{
public:
    explicit ApplicationApi(std::shared_ptr<core::contracts::IModuleRegistry> registry = nullptr);
public:
    bool init();

    bool start();

    core::contracts::OperationStatus registerFeatures();
    eds::server_new::features::runtime::FeatureDispatchResult dispatchFeature(
        eds::server_new::features::runtime::FeatureDispatchRequest request);
    void notifyFeatureSessionDisconnected(const std::string& peerId, std::uintptr_t sessionHandle);

private:
    std::shared_ptr<core::contracts::IModuleRegistry> CoreRegistry;//Реестр модулей
    core::runtime::MessageDispatcher dispatcher_;
    std::unordered_map<std::string, eds::server_new::features::runtime::IFeatureModule*> featureModules_;
    std::unordered_map<std::string, core::contracts::ModuleId> featureModuleIds_;
    mutable std::mutex featuresMutex_;
    bool featuresRegistered_ = false;
};
