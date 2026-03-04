#pragma once

#include "contracts/IMessage.h"
#include "contracts/Primitives.h"
#include "interfaces/iModule.h"

// Интерфейс действия.
// Действие исполняется на основе контекстного сообщения и возвращает статус выполнения.
class iAction : public virtual iModule {
public:
    virtual ~iAction() = default;
    virtual core::contracts::OperationStatus execute(const core::contracts::IMessage& msg) = 0;
};
