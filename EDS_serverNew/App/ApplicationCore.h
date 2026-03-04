#pragma once

#include "contracts/IModuleRegistry.h"
#include <memory>

class ApplicationApi{
public:
    explicit ApplicationApi(std::shared_ptr<core::contracts::IModuleRegistry> registry = nullptr);
public:
    bool init();

    bool start();

private:
    std::shared_ptr<core::contracts::IModuleRegistry> CoreRegistry;//Реестр модулей
};
