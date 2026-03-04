#pragma once
#include "contracts/IMessage.h"
#include "contracts/Primitives.h"

#include "interfaces/iModule.h"

//Интерфейс действия
/*Пример: В верхнеуровневом менеджере регистрируются объекты реализации iAction, после чего он может дёргать их execute с параметрами
    
*/
class iAction : public iModule {
public:
    virtual ~iAction() = default;
    virtual core::contracts::OperationStatus execute(const core::contracts::IMessage& msg) = 0;
    virtual bool execute(const StandardWsMessage& msg) = 0;
};