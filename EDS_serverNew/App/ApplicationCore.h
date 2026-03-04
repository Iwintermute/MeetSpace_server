#pragma once

#include "managers/ModuleRegistry.h"
#include <memory>

class ApplicationApi{
public:
    ApplicationApi();
public:
    bool init();

    bool start();

private:
    std::shared_ptr<ModuleRegistry> CoreRegistry;//Реестр модулей
};