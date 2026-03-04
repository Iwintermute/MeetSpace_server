# Feature Development Guide (Core)
## Что такое feature в текущей модели
Feature = `FeatureManager + Agent + Action`.
- `FeatureManager` управляет агентами и выбирает нужного по `route.agent`.
- `Agent` управляет действиями и выбирает нужное по `route.action`.
- `Action` — минимальная единица бизнес-логики.

Рекомендуемая гранулярность: одно заметное действие — один класс/файл.
## Минимальный workflow
1. Создать класс feature-manager от `BaseFeatureManager`.
2. Создать агент от `BaseAgent`.
3. Создать одно или несколько действий от `BaseAction`.
4. Зарегистрировать agent factory в manager.
5. Зарегистрировать action factories в agent.
6. Зарегистрировать manager в `MessageDispatcher` по `route.object`.
## Пример каркаса
```cpp
class PingAction final : public BaseAction {
public:
    PingAction() : BaseAction("PingAction", static_cast<ModuleId>(-1)) {}

    core::contracts::OperationStatus execute(const core::contracts::IMessage& msg) override {
        // обработка payload
        return core::contracts::OperationStatus::success();
    }
};
```

```cpp
class SystemAgent final : public BaseAgent {
public:
    SystemAgent() : BaseAgent("SystemAgent") {
        registerAction("ping", [] { return std::make_unique<PingAction>(); });
    }
};
```

```cpp
class SystemFeatureManager final : public BaseFeatureManager {
public:
    SystemFeatureManager() : BaseFeatureManager("SystemFeature") {
        registerAgent("default", [] { return std::make_unique<SystemAgent>(); });
    }
};
```

```cpp
core::runtime::MessageDispatcher dispatcher;
auto manager = std::make_shared<SystemFeatureManager>();
dispatcher.registerManager("system", manager);
```
## Routing contract
Для корректного dispatch route должен быть заполнен:
- `object` обязательно
- `action` обязательно
- `agent` обязательно, если у manager больше одного агента

Если `agent` пуст и у manager ровно один агент, он выбирается автоматически.
## Ошибки и диагностика
Все операции возвращают `OperationStatus`:
- `ok = true` — успех
- `ok = false` и `message` — причина ошибки

Это позволяет прокидывать человекочитаемые ошибки в транспортный слой.
## Интеграция с JSON/WebSocket
Core не обязан знать JSON. Рекомендуемая схема:
1. В транспортном слое распарсить JSON.
2. Собрать `MessageRoute`.
3. Завернуть payload в `IMessage` (например `TypedMessage<T>` или `LegacyWsMessageAdapter`).
4. Передать в `MessageDispatcher`.
## Что делать дальше
Следующий этап эволюции:
- выделить `core-runtime` как отдельный артефакт/таргет в CMake
- добавить unit-тесты на `ActionRouter` и `MessageDispatcher`
- стандартизовать payload-модели (`TypedMessage<DomainCommand>`) для `EDS_serverNew`
