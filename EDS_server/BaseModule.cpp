#include "modules/BaseModule.h"

void BaseModule::shutdown() {
    if (initialized_) {
        onShutdown();
        initialized_.store(false);
    }
}

bool BaseModule::initialize() {
    if (!enabled_ || initialized_) return false;

    bool result = onInitialize();
    if (result) {
        initialized_.store(true);
    }
    return result;
}