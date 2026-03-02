#include "Core.h"
#include <iostream>

Core::Core() 
    : eventBus(nullptr), 
      moduleRegistry(nullptr), 
      initialized(false) {
    // Ядро создано, но не инициализировано
}

std::shared_ptr<Core> Core::instance() {
    static std::shared_ptr<Core> inst(new Core);
    return inst;
}

bool Core::initialize() {
    if (initialized) {
        std::cerr << "[Core] Ядро уже инициализировано" << std::endl;
        return false;
    }

    try {
        // Получаем Singleton экземпляры компонентов
        eventBus = EventBus::instance();
        moduleRegistry = ModuleRegistry::instance();

        if (!eventBus) {
            std::cerr << "[Core] Ошибка: не удалось получить EventBus" << std::endl;
            return false;
        }

        if (!moduleRegistry) {
            std::cerr << "[Core] Ошибка: не удалось получить ModuleRegistry" << std::endl;
            return false;
        }

        initialized = true;
        std::cout << "[Core] Ядро успешно инициализировано" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Core] Исключение при инициализации: " << e.what() << std::endl;
        return false;
    }
}

bool Core::isInitialized() const {
    return initialized;
}

void Core::shutdown() {
    if (!initialized) {
        std::cerr << "[Core] Ядро еще не инициализировано" << std::endl;
        return;
    }

    try {
        // Выключаем модули
        shutdownModules();
        
        // Очищаем компоненты
        eventBus = nullptr;
        moduleRegistry = nullptr;
        initialized = false;
        
        std::cout << "[Core] Ядро успешно выключено" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[Core] Исключение при выключении: " << e.what() << std::endl;
    }
}

std::shared_ptr<EventBus> Core::getEventBus() const {
    if (!initialized) {
        std::cerr << "[Core] Предупреждение: явный запрос EventBus до инициализации ядра" << std::endl;
    }
    return eventBus;
}

std::shared_ptr<ModuleRegistry> Core::getModuleRegistry() const {
    if (!initialized) {
        std::cerr << "[Core] Предупреждение: явный запрос ModuleRegistry до инициализации ядра" << std::endl;
    }
    return moduleRegistry;
}

bool Core::initializeModules() {
    if (!initialized) {
        std::cerr << "[Core] Ошибка: ядро не инициализировано, невозможно инициализировать модули" << std::endl;
        return false;
    }

    if (!moduleRegistry) {
        std::cerr << "[Core] Ошибка: ModuleRegistry недоступен" << std::endl;
        return false;
    }

    try {
        std::cout << "[Core] Инициализация модулей..." << std::endl;
        bool result = moduleRegistry->initializeAll();
        if (result) {
            std::cout << "[Core] Все модули успешно инициализированы" << std::endl;
        } else {
            std::cerr << "[Core] Некоторые модули не удалось инициализировать" << std::endl;
        }
        return result;
    } catch (const std::exception& e) {
        std::cerr << "[Core] Исключение при инициализации модулей: " << e.what() << std::endl;
        return false;
    }
}

void Core::shutdownModules() {
    if (!initialized || !moduleRegistry) {
        return;
    }

    try {
        std::cout << "[Core] Выключение модулей..." << std::endl;
        moduleRegistry->shutdownAll();
        std::cout << "[Core] Все модули выключены" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[Core] Исключение при выключении модулей: " << e.what() << std::endl;
    }
}

std::string Core::getStatus() const {
    std::string status;
    
    status += "=== Статус ядра ===\n";
    status += "Инициализировано: " + std::string(initialized ? "да" : "нет") + "\n";
    
    if (initialized && moduleRegistry) {
        status += "Зарегистрировано модулей: " + std::to_string(moduleRegistry->size()) + "\n";
    }
    
    status += "EventBus: " + std::string(eventBus ? "доступна" : "не доступна") + "\n";
    status += "ModuleRegistry: " + std::string(moduleRegistry ? "доступна" : "не доступна") + "\n";
    
    return status;
}
