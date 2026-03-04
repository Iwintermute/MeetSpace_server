#include "App/ApplicationCore.h"
#include "managers/ModuleRegistry.h"
#include <utility>

ApplicationApi::ApplicationApi(std::shared_ptr<core::contracts::IModuleRegistry> registry)
    : CoreRegistry(std::move(registry)) {
    if (!CoreRegistry) {
        CoreRegistry = ModuleRegistry::instance();
    }
}

bool ApplicationApi::init(){
    if (!CoreRegistry) {
        return false;
    }
    return CoreRegistry->initializeAll();
}

bool ApplicationApi::start(){
    return CoreRegistry != nullptr;
}

