#pragma once

#include "contracts/IMessage.h"
#include "contracts/Primitives.h"
#include "interfaces/iAgent.h"
#include "interfaces/iAction.h"

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace core::runtime {

// Потокобезопасный роутер действий внутри агента.
// Регистрирует фабрики действий и лениво создаёт action-instance при первом вызове.
class ActionRouter {
public:
    using ActionFactory = iAgent::tActionFactory;

    core::contracts::OperationStatus registerAction(std::string actionType, ActionFactory factory) {
        if (actionType.empty()) {
            return core::contracts::OperationStatus::failure("Action type must not be empty.");
        }
        if (!factory) {
            return core::contracts::OperationStatus::failure("Action factory must not be empty.");
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (actions_.find(actionType) != actions_.end()) {
            return core::contracts::OperationStatus::failure("Action already registered: " + actionType);
        }

        ActionEntry entry;
        entry.factory = std::move(factory);
        actions_.emplace(std::move(actionType), std::move(entry));
        return core::contracts::OperationStatus::success();
    }

    core::contracts::OperationStatus unregisterAction(std::string_view actionType) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto removed = actions_.erase(std::string(actionType));
        if (removed == 0) {
            return core::contracts::OperationStatus::failure("Action not found: " + std::string(actionType));
        }
        return core::contracts::OperationStatus::success();
    }

    core::contracts::OperationStatus dispatch(std::string_view actionType, const core::contracts::IMessage& message) {
        iAction* action = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = actions_.find(std::string(actionType));
            if (it == actions_.end()) {
                return core::contracts::OperationStatus::failure("Action not registered: " + std::string(actionType));
            }

            if (!it->second.instance) {
                it->second.instance = it->second.factory();
                if (!it->second.instance) {
                    return core::contracts::OperationStatus::failure("Action factory returned nullptr: " + std::string(actionType));
                }
            }
            action = it->second.instance.get();
        }

        return action->execute(message);
    }

private:
    struct ActionEntry {
        ActionFactory factory;
        std::unique_ptr<iAction> instance;
    };

    std::unordered_map<std::string, ActionEntry> actions_;
    std::mutex mutex_;
};

} // namespace core::runtime
