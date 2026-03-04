#pragma once

#include <cstdint>
#include <string>
#include <utility>

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
    bool ok;
    std::string message;

    static OperationStatus success() {
        return { true, {} };
    }

    static OperationStatus failure(std::string error) {
        return { false, std::move(error) };
    }
};

struct ModuleSnapshot {
    ModuleId id = 0;
    std::string name;
    LifecycleState state = LifecycleState::Created;
    bool enabled = true;
};

} // namespace core::contracts
