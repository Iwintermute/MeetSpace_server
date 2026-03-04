#pragma once
#include "interfaces/iModule.h"
#include <atomic>
#include <cstdint>
#include <string>

class BaseModule : public virtual iModule {
    enum class ModuleStatus : uint8_t { SUCCESS, DISABLED, MODULE_ERROR };

protected:
    std::string name_;
    std::atomic<bool> enabled_;
    std::atomic<bool> initialized_;
    std::atomic<LifecycleState> lifecycleState_;
    ModuleId id_;

public:
    BaseModule(const std::string& name = "Dev-Name", const ModuleId& id = static_cast<ModuleId>(-1)/*, const std::string& version = "dev"*/)
        : id_(id),
        name_(name),
        enabled_(true),
        initialized_(false),
        lifecycleState_(LifecycleState::Created) {
    }

    virtual ~BaseModule() = default;

    // IModule implementation
    ModuleId getId() const override { return id_; }
    std::string getName() const override { return name_; }
    bool isEnabled() const override { return enabled_.load(); }
    void setEnabled(bool enabled) override { enabled_.store(enabled); }
    LifecycleState state() const override { return lifecycleState_.load(); }

    bool initialize() override {
        if (!enabled_ || initialized_) return false;
        lifecycleState_.store(LifecycleState::Initializing);

        bool result = onInitialize();
        if (result) {
            initialized_.store(true);
            lifecycleState_.store(LifecycleState::Running);
        } else {
            lifecycleState_.store(LifecycleState::Failed);
        }
        return result;
    }

    void shutdown() override {
        if (initialized_) {
            lifecycleState_.store(LifecycleState::Stopping);
            onShutdown();
            initialized_.store(false);
            lifecycleState_.store(LifecycleState::Stopped);
        }
    }

    friend class ModuleRegistry;

protected:
    // Для реализации в наследниках
    virtual bool onInitialize() = 0;
    virtual void onShutdown() = 0;

    // Вспомогательные методы
    bool isInitialized() const { return initialized_.load(); }

    void setId(ModuleId id) { id_ = id; }
};