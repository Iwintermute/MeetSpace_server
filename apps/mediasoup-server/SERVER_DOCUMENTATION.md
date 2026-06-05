# MeetSpace Mediasoup Server Documentation
## 1. Назначение и зона ответственности
`apps/mediasoup-server` — realtime media backend для MeetSpace, отвечающий за:
- WebSocket signaling для media control;
- оркестрацию mediasoup worker/router/transport/producer/consumer;
- сбор и отдачу телеметрии качества звонков;
- контур отказоустойчивости и graceful shutdown.

Сервис не хранит персистентные данные в БД: состояние звонков держится в памяти процесса.

## 2. Стек и зависимости
Основной runtime:
- Node.js (CommonJS, `type: commonjs`).
- `mediasoup` `^3.19.19` — SFU/media-plane.
- `ws` `^8.20.0` — WebSocket signaling.
- встроенные модули Node: `http`, `https`, `fs`, `os`, `crypto`.

Точка входа:
- `src/server.js`

NPM scripts:
- `npm start` / `npm run dev` — запуск backend.
- `npm run stress:ws` — нагрузка signaling.
- `npm run stress:media-plane` — нагрузка media-plane.

## 3. Архитектура процесса
Внутренние сущности (`state` в `server.js`):
- `workerPool` — пул mediasoup workers.
- `routers` — routers по комнатам.
- `roomPeers` — участники по комнатам.
- `transports`, `producers`, `consumers` — media сущности.
- `capabilityRouter` — router для RTP capabilities.
- `workerByRoom` — привязка room -> worker.

Подход:
- распределение комнат по worker pool;
- load-aware выбор worker по текущему числу активных peer-ов с лимитом `MEDIASOUP_MAX_PEERS_PER_WORKER`;
- отдельный capability router для выдачи RTP capabilities;
- room lifecycle cleanup (при уходе последнего peer комната закрывается).

## 4. Signaling и протокол
Транспорт:
- WebSocket endpoint на `MEDIASOUP_BACKEND_PATH` (по умолчанию `/ws`).
- HTTP(S) server используется и для WS upgrade, и для operational endpoints.

Формат запроса:
- JSON с полями `id`, `operation`, `payload`.

Формат ответа:
- унифицированный envelope `ok: true/false`, `data`, `backend.routerRtpCapabilities`, `message`, `code` (для ошибок).

Ключевые операции:
- `system.getCapabilities`
- `system.getMediaStats`
- `worker.createRouter`
- `router.joinPeer` / `router.leavePeer` / `router.closePeer`
- `router.createWebRtcTransport`
- `transport.connectDtls`
- `transport.addIceCandidate`
- `transport.produce`
- `producer.pause` / `producer.resume` / `producer.close`
- `transport.consume`
- `consumer.resume`

## 5. Media-plane
Кодеки:
- audio/opus
- video/VP9, VP8, H264

Транспорт:
- WebRTC transport создаётся на room worker через mediasoup `createWebRtcTransport`.
- Отдаёт `iceParameters`, `iceCandidates`, `dtlsParameters`, `sctpParameters`.
- Поддерживается прокидывание `iceServers` (из env-конфига) в ответ transport open.

Треки:
- producer создаётся через `transport.produce` с `kind`, `rtpParameters`, `trackType`.
- consumer создаётся paused и затем поднимается `consumer.resume`.
- server валидирует room ownership/consumability перед consume.

## 6. Наблюдаемость (Observability)
### 6.1 HTTP operational endpoints
- `/health` — состояние процесса (workers, rooms, peers, draining).
- `/metrics` — Prometheus-compatible метрики.
- `/audit` — кольцевой буфер последних audit событий.

### 6.2 Метрики
Счётчики/гейджи включают:
- операции (`opsTotal`, `opsSuccess`, `opsFailed`, latency avg),
- ws соединения/disconnect,
- auth rejections,
- worker deaths,
- активные rooms/peers/transports/producers/consumers.

### 6.3 QoS телеметрия
Периодический loop (`MEDIASOUP_QOS_INTERVAL_MS`) собирает producer/consumer stats и пишет structured JSON logs:
- bitrate,
- jitter,
- packets/packetsLost,
- roundTripTime.

### 6.4 Статистика по запросу (`system.getMediaStats`)
Сервер агрегирует и возвращает по transport/producer/consumer:
- `rowsCount`
- `totalBytes`
- `totalPackets`
- `totalPacketsLost`
- `avgJitter`
- `avgRoundTripTime`
- `bitrateKbps`

