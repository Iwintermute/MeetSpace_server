#pragma once

#include "contracts/IMessage.h"
#include "contracts/Primitives.h"
#include "interfaces/iAction.h"
#include "interfaces/iModule.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>

class iAgentManager;

// Агент управляет набором действий и делегирует им входящие сообщения.
class iAgent : public virtual iModule {
public:
    using tActionFactory = std::function<std::unique_ptr<iAction>()>;

    virtual ~iAgent() = default;

    virtual core::contracts::OperationStatus registerAction(std::string type, tActionFactory factory) = 0;
    virtual core::contracts::OperationStatus unregisterAction(std::string_view type) = 0;
    virtual core::contracts::OperationStatus handleMessage(const core::contracts::IMessage& msg) = 0;

protected:
    std::shared_ptr<iAgentManager> managerKnowledge;
};
