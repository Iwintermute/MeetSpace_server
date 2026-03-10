#pragma once

#include "contracts/Primitives.h"
#include "runtime/MessageDispatcher.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eds::server_new::features::runtime {

struct FeatureDispatchRequest {
    std::uintptr_t sessionHandle = 0;
    std::string peerId;
    std::string objectType;
    std::string agentType;
    std::string actionType;
    nlohmann::json context = nlohmann::json::object();
};

struct FeatureDispatchResult {
    core::contracts::OperationStatus status = core::contracts::OperationStatus::success();
    std::string effectiveAgent;
    std::vector<nlohmann::json> outboundEvents;
};

class IFeatureModule {
public:
    virtual ~IFeatureModule() = default;

    virtual std::string_view objectType() const = 0;
    virtual std::string_view defaultAgent() const = 0;
    virtual core::contracts::OperationStatus ensureRegistered(core::runtime::MessageDispatcher& dispatcher) = 0;
    virtual FeatureDispatchResult dispatch(
        const FeatureDispatchRequest& request,
        core::runtime::MessageDispatcher& dispatcher) = 0;
    virtual void onSessionDisconnected(std::string_view peerId, std::uintptr_t sessionHandle) {
        static_cast<void>(peerId);
        static_cast<void>(sessionHandle);
    }
};

class FeatureRegistry {
public:
    using tFactory = std::function<std::unique_ptr<IFeatureModule>()>;

    static FeatureRegistry& instance();

    void addFactory(tFactory factory);
    std::vector<std::unique_ptr<IFeatureModule>> instantiateModules() const;

private:
    FeatureRegistry() = default;

private:
    mutable std::mutex mutex_;
    std::vector<tFactory> factories_;
};

} // namespace eds::server_new::features::runtime
