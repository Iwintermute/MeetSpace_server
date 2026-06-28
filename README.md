# MeetSpace Server
Серверная часть MeetSpace для signaling и media-control plane на базе `mediasoup`.

## Структура
- `apps/mediasoup-server/src/server.js` — основной backend (WebSocket signaling + mediasoup orchestration).
- `apps/mediasoup-server/scripts/*` — JS стресс-тесты signaling/media plane.
- `scripts/*` — вспомогательные нагрузочные сценарии (Python).

## Что делает backend
- Принимает WebSocket-соединения (ws/wss).
- Обрабатывает protocol operations (`system.*`, `router.*`, `transport.*`, `producer.*`, `consumer.*`).
- Управляет worker pool, router-ами, transport/producers/consumers.
- Публикует health/metrics/audit.
- Контролирует лимиты и перегрузку (queue/inflight/capacity guardrails).

## Event bus на сервере
Event-driven исполнение реализовано через:
1. lifecycle WebSocket событий;
2. dispatch операций в `handleRequest` и `op*` handlers;
3. runtime события worker/QoS/shutdown.

Это фактически серверный event bus: входящий сигнал -> операция -> state transition -> ответ/метрика/аудит.
