#pragma once
#include "interfaces/iModule.h"
#include "interfaces/iAction.h"

#include <nlohmann/json.hpp>
#include "App/wsFormat.h"

#include <unordered_map>
#include <functional>
#include <string>
#include <memory>

struct ExecutionContext {
    void* wsSession{ nullptr };
    std::string peer;

};
//Должен выполнять роль объекта-фитчи
class iActionManager : public iModule{
public:
    using tActionFactory = std::function<std::unique_ptr<iAction>()>;

    virtual void registerAction(const std::string& type, tActionFactory factory) = 0;  // Динамическая регистрация
    virtual void unregisterAction(const std::string& type) = 0;  // Для отключения
    virtual bool handleMessage(const StandardWsMessage& msg) = 0;  // Делегирует в action

protected:
    std::unordered_map<std::string, tActionFactory> m_actions;
};