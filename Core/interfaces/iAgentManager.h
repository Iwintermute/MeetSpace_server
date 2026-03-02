#pragma once
#include "interfaces/iModule.h"
#include "interfaces/iAgent.h"

#include <nlohmann/json.hpp>
#include "App/wsFormat.h"

#include <unordered_map>
#include <functional>
#include <string>
#include <memory>

//Должен выполнять роль объекта-фитчи
class iAgentManager : public iModule{
public:
    using tAgentFactory = std::function<std::unique_ptr<iAgent>()>;

    virtual void registerAgent(const std::string& type, tAgentFactory factory) = 0;  // Динамическая регистрация
    virtual void unregisterAgent(const std::string& type) = 0;  // Для отключения
    virtual bool handleMessage(const StandardWsMessage& msg) = 0;  // Делегирует в Agent

protected:
    std::unordered_map<std::string, tAgentFactory> Agents;
};