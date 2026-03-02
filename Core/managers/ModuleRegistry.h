#pragma once

#include "modules/BaseModule.h"
#include "interfaces/iModule.h"

#include <unordered_map>
#include <memory>
#include <vector>
#include <iostream>
#include <mutex>
#include <stdexcept>

/*
# ModuleManager
    Управляет жизненным циклом модулей, сохраняет их атрибуты и позволяет иметь модульную структуру.

    Глобальный реестр: используйте instance для получения ссылки на статический экземпляр.

*/
class ModuleRegistry {
private:

    std::unordered_map<int, std::unique_ptr<iModule>> modules_;
    int nextId_ = 1;
    std::mutex mutex_;

    int generateId() {
        std::lock_guard<std::mutex> lock(mutex_);
        return nextId_++;
    }

    void setModuleId(BaseModule* module, int id) {
        if (module) {
            module->setId(id);
        }
    }

public:
    static std::shared_ptr<ModuleRegistry> instance(){
        static std::shared_ptr<ModuleRegistry> inst(new ModuleRegistry);
        return inst;
    }

    template<typename T, typename... Args>
    T* registerModule(Args&&... args) {
        auto module = std::make_unique<T>(std::forward<Args>(args)...);
        int id = generateId();

        // Устанавливаем id в модуль
        setModuleId(module.get(), id);

        // Проверяем, не существует ли уже модуль с таким id
        if (modules_.find(id) != modules_.end()) {
            throw std::runtime_error("Internal error: generated duplicate id " + std::to_string(id));
        }

        T* ptr = module.get();
        modules_[id] = std::move(module);
        return ptr;
    }

    iModule* getModule(const int& id) {
        auto it = modules_.find(id);
        return it != modules_.end() ? it->second.get() : nullptr;
    }

    template<typename T>
    T* getModuleAs(const int& id) {
        return dynamic_cast<T*>(getModule(id));
    }

    bool initializeAll() {
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

    void shutdownAll() {
        for (auto& [id, module] : modules_) {
            if (module->isEnabled()) {
                module->shutdown();
            }
        }
    }

    std::vector<int> getModuleIds() const {
        std::vector<int> ids;
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