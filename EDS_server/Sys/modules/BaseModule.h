#pragma once
#include "IModule.h"
#include <atomic>
#include <string>

class BaseModule : public IModule {
    enum class ModuleStatus : uint8_t { SUCCESS, DISABLED, MODULE_ERROR };

protected:
    std::string name_;
    std::atomic<bool> enabled_;
    std::atomic<bool> initialized_;
    int id_;

public:
    BaseModule(const std::string& name = "Dev-Name", const int& id = -1/*, const std::string& version = "dev"*/)
        : id_(id),
        name_(name),
        enabled_(true),
        initialized_(false) {
    }

    virtual ~BaseModule() = default;

    // IModule implementation
    int getId() const override { return id_; }
    std::string getName() const override { return name_; }
    bool isEnabled() const override { return enabled_.load(); }
    void setEnabled(bool enabled) override { enabled_.store(enabled); }

    bool initialize() override {
        if (!enabled_ || initialized_) return false;

        bool result = onInitialize();
        if (result) {
            initialized_.store(true);
        }
        return result;
    }

    void shutdown() override {
        if (initialized_) {
            onShutdown();
            initialized_.store(false);
        }
    }

    friend class ModuleRegistry;

protected:
    // Для реализации в наследниках
    virtual bool onInitialize() = 0;
    virtual void onShutdown() = 0;

    // Вспомогательные методы
    bool isInitialized() const { return initialized_.load(); }

    void setId(int id) { id_ = id; }
};