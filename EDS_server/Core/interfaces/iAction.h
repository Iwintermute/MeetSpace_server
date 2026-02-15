#pragma once

#include <functional>

namespace Core::interfaces{
    //Интерфейс действия
    /*Пример: 
    
    */
    class iWsAction : public IModule{
        virtual ~iWsAction() = default;
        virtual void execute(const nlohmann::json& msg, void* session) = 0;
    }
}