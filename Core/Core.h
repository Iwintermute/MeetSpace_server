#pragma once

#include <memory>
#include <managers/EventBus.h>
#include <managers/ModuleRegistry.h>

/**
 * @class Core
 * @brief Ядро приложения - стартовый скрипт для инициализации и управления базовыми компонентами
 * 
 * Предоставляет единый API для инициализации и доступа к:
 * - EventBus (шина событий для межкомпонентного взаимодействия)
 * - ModuleRegistry (реестр модулей для управления жизненным циклом)
 * 
 * Использует Singleton паттерн для гарантирования единственного экземпляра во всем приложении.
 */
class Core {
private:
    // Приватный конструктор для Singleton
    Core();

    // Удаляем копирование и перемещение
    Core(const Core&) = delete;
    Core& operator=(const Core&) = delete;
    Core(Core&&) = delete;
    Core& operator=(Core&&) = delete;

    // Компоненты ядра
    std::shared_ptr<EventBus> eventBus;
    std::shared_ptr<ModuleRegistry> moduleRegistry;
    bool initialized = false;

public:
    ~Core() = default;

    /**
     * @brief Получить единственный экземпляр Core (Singleton)
     * @return Ссылка на экземпляр Core
     */
    static std::shared_ptr<Core> instance();

    /**
     * @brief Инициализировать ядро (создать и подготовить компоненты)
     * @return true если инициализация успешна, false в противном случае
     */
    bool initialize();

    /**
     * @brief Проверить, инициализировано ли ядро
     * @return true если инициализировано, false иначе
     */
    bool isInitialized() const;

    /**
     * @brief Завершить работу ядра (отключить и очистить компоненты)
     */
    void shutdown();

    /**
     * @brief Получить EventBus (шину событий)
     * @return Ссылка на EventBus
     */
    std::shared_ptr<EventBus> getEventBus() const;

    /**
     * @brief Получить ModuleRegistry (реестр модулей)
     * @return Ссылка на ModuleRegistry
     */
    std::shared_ptr<ModuleRegistry> getModuleRegistry() const;

    /**
     * @brief Инициализировать все зарегистрированные модули
     * @return true если все модули инициализированы успешно
     */
    bool initializeModules();

    /**
     * @brief Завершить работу всех модулей
     */
    void shutdownModules();

    /**
     * @brief Получить состояние системы
     * @return Строка с информацией о состоянии
     */
    std::string getStatus() const;
};