#pragma once
#include "interfaces/iModule.h"
#include "interfaces/iAgentManager.h"

#include <nlohmann/json.hpp>
#include "App/wsFormat.h"

#include <unordered_map>
#include <functional>
#include <string>
#include <memory>

//Должен маршрутизировать запросы от менеджеров и регулировать действия, реализовывать логику проекта
class iAgent {
public:

    using tAgentFactory = std::function<std::unique_ptr<iAgent>()>;

    virtual void registerAgent(const std::string& type, tAgentFactory factory) = 0;  // Динамическая регистрация
    virtual void unregisterAgent(const std::string& type) = 0;  // Для отключения
    virtual bool handleMessage(const StandardWsMessage& msg) = 0;  // Делегирует в Agent

protected:
    std::shared_ptr<iAgentManager> managerKnowledge;

    std::unordered_map<std::string, tAgentFactory> Actions;
};