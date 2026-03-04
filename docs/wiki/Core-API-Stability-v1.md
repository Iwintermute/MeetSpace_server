# Core API Stability v1
## Назначение
Этот документ фиксирует публичный API Core версии v1 и правила совместимости.
Цель: сделать развитие ядра предсказуемым и безопасным для feature-команд и внешних интеграций.
## Scope v1
Под “Core API v1” понимаются только публичные контракты и базовые абстракции:
- `Core/contracts/*`:
  - `IModule`, `IModuleRegistry`
  - `IMessage`, `TypedMessage`, `RoutedMessage`, `MessageRoute`
  - `IAction`, `IAgent`, `IFeatureManager`
  - `OperationStatus`, `LifecycleState`
- `Core/interfaces/*`:
  - `iModule`, `iAction`, `iAgent`, `iAgentManager`
- `Core/runtime/*`:
  - `ActionRouter`
  - `MessageDispatcher`
  - `RoutedMessageEnvelope`
- `Core/modules/*`:
  - `BaseModule`
  - `BaseAction`
  - `BaseAgent`
  - `BaseFeatureManager`
## Что считается публичным контрактом
Публичным контрактом считаются:
- имена публичных типов/классов/методов;
- сигнатуры публичных методов;
- семантика `OperationStatus` (`ok` + `message`);
- семантика route (`object`, `agent`, `action`);
- ожидаемое поведение lifecycle (`Created/Initializing/Running/Stopping/Stopped/Failed`).
## Breaking changes (запрещены в v1 без смены версии)
Breaking change — это любое изменение, которое ломает существующий код потребителей Core:
- удаление или переименование публичного класса/метода/поля;
- изменение сигнатуры (тип параметра, возвращаемый тип, const-qualifier и т.д.);
- изменение обязательности route-полей без fallback-совместимости;
- изменение семантики `OperationStatus` и/или lifecycle-state;
- изменение поведения базовых классов, из-за которого существующие feature перестают работать.
## Non-breaking changes (допустимы в v1)
- добавление новых классов/утилит без изменения существующих контрактов;
- добавление новых методов при сохранении старых и их поведения;
- расширение диагностики в `message` при сохранении `ok`-семантики;
- внутренние оптимизации без изменения внешнего поведения.
## Политика эволюции API
1. Сначала добавить новую возможность в back-compatible виде.
2. Для устаревающих методов использовать мягкую депрекацию:
   - документация с пометкой `Deprecated`;
   - переходный период минимум 1 минорный релиз.
3. Удаление/жёсткое изменение — только в следующей major-версии API.
## Политика для adapters и transport
`Core/adapters` и любой transport-layer (`ws/http/mediasoup`) не входят в стабильное API v1.
Их можно развивать быстрее, если они не меняют контракты `Core/contracts`.
## Минимальные правила совместимости для feature-команд
Если feature написана через `BaseFeatureManager -> BaseAgent -> BaseAction`, то Core v1 гарантирует:
- корректную маршрутизацию по `object/agent/action`;
- стабильный контракт входного сообщения через `IMessage`;
- стабильный формат результата через `OperationStatus`.
## Проверка совместимости при изменениях Core
Перед мерджем изменений в Core:
1. Проверить, не изменились ли сигнатуры публичных API из Scope v1.
2. Проверить сборку минимум двух таргетов:
   - `eduspace_server`
   - `eds_server_new_arch`
3. Обновить wiki-документацию, если поведение расширено.