## 7. Безопасность
### 7.1 TLS
Поддержка `wss`:
- `MEDIASOUP_BACKEND_TLS_ENABLED`
- `MEDIASOUP_BACKEND_TLS_CERT_FILE`
- `MEDIASOUP_BACKEND_TLS_KEY_FILE`
- `MEDIASOUP_BACKEND_TLS_CA_FILE`
- `MEDIASOUP_BACKEND_TLS_REQUIRE_CLIENT_CERT`

### 7.2 WS auth
Если задан `MEDIASOUP_AUTH_SECRET`, включается auth на WS upgrade.
Параметры query, поддерживаемые сервером:
- `token`
- `auth`

Проверка токена:
- HMAC SHA-256 (`crypto.createHmac`) + timing-safe compare.

### 7.3 Hardening
- ограничение max payload (`MEDIASOUP_WS_MAX_PAYLOAD_BYTES`);
- ограничение max connections;
- ограничение очереди сообщений на сокет;
- operation timeout;
- heartbeat ping/pong и socket termination при timeout;
- лимиты capacity по rooms/peers/transports/producers/consumers;
- per-peer sliding-window rate limiting (`MEDIASOUP_PEER_RATE_WINDOW_MS`, `MEDIASOUP_PEER_MAX_OPS_PER_WINDOW`).

## 8. Отказоустойчивость и graceful degradation
### 8.1 Worker resilience
- worker death мониторится;
- affected rooms закрываются;
- capability router пересоздаётся при необходимости;
- worker pool пытается автоматически восстановиться.

### 8.2 Graceful shutdown/drain
При SIGINT/SIGTERM:
- service переходит в draining;
- heartbeat/qos loops останавливаются;
- ожидается drain комнат до timeout (`MEDIASOUP_DRAIN_TIMEOUT_MS`);
- закрываются media сущности, workers, WS clients, server socket.

## 9. Multi-region и geo-affinity
`MEDIASOUP_REGION` маркирует инстанс региона.
Region включён в:
- health response,
- logs,
- prometheus metrics labels.

Роутинг по регионам выполняется на уровне внешнего signaling/orchestrator слоя, а этот backend предоставляет региональный media endpoint.

## 10. Конфигурация (основные env)
Сетевые:
- `MEDIASOUP_BACKEND_HOST`, `MEDIASOUP_BACKEND_PORT`, `MEDIASOUP_BACKEND_PATH`
- `MEDIASOUP_ANNOUNCED_IP`, `MEDIASOUP_RTC_LISTEN_IP`
- `MEDIASOUP_RTC_MIN_PORT`, `MEDIASOUP_RTC_MAX_PORT`

Производительность:
- `MEDIASOUP_WORKER_COUNT`
- `MEDIASOUP_PHYSICAL_CORE_COUNT` (hint для режима 1 worker на физическое ядро)
- `MEDIASOUP_MAX_PEERS_PER_WORKER` (базовый target: 500 active users/worker)
- `MEDIASOUP_MAX_INFLIGHT_OPERATIONS` (global concurrent signaling operations)
- `MEDIASOUP_MAX_GLOBAL_PENDING_MESSAGES` (global queued signaling messages)
- лимиты capacities и WS limits

Безопасность:
- TLS env
- `MEDIASOUP_AUTH_SECRET`
- `MEDIASOUP_ICE_SERVERS` (JSON array STUN/TURN)

Наблюдаемость:
- `MEDIASOUP_QOS_INTERVAL_MS`
- `MEDIASOUP_MAX_AUDIT_ENTRIES`
- `MEDIASOUP_REGION`

## 11. Интеграция с клиентом MeetSpace
Клиент использует:
- `system.getCapabilities` для RTP capabilities + `iceServers`;
- `router.createWebRtcTransport` и DTLS connect;
- publish/consume track operations;
- `system.getMediaStats` для ABR/degraded-mode логики;
- WS auth query (`auth` или `token`) при включённом server auth.

## 12. Ограничения и практические замечания
- Сервис stateful in-memory: при рестарте call state теряется.
- Нет встроенного persistent audit sink (только in-memory + stdout logs).
- QoS и metrics сбор без внешнего TSDB/SIEM требует интеграции на уровне инфраструктуры (Prometheus + Grafana + log pipeline).
- Multi-region failover требует внешнего control plane (service discovery/traffic manager), не реализуется только внутри этого процесса.
