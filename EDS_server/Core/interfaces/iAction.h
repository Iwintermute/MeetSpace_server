#pragma once

#include "interfaces/iModule.h"
#include <functional>

#include <nlohmann/json.hpp>
#include "App/wsFormat.h"

//Интерфейс действия
/*Пример: В верхнеуровневом менеджере регистрируются объекты реализации iAction, после чего он может дёргать их execute с параметрами
    
*/
class iAction : public iModule {
public:
    virtual ~iAction() = default;

    virtual bool execute(const StandardWsMessage& msg) = 0;
};