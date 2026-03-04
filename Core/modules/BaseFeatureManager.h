#pragma once

#include "contracts/Primitives.h"
#include "interfaces/iAgent.h"
#include "interfaces/iAgentManager.h"
#include "modules/BaseModule.h"
#include "runtime/MessageDispatcher.h"

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

// Базовая реализация feature-manager:
// - регистрирует фабрики агентов
// - лениво создаёт агента при первом сообщении
// - маршрутизирует RoutedMessageEnvelope к нужному агенту
class BaseFeatureManager : public BaseModule, public iAgentManager {
public:
    BaseFeatureManager(const std::string& name, ModuleId id = static_cast<ModuleId>(-1))
        : BaseModule(name, id) {
    }

    core::contracts::OperationStatus registerAgent(std::string type, tAgentFactory factory) override {
        if (type.empty()) {
            return core::contracts::OperationStatus::failure("Agent type must not be empty.");
        }
        if (!factory) {
            return core::contracts::OperationStatus::failure("Agent factory must not be empty.");
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (Agents.find(type) != Agents.end()) {
            return core::contracts::OperationStatus::failure("Agent already registered: " + type);
        }

        Agents.emplace(std::move(type), std::move(factory));
        return core::contracts::OperationStatus::success();
    }

    core::contracts::OperationStatus unregisterAgent(std::string_view type) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto key = std::string(type);
        const auto removedFactory = Agents.erase(key);
        agentInstances_.erase(key);

        if (removedFactory == 0) {
            return core::contracts::OperationStatus::failure("Agent not found: " + key);
        }
        return core::contracts::OperationStatus::success();
    }

    core::contracts::OperationStatus handleMessage(const core::contracts::IMessage& msg) override {
        const auto* envelope = dynamic_cast<const core::runtime::RoutedMessageEnvelope*>(&msg);
        if (!envelope) {
            return core::contracts::OperationStatus::failure("BaseFeatureManager expects RoutedMessageEnvelope.");
        }

        const auto& route = envelope->route();

        iAgent* agent = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);

            std::string agentKey = route.agent;
            if (agentKey.empty()) {
                if (Agents.empty()) {
                    return core::contracts::OperationStatus::failure("No agents registered.");
                }
                if (Agents.size() > 1) {
                    return core::contracts::OperationStatus::failure("Route agent is empty and manager has multiple agents.");
                }
                agentKey = Agents.begin()->first;
            }

            auto it = agentInstances_.find(agentKey);
            if (it == agentInstances_.end()) {
                auto fit = Agents.find(agentKey);
                if (fit == Agents.end()) {
                    return core::contracts::OperationStatus::failure("Agent factory not found: " + agentKey);
                }

                auto instance = fit->second();
                if (!instance) {
                    return core::contracts::OperationStatus::failure("Agent factory returned nullptr: " + agentKey);
                }
                it = agentInstances_.emplace(std::move(agentKey), std::move(instance)).first;
            }

            agent = it->second.get();
        }

        return agent->handleMessage(msg);
    }

protected:
    bool onInitialize() override {
        return true;
    }

    void onShutdown() override {
        std::lock_guard<std::mutex> lock(mutex_);
        agentInstances_.clear();
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::unique_ptr<iAgent>> agentInstances_;
};
