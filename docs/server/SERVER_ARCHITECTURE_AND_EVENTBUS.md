# MeetSpace Server — архитектура, процессы, абстракции и event bus
Документ фиксирует архитектуру backend-части в `C:\diplom`, включая signaling, mediasoup, операционные сценарии и event-driven модель.

## 1. Общая картина
Основной сервер находится в:
- `apps/mediasoup-server/src/server.js`

Нагрузочные сценарии:
- `apps/mediasoup-server/scripts/stress-ws-signaling.js`
- `apps/mediasoup-server/scripts/stress-media-plane.js`
- `scripts/chat-heavy-load.py`
- `scripts/media-plane-10x10x10.py`

В `deploy/meetspace-server-31.177.83.146/...` находится зеркальная deploy-копия.

## 2. Архитектурные блоки backend
### 2.1 Network edge
- HTTP/HTTPS сервер (`http`/`https`).
- Upgrade в WebSocket через `ws`.
- health/metrics/audit endpoints.

### 2.2 Signaling dispatcher
- Входящие JSON-сообщения принимаются в per-socket queue.
- Выполняется backpressure и глобальные лимиты pending/inflight.
- Операции обрабатываются в `handleRequest`.
- Ответ формируется как `makeSuccess` или `makeFailure`.

### 2.3 Media control plane (mediasoup)
- Worker pool (`state.workerPool`).
- Router на room (`state.routers`).
- Transport/producers/consumers в `state.transports`, `state.producers`, `state.consumers`.
- Операции `opCreateWebRtcTransport`, `opProduce`, `opConsume`, `opResumeConsumer` и т.д.

### 2.4 Observability и control
- Metrics counters/gauges + Prometheus text endpoint.
- Audit-log кольцевой буфер.
- QoS loop с периодическим сбором media stats.
- Heartbeat loop для WebSocket.

## 3. Event bus на сервере
В сервере event bus реализован как композиция трёх потоков событий:

1) **Transport events**:
- WebSocket lifecycle (`connection`, `message`, `close`, `error`, `pong`).

2) **Operation events**:
- `handleRequest` -> `op*` handlers -> `makeSuccess/makeFailure`.
- Каждая операция фиксируется в metrics + audit.

3) **Runtime events**:
- события worker lifecycle (`died`), heartbeat timeouts, qos snapshots, shutdown lifecycle.

Итог: единая event-driven модель исполнения, где входящий signaling превращается в детерминированную операцию и обратный ответ.

## 4. Основные процессы
### 4.1 Join flow
1. `router.joinPeer` проверяет лимиты.
2. Создаётся/берётся room router.
3. Peer регистрируется в `roomPeers`.

### 4.2 Media publish flow
1. `router.createWebRtcTransport`.
2. `transport.connectDtls`.
3. `transport.produce`.
4. В state добавляется producer с привязкой к room/peer/transport.

### 4.3 Media consume flow
1. `transport.consume`.
2. Проверка `canConsume`.
3. Создание consumer в paused mode.
4. `consumer.resume`.

### 4.4 Reclaim/cleanup flow
- `router.leavePeer` / `router.closePeer` -> закрытие consumer/producer/transport.
- При пустой комнате вызывается `closeRoom`.
- При shutdown происходит draining + полная очистка ресурсов.

## 5. Абстракции и структуры состояния
Ключевые структуры состояния:
- `state.workerPool`
- `state.workerByRoom`
- `state.routers`
- `state.roomPeers`
- `state.transports`
- `state.producers`
- `state.consumers`
- `socketRuntimeState`

Ключевые категории функций:
- parse/normalize/validation helpers,
- queueing/backpressure helpers,
- op-handlers протокольных операций,
- lifecycle/observability/shutdown helpers.

Полный method-level reference:
- `docs/server/SERVER_METHOD_REFERENCE.md`

## 6. Масштабирование и capacity management
В коде уже есть ограничители:
- `MAX_ROOMS`
- `MAX_PEERS_PER_ROOM`
- `MAX_TOTAL_PEERS`
- `MAX_TRANSPORTS_PER_ROOM`
- `MAX_TOTAL_TRANSPORTS`
- `MAX_PRODUCERS_PER_ROOM`
- `MAX_TOTAL_PRODUCERS`
- `MAX_CONSUMERS_PER_ROOM`
- `MAX_TOTAL_CONSUMERS`
- `MAX_PEERS_PER_WORKER`
- `MAX_INFLIGHT_OPERATIONS`
- `MAX_GLOBAL_PENDING_MESSAGES`

Стратегия scale-out:
- горизонтальное масштабирование по регионам/инстансам;
- sticky routing по roomId;
- отдельные node-группы под signaling и media-heavy сценарии;
- внешняя централизованная метрика+алерты.

## 7. Безопасность
- TLS через cert/key/ca env.
- Опциональная token-auth проверка (`verifyAuthToken`).
- Ограничения на payload size и string length.
- Rate-limit per peer и перегрузочные отказы (`overloaded`).

## 8. Рекомендации для команды из 8 человек
Возможное разделение:
1. Signaling protocol + ops handlers.
2. Mediasoup worker/router lifecycle.
3. QoS/metrics/audit.
4. Security + auth.
5. Capacity/backpressure.
6. DevOps/deploy.
7. Stress/load tooling.
8. Integration with client and contract governance.

Для стабильной работы:
- versioned protocol contracts;
- обязательные нагрузочные прогоны перед релизом;
- эксплуатационные runbook и инцидентные процедуры.
