#pragma once

#include "contracts/Primitives.h"
#include "interfaces/iAgent.h"
#include "modules/BaseModule.h"
#include "runtime/ActionRouter.h"
#include "runtime/MessageDispatcher.h"

#include <string>
#include <string_view>
#include <utility>

// Базовая реализация агента:
// - хранит и маршрутизирует действия через ActionRouter
// - принимает RoutedMessageEnvelope и делегирует payload в выбранное action
class BaseAgent : public BaseModule, public iAgent {
public:
    BaseAgent(const std::string& name, ModuleId id = static_cast<ModuleId>(-1))
        : BaseModule(name, id) {
    }

    core::contracts::OperationStatus registerAction(std::string type, tActionFactory factory) override {
        return actionRouter_.registerAction(std::move(type), std::move(factory));
    }

    core::contracts::OperationStatus unregisterAction(std::string_view type) override {
        return actionRouter_.unregisterAction(type);
    }

    core::contracts::OperationStatus handleMessage(const core::contracts::IMessage& msg) override {
        const auto* envelope = dynamic_cast<const core::runtime::RoutedMessageEnvelope*>(&msg);
        if (!envelope) {
            return core::contracts::OperationStatus::failure("BaseAgent expects RoutedMessageEnvelope.");
        }

        const auto& route = envelope->route();
        if (route.action.empty()) {
            return core::contracts::OperationStatus::failure("Route action must not be empty.");
        }

        return actionRouter_.dispatch(route.action, envelope->payload());
    }

protected:
    bool onInitialize() override {
        return true;
    }

    void onShutdown() override {
    }

    core::runtime::ActionRouter& actionRouter() {
        return actionRouter_;
    }

private:
    core::runtime::ActionRouter actionRouter_;
};
