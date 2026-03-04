#pragma once
#include "contracts/IMessage.h"
#include "contracts/Primitives.h"
#include "interfaces/iAction.h"
#include "interfaces/iModule.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

class iAgentManager;

//Должен маршрутизировать запросы от менеджеров и регулировать действия, реализовывать логику проекта
class iAgent {
public:

    using tActionFactory = std::function<std::unique_ptr<iAction>()>;

    virtual core::contracts::OperationStatus registerAction(std::string type, tActionFactory factory) = 0;  // Динамическая регистрация
    virtual core::contracts::OperationStatus unregisterAction(std::string_view type) = 0;  // Для отключения
    virtual core::contracts::OperationStatus handleMessage(const core::contracts::IMessage& msg) = 0;  // Делегирует в Action

protected:
    std::shared_ptr<iAgentManager> managerKnowledge;
    std::unordered_map<std::string, tActionFactory> Actions;
    std::unordered_map<std::string, tAgentFactory> Actions;
};