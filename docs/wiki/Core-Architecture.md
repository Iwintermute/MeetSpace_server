# EduSpace Core Architecture
## Цель
`Core` проектируется как переиспользуемое архитектурное ядро без привязки к конкретному транспорту (WebSocket, HTTP, mediasoup и т.д.) и без привязки к конкретному домену.
## Слои
### 1) `Core/contracts`
Стабильные контракты и базовые типы:
- `IModule`, `IModuleRegistry`
- `IMessage`, `TypedMessage`, `RoutedMessage`
- `IAction`, `IAgent`, `IFeatureManager`
- `OperationStatus`, `LifecycleState`, `MessageRoute`

Правило: в этом слое нет бизнес-логики, транспорта и сторонних систем.
### 2) `Core/interfaces`
Legacy-совместимый слой интерфейсов проекта:
- `iModule`, `iAction`, `iAgent`, `iAgentManager`

Этот слой уже переведён на контракты `IMessage/OperationStatus`, чтобы не зависеть от `App/wsFormat.h`.
### 3) `Core/runtime`
Инфраструктура маршрутизации:
- `ActionRouter` — регистрация/удаление/dispatch действий внутри агента
- `MessageDispatcher` — маршрутизация сообщений по `object -> manager`
- `RoutedMessageEnvelope` — envelope для передачи route + payload

Runtime не знает про JSON, websocket-фреймы и mediasoup API.
### 4) `Core/modules`
Базовые классы для быстрого расширения:
- `BaseModule` — lifecycle + id/name/enable state
- `BaseAction` — базовый action-класс
- `BaseAgent` — базовый агент с `ActionRouter`
- `BaseFeatureManager` — базовый feature-manager с lazy agent instantiation
## Маршрут сообщения
Маршрут задаётся через `MessageRoute`:
- `object` — целевой feature-manager
- `agent` — целевой агент внутри feature-manager
- `action` — действие внутри агента

Фактическая схема:
1. Внешний слой формирует `MessageRoute` и payload (`IMessage`).
2. `MessageDispatcher` ищет manager по `route.object`.
3. `BaseFeatureManager` ищет/создаёт agent по `route.agent`.
4. `BaseAgent` делегирует в `ActionRouter` по `route.action`.
5. `ActionRouter` вызывает `iAction::execute(payload)`.
## Legacy integration
Для старого websocket формата используется адаптер:
- `Core/adapters/LegacyWsMessageAdapter.h`

Идея: транспорт конвертирует вход в `IMessage`/envelope снаружи Core, а не внутри Core.
## Потокобезопасность
- `ActionRouter` использует внутренний mutex.
- `MessageDispatcher` использует внутренний mutex.
- `BaseFeatureManager` защищает фабрики/инстансы агентов mutex'ом.

Это даёт безопасную регистрацию и dispatch в многопоточной среде без внешних блокировок.
## Границы зависимостей
Допустимо:
- App/Transport -> Core/contracts
- App/Transport -> Core/runtime
- Feature модули -> Core/modules + Core/contracts

Недопустимо:
- Core/contracts -> App/*
- Core/runtime -> App/*
- Core/modules -> конкретный transport/api (например mediasoup websocket types)
