#pragma once
#include "interfaces/iAction.h"
#include "App/wsFormat.h"

class BaseAction : public iAction {
public:
    BaseAction(const std::string& name, int id) {}
    virtual ~BaseAction() = default;

    // iAction override
    virtual bool execute(const StandardWsMessage& msg) override = 0;

    // Helper methods
    std::string getActionType() const { return m_actionType; }
    void setActionType(const std::string& type) { m_actionType = type; }

protected:
    std::string m_actionType; // e.g., "create", "join"
};

