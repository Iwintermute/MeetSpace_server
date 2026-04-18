#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <nlohmann/json.hpp>

namespace core::contracts {

    using ModuleId = std::uint64_t;
    using SubscriptionId = std::uint64_t;

    enum class LifecycleState : std::uint8_t {
        Created = 0,
        Registered,
        Initializing,
        Running,
        Stopping,
        Stopped,
        Failed
    };

    struct OperationStatus {
        bool ok = false;
        std::string message;
        nlohmann::json data = nlohmann::json::object();

        static OperationStatus success(
            std::string msg = {},
            nlohmann::json payload = nlohmann::json::object()) {
            return { true, std::move(msg), std::move(payload) };
        }

        static OperationStatus failure(
            std::string error,
            nlohmann::json payload = nlohmann::json::object()) {
            return { false, std::move(error), std::move(payload) };
        }
    };

    struct ModuleSnapshot {
        ModuleId id = 0;
        std::string name;
        LifecycleState state = LifecycleState::Created;
        bool enabled = true;
    };

} // namespace core::contracts