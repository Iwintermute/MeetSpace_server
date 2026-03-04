#pragma once

#include "contracts/IModule.h"
#include <string>

class iModule : public core::contracts::IModule {
public:
    using ModuleId = core::contracts::ModuleId;
    using LifecycleState = core::contracts::LifecycleState;
    virtual ~iModule() = default;
    virtual ModuleId getId() const = 0;
    virtual std::string getName() const = 0;

    // Жизненный цикл
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;

    // Статус
    virtual bool isEnabled() const = 0;
    virtual void setEnabled(bool enabled) = 0;

    LifecycleState state() const override {
        return LifecycleState::Running;
    }
};