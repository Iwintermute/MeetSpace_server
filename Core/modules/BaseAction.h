#pragma once
#include "modules/BaseModule.h"
#include "interfaces/iAction.h"
#include "contracts/IMessage.h"
#include "contracts/Primitives.h"
class BaseAction : public BaseModule, public iAction {
class BaseAction : public iAction {
public:
    BaseAction(const std::string& name, ModuleId id)
        : BaseModule(name, id) {
    }
    virtual ~BaseAction() = default;

    // iAction override
    virtual core::contracts::OperationStatus execute(const core::contracts::IMessage& msg) override = 0;

    // Helper methods
    std::string getActionType() const { return m_actionType; }
    void setActionType(const std::string& type) { m_actionType = type; }

protected:
    std::string m_actionType; // e.g., "create", "join"
};

