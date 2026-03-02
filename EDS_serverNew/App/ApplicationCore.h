#include "Core/managers/ModuleRegistry.h"

class ApplicationApi{
public:
    ApplicationApi();
public:
    bool init();

    bool start();

private:
    std::shared_ptr<ModuleRegistry> CoreRegistry;//Реестр модулей
};