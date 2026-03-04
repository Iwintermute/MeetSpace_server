#pragma once
#include "contracts/IMessage.h"
#include "contracts/Primitives.h"
#include "interfaces/iModule.h"

#include <unordered_map>
#include <functional>
#include <string>
#include <memory>
#include <string_view>

class iAgent;

//Должен выполнять роль объекта-фитчи
class iAgentManager : public virtual iModule{
public:
    using tAgentFactory = std::function<std::unique_ptr<iAgent>()>;

    virtual core::contracts::OperationStatus registerAgent(std::string type, tAgentFactory factory) = 0;  // Динамическая регистрация
    virtual core::contracts::OperationStatus unregisterAgent(std::string_view type) = 0;  // Для отключения
    virtual core::contracts::OperationStatus handleMessage(const core::contracts::IMessage& msg) = 0;  // Делегирует в Agent

protected:
    std::unordered_map<std::string, tAgentFactory> Agents;
};