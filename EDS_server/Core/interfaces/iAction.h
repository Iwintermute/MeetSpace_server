#pragma once

#include "iModule.h"
#include <functional>

#include <nlohmann/json.hpp>
//Интерфейс действия
/*Пример: В верхнеуровневом менеджере регистрируются объекты реализации iAction, после чего он может дёргать их execute с параметрами
    
*/
class iAction : public iModule {
    virtual ~iAction() = default;

    virtual bool execute() = 0;
};