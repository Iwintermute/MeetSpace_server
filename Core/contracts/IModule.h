#pragma once

#include "contracts/Primitives.h"
#include <string>

namespace core::contracts {

class IModule {
public:
    virtual ~IModule() = default;

    virtual ModuleId getId() const = 0;
    virtual std::string getName() const = 0;

    virtual bool initialize() = 0;
    virtual void shutdown() = 0;

    virtual bool isEnabled() const = 0;
    virtual void setEnabled(bool enabled) = 0;
    virtual LifecycleState state() const = 0;
};

} // namespace core::contracts
