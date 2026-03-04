#pragma once

#include "contracts/IModuleRegistry.h"
#include "interfaces/iModule.h"
#include "modules/BaseModule.h"

#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

/*
# ModuleManager
    Управляет жизненным циклом модулей, сохраняет их атрибуты и позволяет иметь модульную структуру.

    Глобальный реестр: используйте instance для получения ссылки на статический экземпляр.

*/
class ModuleRegistry : public core::contracts::IModuleRegistry {
private:
    using ModuleId = core::contracts::ModuleId;

    std::unordered_map<ModuleId, std::unique_ptr<iModule>> modules_;
    ModuleId nextId_ = 1;
    mutable std::mutex mutex_;

    ModuleId generateId() {
        std::lock_guard<std::mutex> lock(mutex_);
        return nextId_++;
    }

    void setModuleId(iModule* module, ModuleId id) {
        if (auto* baseModule = dynamic_cast<BaseModule*>(module)) {
            baseModule->setId(id);
        }
    }

public:
    virtual ~ModuleRegistry() = default;

    static std::shared_ptr<ModuleRegistry> instance() {
        static std::shared_ptr<ModuleRegistry> inst(new ModuleRegistry);
        return inst;
    }

    ModuleId registerModule(std::unique_ptr<core::contracts::IModule> module) override {
        if (!module) {
            return 0;
        }

        auto* legacyModule = dynamic_cast<iModule*>(module.get());
        if (!legacyModule) {
            throw std::runtime_error("ModuleRegistry accepts only iModule-compatible modules.");
        }

        ModuleId id = generateId();
        setModuleId(legacyModule, id);

        if (modules_.find(id) != modules_.end()) {
            throw std::runtime_error("Internal error: generated duplicate id " + std::to_string(id));
        }

        module.release();
        modules_[id] = std::unique_ptr<iModule>(legacyModule);
        return id;
    }

    std::vector<ModuleId> registerModules(std::vector<std::unique_ptr<core::contracts::IModule>> modules) override {
        std::vector<ModuleId> ids;
        ids.reserve(modules.size());
        for (auto& module : modules) {
            ids.push_back(registerModule(std::move(module)));
        }
        return ids;
    }

    template<typename T, typename... Args>
    T* registerModule(Args&&... args) {
        auto module = std::make_unique<T>(std::forward<Args>(args)...);
        ModuleId id = generateId();

        setModuleId(module.get(), id);

        if (modules_.find(id) != modules_.end()) {
            throw std::runtime_error("Internal error: generated duplicate id " + std::to_string(id));
        }

        T* ptr = module.get();
        modules_[id] = std::move(module);
        return ptr;
    }

    iModule* getModule(ModuleId id) override {
        auto it = modules_.find(id);
        return it != modules_.end() ? it->second.get() : nullptr;
    }

    const iModule* getModule(ModuleId id) const override {
        auto it = modules_.find(id);
        return it != modules_.end() ? it->second.get() : nullptr;
    }

    template<typename T>
    T* getModuleAs(const ModuleId& id) {
        return dynamic_cast<T*>(getModule(id));
    }

    bool initializeAll() override {
        std::lock_guard<std::mutex> lock(mutex_);
        bool all_ok = true;
        for (auto& [id, module] : modules_) {
            if (module->isEnabled()) {
                if (!module->initialize()) {
                    std::cerr << "Failed to initialize module: " << id << std::endl;
                    all_ok = false;
                }
            }
        }
        return all_ok;
    }

    void shutdownAll() override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [id, module] : modules_) {
            if (module->isEnabled()) {
                module->shutdown();
            }
        }
    }

    std::vector<core::contracts::ModuleSnapshot> snapshots() const override {
        std::vector<core::contracts::ModuleSnapshot> result;
        result.reserve(modules_.size());
        for (const auto& [id, module] : modules_) {
            core::contracts::ModuleSnapshot snapshot;
            snapshot.id = id;
            snapshot.name = module->getName();
            snapshot.state = module->state();
            snapshot.enabled = module->isEnabled();
            result.push_back(std::move(snapshot));
        }
        return result;
    }

    std::vector<ModuleId> getModuleIds() const {
        std::vector<ModuleId> ids;
        ids.reserve(modules_.size());
        for (const auto& [id, module] : modules_) {
            ids.push_back(id);
        }
        return ids;
    }

    size_t size() const {
        return modules_.size();
    }

    bool empty() const {
        return modules_.empty();
    }
};
