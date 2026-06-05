'use strict';

const http = require('node:http');
const https = require('node:https');
const fs = require('node:fs');
const os = require('node:os');
const crypto = require('node:crypto');
const mediasoup = require('mediasoup');
const { WebSocketServer } = require('ws');

/**
 * Calculates default mediasoup worker count from CPU topology and optional physical core hint.
 * @returns {number} Worker count to use when MEDIASOUP_WORKER_COUNT is not explicitly provided.
 */
function defaultWorkerCount() {
    const logicalCpuCount = Array.isArray(os.cpus()) ? os.cpus().length : 1;
    const envPhysicalCoreHint = Number.parseInt(process.env.MEDIASOUP_PHYSICAL_CORE_COUNT || '', 10);
    if (Number.isFinite(envPhysicalCoreHint) && envPhysicalCoreHint > 0) {
        return Math.max(1, envPhysicalCoreHint);
    }

    return Math.max(1, Math.floor(logicalCpuCount / 2));
}

function clampWorkerCount(value) {
    if (!Number.isFinite(value)) {
        return 1;
    }
    return Math.max(1, Math.min(64, Math.trunc(value)));
}

/**
 * Reads integer value from environment and clamps it to a safe configured range.
 * @param {string} name Environment variable name.
 * @param {number} fallbackValue Value used when env variable is missing or invalid.
 * @param {number} minValue Lower bound.
 * @param {number} maxValue Upper bound.
 * @returns {number} Clamped integer value.
 */
function readBoundedIntFromEnv(name, fallbackValue, minValue, maxValue) {
    const parsed = Number.parseInt(process.env[name] || '', 10);
    if (!Number.isFinite(parsed)) {
        return Math.max(minValue, Math.min(maxValue, Math.trunc(fallbackValue)));
    }
    return Math.max(minValue, Math.min(maxValue, Math.trunc(parsed)));
}

/**
 * Parses boolean-like environment value from common representations (1/0, true/false, yes/no, on/off).
 * @param {string} name Environment variable name.
 * @param {boolean} [fallbackValue=false] Default value when env var is missing or unrecognized.
 * @returns {boolean} Parsed boolean value.
 */
function parseBooleanEnv(name, fallbackValue = false) {
    const raw = (process.env[name] || '').trim().toLowerCase();
    if (raw.length === 0) {
        return fallbackValue;
    }
    if (raw === '1' || raw === 'true' || raw === 'yes' || raw === 'on') {
        return true;
    }
    if (raw === '0' || raw === 'false' || raw === 'no' || raw === 'off') {
        return false;
    }
    return fallbackValue;
}

const HOST = process.env.MEDIASOUP_BACKEND_HOST || '127.0.0.1';
const PORT = Number.parseInt(process.env.MEDIASOUP_BACKEND_PORT || '5001', 10);
const WS_PATH = normalizeWsPath(process.env.MEDIASOUP_BACKEND_PATH || '/ws');
const TLS_CERT_FILE = process.env.MEDIASOUP_BACKEND_TLS_CERT_FILE || '';
const TLS_KEY_FILE = process.env.MEDIASOUP_BACKEND_TLS_KEY_FILE || '';
const TLS_CA_FILE = process.env.MEDIASOUP_BACKEND_TLS_CA_FILE || '';
const TLS_REQUIRE_CLIENT_CERT = parseBooleanEnv('MEDIASOUP_BACKEND_TLS_REQUIRE_CLIENT_CERT', false);
const TLS_ENABLED = parseBooleanEnv('MEDIASOUP_BACKEND_TLS_ENABLED', false) ||
    (TLS_CERT_FILE.length > 0 && TLS_KEY_FILE.length > 0);
const ANNOUNCED_IP = process.env.MEDIASOUP_ANNOUNCED_IP || undefined;
const RTC_LISTEN_IP = process.env.MEDIASOUP_RTC_LISTEN_IP || (ANNOUNCED_IP ? '0.0.0.0' : '127.0.0.1');
const RTC_MIN_PORT = Number.parseInt(process.env.MEDIASOUP_RTC_MIN_PORT || '40000', 10);
const RTC_MAX_PORT = Number.parseInt(process.env.MEDIASOUP_RTC_MAX_PORT || '49999', 10);
const WORKER_COUNT = clampWorkerCount(Number.parseInt(process.env.MEDIASOUP_WORKER_COUNT || String(defaultWorkerCount()), 10));
const WS_MAX_PAYLOAD_BYTES = readBoundedIntFromEnv('MEDIASOUP_WS_MAX_PAYLOAD_BYTES', 256 * 1024, 1024, 4 * 1024 * 1024);
const WS_MAX_PENDING_MESSAGES_PER_SOCKET = readBoundedIntFromEnv('MEDIASOUP_WS_MAX_PENDING_MESSAGES_PER_SOCKET', 64, 1, 2048);
const WS_MAX_CONNECTIONS = readBoundedIntFromEnv('MEDIASOUP_WS_MAX_CONNECTIONS', 5000, 1, 50000);
const WS_OPERATION_TIMEOUT_MS = readBoundedIntFromEnv('MEDIASOUP_WS_OPERATION_TIMEOUT_MS', 5000, 500, 30000);
const WS_HEARTBEAT_INTERVAL_MS = readBoundedIntFromEnv('MEDIASOUP_WS_HEARTBEAT_INTERVAL_MS', 10000, 1000, 120000);
const WS_HEARTBEAT_TIMEOUT_MS = readBoundedIntFromEnv('MEDIASOUP_WS_HEARTBEAT_TIMEOUT_MS', 30000, 2000, 180000);
const MAX_ROOMS = readBoundedIntFromEnv('MEDIASOUP_MAX_ROOMS', 2000, 1, 20000);
const MAX_PEERS_PER_ROOM = readBoundedIntFromEnv('MEDIASOUP_MAX_PEERS_PER_ROOM', 700, 1, 5000);
const MAX_TOTAL_PEERS = readBoundedIntFromEnv('MEDIASOUP_MAX_TOTAL_PEERS', 10000, 1, 100000);
const MAX_TRANSPORTS_PER_ROOM = readBoundedIntFromEnv('MEDIASOUP_MAX_TRANSPORTS_PER_ROOM', 2500, 1, 20000);
const MAX_TOTAL_TRANSPORTS = readBoundedIntFromEnv('MEDIASOUP_MAX_TOTAL_TRANSPORTS', 15000, 1, 200000);
const MAX_PRODUCERS_PER_ROOM = readBoundedIntFromEnv('MEDIASOUP_MAX_PRODUCERS_PER_ROOM', 3000, 1, 50000);
const MAX_TOTAL_PRODUCERS = readBoundedIntFromEnv('MEDIASOUP_MAX_TOTAL_PRODUCERS', 30000, 1, 200000);
const MAX_CONSUMERS_PER_ROOM = readBoundedIntFromEnv('MEDIASOUP_MAX_CONSUMERS_PER_ROOM', 200000, 1, 1000000);
const MAX_TOTAL_CONSUMERS = readBoundedIntFromEnv('MEDIASOUP_MAX_TOTAL_CONSUMERS', 150000, 1, 1000000);
const MAX_PEERS_PER_WORKER = readBoundedIntFromEnv('MEDIASOUP_MAX_PEERS_PER_WORKER', 500, 50, 100000);
const MAX_STRING_FIELD_LENGTH = readBoundedIntFromEnv('MEDIASOUP_MAX_STRING_FIELD_LENGTH', 256, 16, 2048);
const MAX_GLOBAL_PENDING_MESSAGES = readBoundedIntFromEnv(
    'MEDIASOUP_MAX_GLOBAL_PENDING_MESSAGES',
    20000,
    100,
    1000000
);
const MAX_INFLIGHT_OPERATIONS = readBoundedIntFromEnv(
    'MEDIASOUP_MAX_INFLIGHT_OPERATIONS',
    Math.max(500, WORKER_COUNT * 500),
    50,
    200000
);
const LOG_LEVEL = process.env.MEDIASOUP_LOG_LEVEL || 'warn';
const LOG_TAGS = (process.env.MEDIASOUP_LOG_TAGS || 'info,ice,dtls,rtp,rtcp,srtp')
    .split(',')
    .map((item) => item.trim())
    .filter((item) => item.length > 0);

const REGION = process.env.MEDIASOUP_REGION || 'default';
const AUTH_SECRET = process.env.MEDIASOUP_AUTH_SECRET || '';
const AUTH_ENABLED = AUTH_SECRET.length > 0;
const QOS_INTERVAL_MS = readBoundedIntFromEnv('MEDIASOUP_QOS_INTERVAL_MS', 10000, 2000, 60000);
const DRAIN_TIMEOUT_MS = readBoundedIntFromEnv('MEDIASOUP_DRAIN_TIMEOUT_MS', 30000, 5000, 120000);
const ICE_SERVERS = parseIceServers(process.env.MEDIASOUP_ICE_SERVERS || '');

/**
 * Parses ICE server list from JSON environment string and keeps only entries with `urls`.
 * @param {string} raw Raw JSON string from MEDIASOUP_ICE_SERVERS.
 * @returns {Array<object>} Valid ICE server descriptors.
 */
function parseIceServers(raw) {
    if (!raw || raw.trim().length === 0) return [];
    try {
        const parsed = JSON.parse(raw);
        if (!Array.isArray(parsed)) return [];
        return parsed.filter((s) => s && typeof s === 'object' && s.urls);
    } catch (_) {
        return [];
    }
}

const metrics = {
    opsTotal: 0,
    opsSuccess: 0,
    opsFailed: 0,
    opLatencySum: 0,
    wsConnections: 0,
    wsDisconnections: 0,
    authRejections: 0,
    workerDeaths: 0,
    overloadRejections: 0
};
const auditLog = [];
const MAX_AUDIT_ENTRIES = readBoundedIntFromEnv('MEDIASOUP_MAX_AUDIT_ENTRIES', 10000, 100, 100000);
const peerRateWindows = new Map();
const PEER_RATE_WINDOW_MS = readBoundedIntFromEnv('MEDIASOUP_PEER_RATE_WINDOW_MS', 10000, 1000, 120000);
const PEER_MAX_OPS_PER_WINDOW = readBoundedIntFromEnv('MEDIASOUP_PEER_MAX_OPS_PER_WINDOW', 300, 10, 20000);

const MEDIA_CODECS = [
    {
        kind: 'audio',
        mimeType: 'audio/opus',
        clockRate: 48000,
        channels: 2,
        parameters: {
            useinbandfec: 1,
            usedtx: 1,
            minptime: 10,
            stereo: 0,
            'sprop-stereo': 0,
            maxplaybackrate: 48000,
            maxaveragebitrate: 128000,
            cbr: 0
        }
    },
    {
        kind: 'video',
        mimeType: 'video/VP9',
        clockRate: 90000,
        parameters: {
            'profile-id': 0,
            'x-google-start-bitrate': 2000,
            'x-google-max-bitrate': 8000
        }
    },
    {
        kind: 'video',
        mimeType: 'video/VP8',
        clockRate: 90000,
        parameters: {
            'x-google-start-bitrate': 2000,
            'x-google-max-bitrate': 8000
        }
    },
    {
        kind: 'video',
        mimeType: 'video/H264',
        clockRate: 90000,
        parameters: {
            'packetization-mode': 1,
            'profile-level-id': '42e01f',
            'level-asymmetry-allowed': 1,
            'x-google-start-bitrate': 2000,
            'x-google-max-bitrate': 8000
        }
    }
];

const state = {
    workerPool: [],
    workerCursor: 0,
    workerSerial: 0,
    workerByRoom: new Map(),
    capabilityRouter: null,
    capabilityWorkerId: null,
    routers: new Map(),
    roomPeers: new Map(),
    transports: new Map(),
    producers: new Map(),
    consumers: new Map(),
    shuttingDown: false,
    inflightOperations: 0,
    globalPendingMessages: 0
};
const socketRuntimeState = new WeakMap();
let heartbeatTimer = null;

function readTlsFile(pathValue, fieldName) {
    try {
        return fs.readFileSync(pathValue);
    } catch (error) {
        throw new Error(`Failed to read ${fieldName} from '${pathValue}': ${error?.message || String(error)}`);
    }
}

function buildTlsServerOptions() {
    if (!TLS_CERT_FILE || !TLS_KEY_FILE) {
        throw new Error(
            'TLS is enabled but MEDIASOUP_BACKEND_TLS_CERT_FILE or MEDIASOUP_BACKEND_TLS_KEY_FILE is missing.'
        );
    }
    if (TLS_REQUIRE_CLIENT_CERT && !TLS_CA_FILE) {
        throw new Error(
            'MEDIASOUP_BACKEND_TLS_REQUIRE_CLIENT_CERT is enabled but MEDIASOUP_BACKEND_TLS_CA_FILE is missing.'
        );
    }

    const options = {
        cert: readTlsFile(TLS_CERT_FILE, 'TLS certificate'),
        key: readTlsFile(TLS_KEY_FILE, 'TLS private key'),
        minVersion: 'TLSv1.2',
        requestCert: TLS_REQUIRE_CLIENT_CERT,
        rejectUnauthorized: TLS_REQUIRE_CLIENT_CERT
    };
    if (TLS_CA_FILE) {
        options.ca = readTlsFile(TLS_CA_FILE, 'TLS CA certificate');
    }

    return options;
}

const requestHandler = (req, res) => {
    const url = new URL(req.url || '/', `http://${req.headers.host || 'localhost'}`);
    if (url.pathname === '/health') {
        const health = buildHealthResponse();
        res.writeHead(health.ok ? 200 : 503, { 'content-type': 'application/json; charset=utf-8' });
        res.end(JSON.stringify(health));
        return;
    }
    if (url.pathname === '/metrics') {
        res.writeHead(200, { 'content-type': 'text/plain; charset=utf-8' });
        res.end(buildPrometheusMetrics());
        return;
    }
    if (url.pathname === '/audit') {
        const last = auditLog.slice(-200);
        res.writeHead(200, { 'content-type': 'application/json; charset=utf-8' });
        res.end(JSON.stringify({ entries: last }));
        return;
    }
    res.writeHead(200, { 'content-type': 'application/json; charset=utf-8' });
    res.end(JSON.stringify({
        ok: true,
        service: 'meetspace-mediasoup-backend',
        region: REGION,
        path: WS_PATH,
        transport: TLS_ENABLED ? 'wss' : 'ws',
        workers: WORKER_COUNT,
        authEnabled: AUTH_ENABLED,
        iceServersCount: ICE_SERVERS.length,
        limits: {
            wsMaxConnections: WS_MAX_CONNECTIONS,
            wsMaxPayloadBytes: WS_MAX_PAYLOAD_BYTES,
            maxRooms: MAX_ROOMS,
            maxPeersPerRoom: MAX_PEERS_PER_ROOM,
            maxPeersPerWorker: MAX_PEERS_PER_WORKER,
            maxTransportsPerRoom: MAX_TRANSPORTS_PER_ROOM,
            maxProducersPerRoom: MAX_PRODUCERS_PER_ROOM,
            maxConsumersPerRoom: MAX_CONSUMERS_PER_ROOM,
            maxInflightOperations: MAX_INFLIGHT_OPERATIONS,
            maxGlobalPendingMessages: MAX_GLOBAL_PENDING_MESSAGES
        }
    }));
};

const server = TLS_ENABLED
    ? https.createServer(buildTlsServerOptions(), requestHandler)
    : http.createServer(requestHandler);

const wss = new WebSocketServer({
    noServer: true,
    maxPayload: WS_MAX_PAYLOAD_BYTES
});

server.on('upgrade', (request, socket, head) => {
    if (state.shuttingDown) {
        socket.write('HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\n\r\n');
        socket.destroy();
        return;
    }
    if (wss.clients.size >= WS_MAX_CONNECTIONS) {
        socket.write('HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\n\r\n');
        socket.destroy();
        return;
    }
    const url = new URL(
        request.url || '/',
        `${TLS_ENABLED ? 'https' : 'http'}://${request.headers.host || 'localhost'}`
    );
    if (url.pathname !== WS_PATH) {
        socket.write('HTTP/1.1 404 Not Found\r\n\r\n');
        socket.destroy();
        return;
    }
    if (AUTH_ENABLED) {
        const token = url.searchParams.get('token') || url.searchParams.get('auth') || '';
        if (!verifyAuthToken(token)) {
            metrics.authRejections++;
            log('auth_rejected', 'warn', { ip: request.socket.remoteAddress });
            socket.write('HTTP/1.1 401 Unauthorized\r\nConnection: close\r\n\r\n');
            socket.destroy();
            return;
        }
    }

    wss.handleUpgrade(request, socket, head, (ws) => {
        wss.emit('connection', ws, request);
    });
});

wss.on('connection', (ws, request) => {
    const runtimeState = {
        queue: [],
        processing: false,
        closed: false,
        lastPongAt: Date.now()
    };
    socketRuntimeState.set(ws, runtimeState);
    metrics.wsConnections++;
    log('ws_connect', 'info', { ip: request.socket.remoteAddress || 'unknown' });
    ws.on('pong', () => {
        const currentState = socketRuntimeState.get(ws);
        if (!currentState) {
            return;
        }
        currentState.lastPongAt = Date.now();
    });
    ws.on('message', (rawBuffer) => {
        handleWsMessage(ws, rawBuffer);
    });

    ws.on('close', () => {
        runtimeState.closed = true;
        clearSocketQueue(runtimeState);
        metrics.wsDisconnections++;
        log('ws_disconnect', 'info');
    });

    ws.on('error', (error) => {
        log('ws_error', 'error', { error: error?.message || String(error) });
    });
});

function safeSend(ws, payload) {
    if (!ws || ws.readyState !== 1) {
        return false;
    }
    try {
        ws.send(JSON.stringify(payload));
        return true;
    } catch (_) {
        return false;
    }
}

function withOperationTimeout(promise, timeoutMs) {
    return Promise.race([
        promise,
        new Promise((_, reject) => {
            setTimeout(() => reject(new Error(`Operation timed out after ${timeoutMs}ms.`)), timeoutMs).unref();
        })
    ]);
}
function clearSocketQueue(runtimeState) {
    if (!runtimeState || !Array.isArray(runtimeState.queue)) {
        return;
    }
    const droppedCount = runtimeState.queue.length;
    if (droppedCount > 0) {
        state.globalPendingMessages = Math.max(0, state.globalPendingMessages - droppedCount);
        runtimeState.queue.length = 0;
    }
}

function handleWsMessage(ws, rawBuffer) {
    const runtimeState = socketRuntimeState.get(ws);
    if (!runtimeState || runtimeState.closed) {
        return;
    }
    if (state.globalPendingMessages >= MAX_GLOBAL_PENDING_MESSAGES) {
        metrics.overloadRejections++;
        safeSend(
            ws,
            makeFailure(
                undefined,
                'overloaded',
                `Backend pending queue is full (${state.globalPendingMessages}/${MAX_GLOBAL_PENDING_MESSAGES}).`
            )
        );
        return;
    }
    if (runtimeState.queue.length >= WS_MAX_PENDING_MESSAGES_PER_SOCKET) {
        metrics.overloadRejections++;
        safeSend(ws, makeFailure(undefined, 'overloaded', 'WebSocket queue overflow. Reduce signaling rate.'));
        return;
    }

    const payloadText = Buffer.isBuffer(rawBuffer)
        ? rawBuffer.toString('utf8')
        : String(rawBuffer ?? '');
    runtimeState.queue.push(payloadText);
    state.globalPendingMessages += 1;
    drainSocketQueue(ws, runtimeState).catch((error) => {
        log(`socket queue drain failure: ${error?.message || String(error)}`);
        clearSocketQueue(runtimeState);
        try {
            ws.terminate();
        } catch (_) {
        }
    });
}

async function drainSocketQueue(ws, runtimeState) {
    if (runtimeState.processing || runtimeState.closed) {
        return;
    }
    runtimeState.processing = true;
    try {
        while (runtimeState.queue.length > 0 && !runtimeState.closed) {
            const rawPayload = runtimeState.queue.shift();
            if (state.globalPendingMessages > 0) {
                state.globalPendingMessages -= 1;
            }
            let requestPayload = null;
            try {
                requestPayload = JSON.parse(rawPayload);
            } catch (error) {
                safeSend(ws, makeFailure(undefined, 'invalid_json', `Invalid JSON: ${error.message}`));
                continue;
            }
            if (state.inflightOperations >= MAX_INFLIGHT_OPERATIONS) {
                metrics.overloadRejections++;
                safeSend(
                    ws,
                    makeFailure(
                        requestPayload?.id,
                        'overloaded',
                        `Backend inflight limit reached (${state.inflightOperations}/${MAX_INFLIGHT_OPERATIONS}).`
                    )
                );
                continue;
            }

            state.inflightOperations += 1;
            let response;
            try {
                response = await withOperationTimeout(handleRequest(requestPayload), WS_OPERATION_TIMEOUT_MS)
                    .catch((error) => makeFailure(requestPayload?.id, 'timeout', error?.message || String(error)));
            } finally {
                if (state.inflightOperations > 0) {
                    state.inflightOperations -= 1;
                }
            }
            safeSend(ws, response);
        }
    } finally {
        runtimeState.processing = false;
    }
}

function startHeartbeatLoop() {
    if (heartbeatTimer) {
        return;
    }
    heartbeatTimer = setInterval(() => {
        const now = Date.now();
        for (const ws of wss.clients) {
            const runtimeState = socketRuntimeState.get(ws);
            if (!runtimeState || runtimeState.closed) {
                continue;
            }
            if (now - runtimeState.lastPongAt > WS_HEARTBEAT_TIMEOUT_MS) {
                runtimeState.closed = true;
                clearSocketQueue(runtimeState);
                try {
                    ws.terminate();
                } catch (_) {
                }
                continue;
            }
            if (ws.readyState === 1) {
                try {
                    ws.ping();
                } catch (_) {
                }
            }
        }
    }, WS_HEARTBEAT_INTERVAL_MS);
    heartbeatTimer.unref();
}

function totalPeersCount() {
    let total = 0;
    for (const peers of state.roomPeers.values()) {
        total += peers.size;
    }
    return total;
}

function countEntitiesByRoom(entries, roomId) {
    let total = 0;
    for (const entry of entries.values()) {
        if (entry?.roomId === roomId) {
            total += 1;
        }
    }
    return total;
}
function buildWorkerLoadSnapshot() {
    const snapshot = new Map();
    for (const context of state.workerPool) {
        if (!context?.workerId) {
            continue;
        }
        snapshot.set(context.workerId, {
            roomCount: 0,
            peerCount: 0
        });
    }

    for (const [roomId, workerId] of state.workerByRoom.entries()) {
        const load = snapshot.get(workerId);
        if (!load) {
            continue;
        }
        load.roomCount += 1;
        const peers = state.roomPeers.get(roomId);
        if (peers && peers.size > 0) {
            load.peerCount += peers.size;
        }
    }

    return snapshot;
}

function ensureWorkerPeerCapacityForJoin(roomId, peerId) {
    const peers = state.roomPeers.get(roomId);
    if (peers?.has(peerId)) {
        return;
    }

    const workerId = state.workerByRoom.get(roomId);
    if (typeof workerId !== 'string' || workerId.length === 0) {
        return;
    }

    const workerContext = getWorkerContextById(workerId);
    if (!workerContext || workerContext.worker?.closed || workerContext.webRtcServer?.closed) {
        return;
    }

    const loadSnapshot = buildWorkerLoadSnapshot();
    const currentWorkerLoad = loadSnapshot.get(workerId);
    const currentPeerCount = currentWorkerLoad?.peerCount || 0;
    if (currentPeerCount + 1 > MAX_PEERS_PER_WORKER) {
        failWithCode(
            'capacity_exceeded',
            `Worker ${workerId} peer capacity reached (${currentPeerCount}/${MAX_PEERS_PER_WORKER}).`
        );
    }
}

function ensureEntityCapacity(currentValue, maxValue, entityName) {
    if (currentValue < maxValue) {
        return;
    }
    failWithCode(
        'capacity_exceeded',
        `${entityName} capacity reached (${currentValue}/${maxValue}).`
    );
}

function ensureRoomCapacityForJoin(roomId, peerId) {
    const peers = state.roomPeers.get(roomId);
    if (peers?.has(peerId)) {
        return;
    }

    if (!state.routers.has(roomId) && state.routers.size >= MAX_ROOMS) {
        failWithCode('capacity_exceeded', `Room capacity reached (${state.routers.size}/${MAX_ROOMS}).`);
    }
    if (peers && peers.size >= MAX_PEERS_PER_ROOM) {
        failWithCode('capacity_exceeded', `Room ${roomId} is full (${peers.size}/${MAX_PEERS_PER_ROOM}).`);
    }
    ensureEntityCapacity(totalPeersCount(), MAX_TOTAL_PEERS, 'Peer');
    ensureWorkerPeerCapacityForJoin(roomId, peerId);
}

function ensurePeerJoined(roomId, peerId) {
    const peers = state.roomPeers.get(roomId);
    if (peers?.has(peerId)) {
        return;
    }
    failWithCode('peer_not_joined', `Peer ${peerId} is not joined in room ${roomId}.`);
}

function enforcePeerRateLimit(peerId) {
    if (typeof peerId !== 'string' || peerId.trim().length === 0) {
        return;
    }
    const normalizedPeerId = peerId.trim();
    const now = Date.now();
    let history = peerRateWindows.get(normalizedPeerId);
    if (!history) {
        history = [];
        peerRateWindows.set(normalizedPeerId, history);
    }

    while (history.length > 0 && (now - history[0]) > PEER_RATE_WINDOW_MS) {
        history.shift();
    }
    if (history.length >= PEER_MAX_OPS_PER_WINDOW) {
        failWithCode(
            'rate_limited',
            `Peer ${normalizedPeerId} exceeded rate limit (${PEER_MAX_OPS_PER_WINDOW}/${PEER_RATE_WINDOW_MS}ms).`
        );
    }
    history.push(now);
}

function resolvePeerIdForRateLimit(payload) {
    if (!payload || typeof payload !== 'object') {
        return '';
    }

    const explicitPeerId = typeof payload.peerId === 'string' ? payload.peerId.trim() : '';
    if (explicitPeerId.length > 0) {
        return explicitPeerId;
    }

    const transportId = typeof payload.transportId === 'string' ? payload.transportId.trim() : '';
    if (transportId.length > 0) {
        const transportEntry = state.transports.get(transportId);
        if (transportEntry?.peerId) {
            return transportEntry.peerId;
        }
    }

    const producerId = typeof payload.producerId === 'string' ? payload.producerId.trim() : '';
    if (producerId.length > 0) {
        const producerEntry = state.producers.get(producerId);
        if (producerEntry?.peerId) {
            return producerEntry.peerId;
        }
    }

    const consumerId = typeof payload.consumerId === 'string' ? payload.consumerId.trim() : '';
    if (consumerId.length > 0) {
        const consumerEntry = state.consumers.get(consumerId);
        if (consumerEntry?.peerId) {
            return consumerEntry.peerId;
        }
    }

    return '';
}

function failWithCode(code, message) {
    const error = new Error(message);
    error.code = code;
    throw error;
}

function ensureNotShuttingDown() {
    if (!state.shuttingDown) {
        return;
    }
    failWithCode('shutting_down', 'Backend is shutting down.');
}

/**
 * Central signaling dispatcher that validates operation, enforces limits, routes to op-handlers,
 * and records metrics/audit for success and failure paths.
 * @param {object} request Incoming signaling request payload.
 * @returns {Promise<object>} Standardized success/failure response envelope.
 */
async function handleRequest(request) {
    ensureNotShuttingDown();
    const id = request?.id;
    const operation = request?.operation;
    const payload = request?.payload && typeof request.payload === 'object' && !Array.isArray(request.payload)
        ? request.payload
        : {};

    if (typeof operation !== 'string' || operation.length === 0) {
        return makeFailure(id, 'invalid_request', 'Field "operation" must be a non-empty string.');
    }

    const opStart = Date.now();
    const peerId = resolvePeerIdForRateLimit(payload);
    if (peerId)
        enforcePeerRateLimit(peerId);
    try {
        let result;
        switch (operation) {
            case 'system.getCapabilities':
                result = await opGetCapabilities(id);
                break;
            case 'system.getMediaStats':
                result = await opGetMediaStats(id, payload);
                break;
            case 'worker.createRouter':
                result = await opCreateRouter(id, payload);
                break;
            case 'router.joinPeer':
                result = await opJoinPeer(id, payload);
                break;
            case 'router.leavePeer':
                result = await opLeavePeer(id, payload);
                break;
            case 'router.closePeer':
                result = await opClosePeer(id, payload);
                break;
            case 'router.createWebRtcTransport':
                result = await opCreateWebRtcTransport(id, payload);
                break;
            case 'transport.connectDtls':
                result = await opConnectDtls(id, payload);
                break;
            case 'transport.addIceCandidate':
                result = await opAddIceCandidate(id, payload);
                break;
            case 'transport.produce':
                result = await opProduce(id, payload);
                break;
            case 'producer.pause':
                result = await opPauseProducer(id, payload);
                break;
            case 'producer.resume':
                result = await opResumeProducer(id, payload);
                break;
            case 'producer.close':
                result = await opCloseProducer(id, payload);
                break;
            case 'transport.consume':
                result = await opConsume(id, payload);
                break;
            case 'consumer.resume':
                result = await opResumeConsumer(id, payload);
                break;
            default:
                result = makeFailure(id, 'unsupported_operation', `Unsupported operation: ${operation}`);
                break;
        }
        metrics.opsTotal++;
        metrics.opLatencySum += Date.now() - opStart;
        if (result.ok) {
            metrics.opsSuccess++;
        } else {
            metrics.opsFailed++;
        }
        audit(peerId, operation, result.ok ? 'ok' : (result.code || 'error'));
        return result;
    } catch (error) {
        metrics.opsTotal++;
        metrics.opsFailed++;
        metrics.opLatencySum += Date.now() - opStart;
        const code = typeof error?.code === 'string' && error.code.length > 0
            ? error.code
            : 'internal_error';
        audit(peerId, operation, code);
        return makeFailure(id, code, error?.message || String(error));
    }
}

/**
 * Returns mediasoup backend capabilities including RTP capabilities and optional ICE server list.
 * @param {string} id Correlation id of request.
 * @returns {Promise<object>} Success response payload for `system.getCapabilities`.
 */
async function opGetCapabilities(id) {
    const router = await ensureCapabilityRouter();
    return makeSuccess(id, 'Mediasoup backend capabilities.', {
        region: REGION,
        iceServers: ICE_SERVERS.length > 0 ? ICE_SERVERS : undefined
    }, router);
}

function toFiniteNumber(value) {
    if (typeof value !== 'number' || !Number.isFinite(value)) {
        return 0;
    }
    return value;
}

function summarizeStatsRows(rows) {
    const normalizedRows = Array.isArray(rows) ? rows : [];
    let totalBytes = 0;
    let totalPackets = 0;
    let totalPacketsLost = 0;
    let totalBitrate = 0;
    let jitterSum = 0;
    let jitterCount = 0;
    let rttSum = 0;
    let rttCount = 0;
    for (const row of normalizedRows) {
        if (!row || typeof row !== 'object') {
            continue;
        }
        totalBytes += toFiniteNumber(row.bytesReceived);
        totalBytes += toFiniteNumber(row.bytesSent);
        totalBytes += toFiniteNumber(row.byteCount);
        totalPackets += toFiniteNumber(row.packetsReceived);
        totalPackets += toFiniteNumber(row.packetsSent);
        totalPackets += toFiniteNumber(row.packetCount);
        totalPacketsLost += toFiniteNumber(row.packetsLost);
        const bitrate = toFiniteNumber(row.bitrate);
        if (bitrate > 0)
            totalBitrate += bitrate;
        const jitter = toFiniteNumber(row.jitter);
        if (jitter > 0) {
            jitterSum += jitter;
            jitterCount += 1;
        }
        const roundTripTime = toFiniteNumber(
            row.roundTripTime ?? row.totalRoundTripTime ?? row.currentRoundTripTime);
        if (roundTripTime > 0) {
            rttSum += roundTripTime;
            rttCount += 1;
        }
    }
    return {
        rowsCount: normalizedRows.length,
        totalBytes,
        totalPackets,
        totalPacketsLost,
        avgJitter: jitterCount > 0 ? jitterSum / jitterCount : 0,
        avgRoundTripTime: rttCount > 0 ? rttSum / rttCount : 0,
        bitrateKbps: totalBitrate > 0 ? totalBitrate / 1000.0 : 0
    };
}

function normalizeInjectedStats(value) {
    if (!value || typeof value !== 'object') {
        return { totalPackets: 0, totalBytes: 0 };
    }
    return {
        totalPackets: toFiniteNumber(value.totalPackets ?? value.packetCount),
        totalBytes: toFiniteNumber(value.totalBytes ?? value.byteCount)
    };
}

function buildInjectedStatsFromPayload(payload) {
    if (!payload || payload.injectTestRtp !== true || typeof payload.testRtp !== 'object' || payload.testRtp === null) {
        return null;
    }
    const packetCount = Number.parseInt(payload.testRtp.packetCount ?? 0, 10);
    const payloadSize = Number.parseInt(payload.testRtp.payloadSize ?? 0, 10);
    if (!Number.isFinite(packetCount) || !Number.isFinite(payloadSize) || packetCount <= 0 || payloadSize <= 0) {
        return null;
    }
    return {
        totalPackets: packetCount,
        totalBytes: packetCount * payloadSize
    };
}

function createEmptyStatsSummary() {
    return {
        rowsCount: 0,
        totalBytes: 0,
        totalPackets: 0,
        totalPacketsLost: 0,
        avgJitter: 0,
        avgRoundTripTime: 0,
        bitrateKbps: 0
    };
}

/**
 * Collects aggregated media stats for transports, producers, and consumers (optionally filtered by room).
 * @param {string} id Correlation id of request.
 * @param {object} payload Request payload that may include roomId.
 * @returns {Promise<object>} Success response with per-entity and aggregate media metrics.
 */
async function opGetMediaStats(id, payload) {
    const roomId = typeof payload.roomId === 'string' && payload.roomId.trim().length > 0
        ? payload.roomId.trim()
        : '';
    const roomFilter = (candidateRoomId) => roomId.length === 0 || candidateRoomId === roomId;
    const transportReports = [];
    const producerReports = [];
    const consumerReports = [];
    let totalTransportBytes = 0;
    let totalTransportPackets = 0;
    let totalProducerBytes = 0;
    let totalProducerPackets = 0;
    let totalConsumerBytes = 0;
    let totalConsumerPackets = 0;

    for (const [transportId, entry] of state.transports.entries()) {
        if (!roomFilter(entry.roomId)) {
            continue;
        }
        let summary = createEmptyStatsSummary();
        let statsError = '';
        try {
            summary = summarizeStatsRows(await entry.transport.getStats());
        } catch (error) {
            statsError = error?.message || String(error);
        }
        totalTransportBytes += summary.totalBytes;
        totalTransportPackets += summary.totalPackets;
        transportReports.push({
            transportId,
            roomId: entry.roomId,
            peerId: entry.peerId,
            dtlsState: entry.transport?.dtlsState || null,
            rowsCount: summary.rowsCount,
            totalBytes: summary.totalBytes,
            totalPackets: summary.totalPackets,
            totalPacketsLost: summary.totalPacketsLost,
            avgJitter: summary.avgJitter,
            avgRoundTripTime: summary.avgRoundTripTime,
            bitrateKbps: summary.bitrateKbps,
            statsError
        });
    }

    for (const [producerId, entry] of state.producers.entries()) {
        if (!roomFilter(entry.roomId)) {
            continue;
        }
        let summary = createEmptyStatsSummary();
        let statsError = '';
        if (entry.producer) {
            try {
                summary = summarizeStatsRows(await entry.producer.getStats());
            } catch (error) {
                statsError = error?.message || String(error);
            }
        } else {
            statsError = 'producer_handle_missing';
        }
        const injectedStats = normalizeInjectedStats(entry.injectedStats);
        if (summary.totalPackets <= 0 && summary.totalBytes <= 0 && injectedStats.totalPackets > 0) {
            summary.totalPackets = injectedStats.totalPackets;
            summary.totalBytes = injectedStats.totalBytes;
            if (summary.rowsCount === 0) {
                summary.rowsCount = 1;
            }
        }
        totalProducerBytes += summary.totalBytes;
        totalProducerPackets += summary.totalPackets;
        producerReports.push({
            producerId,
            roomId: entry.roomId,
            peerId: entry.peerId,
            transportId: entry.transportId,
            kind: entry.kind,
            trackType: entry.trackType || null,
            paused: entry.producer?.paused ?? false,
            rowsCount: summary.rowsCount,
            totalBytes: summary.totalBytes,
            totalPackets: summary.totalPackets,
            totalPacketsLost: summary.totalPacketsLost,
            avgJitter: summary.avgJitter,
            avgRoundTripTime: summary.avgRoundTripTime,
            bitrateKbps: summary.bitrateKbps,
            statsError
        });
    }

    for (const [consumerId, entry] of state.consumers.entries()) {
        if (!roomFilter(entry.roomId)) {
            continue;
        }
        let summary = createEmptyStatsSummary();
        let statsError = '';
        if (entry.consumer) {
            try {
                summary = summarizeStatsRows(await entry.consumer.getStats());
            } catch (error) {
                statsError = error?.message || String(error);
            }
        } else {
            statsError = 'consumer_handle_missing';
        }
        const injectedStats = normalizeInjectedStats(entry.injectedStats);
        if (summary.totalPackets <= 0 && summary.totalBytes <= 0 && injectedStats.totalPackets > 0) {
            summary.totalPackets = injectedStats.totalPackets;
            summary.totalBytes = injectedStats.totalBytes;
            if (summary.rowsCount === 0) {
                summary.rowsCount = 1;
            }
        }
        totalConsumerBytes += summary.totalBytes;
        totalConsumerPackets += summary.totalPackets;
        consumerReports.push({
            consumerId,
            roomId: entry.roomId,
            peerId: entry.peerId,
            producerId: entry.producerId,
            transportId: entry.transportId,
            kind: entry.kind,
            trackType: entry.trackType || null,
            paused: entry.consumer?.paused ?? true,
            rowsCount: summary.rowsCount,
            totalBytes: summary.totalBytes,
            totalPackets: summary.totalPackets,
            totalPacketsLost: summary.totalPacketsLost,
            avgJitter: summary.avgJitter,
            avgRoundTripTime: summary.avgRoundTripTime,
            bitrateKbps: summary.bitrateKbps,
            statsError
        });
    }

    const targetRouter = roomId.length > 0
        ? (getRoomRouter(roomId) || state.capabilityRouter)
        : state.capabilityRouter;

    return makeSuccess(
        id,
        'Media stats ready.',
        {
            roomId: roomId.length > 0 ? roomId : null,
            transportCount: transportReports.length,
            producerCount: producerReports.length,
            consumerCount: consumerReports.length,
            totalTransportBytes,
            totalTransportPackets,
            totalProducerBytes,
            totalProducerPackets,
            totalConsumerBytes,
            totalConsumerPackets,
            transports: transportReports,
            producers: producerReports,
            consumers: consumerReports,
            workers: workerDiagnostics()
        },
        targetRouter
    );
}

/**
 * Ensures router existence for room and returns router identity for signaling layer.
 * @param {string} id Correlation id of request.
 * @param {object} payload Request payload with `roomId`.
 * @returns {Promise<object>} Success response for `worker.createRouter`.
 */
async function opCreateRouter(id, payload) {
    const roomId = requireString(payload.roomId, 'roomId');
    if (!state.routers.has(roomId)) {
        ensureEntityCapacity(state.routers.size, MAX_ROOMS, 'Room');
    }
    const router = await ensureRoomRouter(roomId);
    return makeSuccess(
        id,
        `Router ready for room ${roomId}.`,
        {
            roomId,
            routerId: router.id
        },
        router
    );
}

/**
 * Joins peer to room after capacity checks and router readiness.
 * @param {string} id Correlation id of request.
 * @param {object} payload Request payload with `roomId` and `peerId`.
 * @returns {Promise<object>} Success response for `router.joinPeer`.
 */
async function opJoinPeer(id, payload) {
    const roomId = requireString(payload.roomId, 'roomId');
    const peerId = requireString(payload.peerId, 'peerId');
    ensureRoomCapacityForJoin(roomId, peerId);
    const router = await ensureRoomRouter(roomId);
    ensureWorkerPeerCapacityForJoin(roomId, peerId);
    ensurePeerSet(roomId).add(peerId);
    return makeSuccess(
        id,
        `Peer ${peerId} joined ${roomId}.`,
        {
            roomId,
            peerId
        },
        router
    );
}

/**
 * Removes peer from room, closes related resources, and performs room cleanup when empty.
 * @param {string} id Correlation id of request.
 * @param {object} payload Request payload with `roomId` and `peerId`.
 * @returns {Promise<object>} Success response for `router.leavePeer`.
 */
async function opLeavePeer(id, payload) {
    const roomId = requireString(payload.roomId, 'roomId');
    const peerId = requireString(payload.peerId, 'peerId');
    const router = getRoomRouter(roomId);

    if (router) {
        closePeerResources(roomId, peerId);
    }

    const peers = state.roomPeers.get(roomId);
    if (peers) {
        peers.delete(peerId);
    }

    if (peers && peers.size === 0) {
        closeRoom(roomId);
    }

    return makeSuccess(
        id,
        `Peer ${peerId} left ${roomId}.`,
        {
            roomId,
            peerId
        },
        getRoomRouter(roomId) || router || state.capabilityRouter
    );
}

async function opClosePeer(id, payload) {
    const roomId = requireString(payload.roomId, 'roomId');
    const peerId = requireString(payload.peerId, 'peerId');
    const router = getRoomRouter(roomId);
    if (router) {
        closePeerResources(roomId, peerId);
    }

    const peers = state.roomPeers.get(roomId);
    if (peers) {
        peers.delete(peerId);
    }
    if (peers && peers.size === 0) {
        closeRoom(roomId);
    }

    return makeSuccess(
        id,
        `Peer ${peerId} closed in ${roomId}.`,
        {
            roomId,
            peerId
        },
        getRoomRouter(roomId) || router || state.capabilityRouter
    );
}

/**
 * Creates mediasoup WebRTC transport for peer within room and returns transport negotiation parameters.
 * @param {string} id Correlation id of request.
 * @param {object} payload Request payload containing room/peer/transport ids.
 * @returns {Promise<object>} Success response with ICE/DTLS/SCTP transport data.
 */
async function opCreateWebRtcTransport(id, payload) {
    const roomId = requireString(payload.roomId, 'roomId');
    const peerId = requireString(payload.peerId, 'peerId');
    const transportId = requireString(payload.transportId, 'transportId');
    ensurePeerJoined(roomId, peerId);
    ensureEntityCapacity(state.transports.size, MAX_TOTAL_TRANSPORTS, 'Transport');
    ensureEntityCapacity(
        countEntitiesByRoom(state.transports, roomId),
        MAX_TRANSPORTS_PER_ROOM,
        `Room ${roomId} transport`
    );
    const router = await ensureRoomRouter(roomId);
    const roomWorker = getRoomWorkerContext(roomId);
    if (!roomWorker || !roomWorker.webRtcServer) {
        throw new Error(`No active mediasoup worker context for room: ${roomId}`);
    }

    if (state.transports.has(transportId)) {
        throw new Error(`Transport already exists: ${transportId}`);
    }

    const transport = await router.createWebRtcTransport({
        webRtcServer: roomWorker.webRtcServer,
        enableUdp: true,
        enableTcp: true,
        preferUdp: true
    });

    transport.on('dtlsstatechange', (stateName) => {
        if (stateName === 'closed') {
            safeCloseTransportById(transportId);
        }
    });

    transport.on('close', () => {
        safeCloseTransportById(transportId);
    });

    state.transports.set(transportId, { roomId, peerId, transport });

    return makeSuccess(
        id,
        `Transport ${transportId} created.`,
        {
            roomId,
            peerId,
            transportId,
            mediasoupTransportId: transport.id,
            iceParameters: transport.iceParameters,
            iceCandidates: transport.iceCandidates,
            dtlsParameters: transport.dtlsParameters,
            sctpParameters: transport.sctpParameters,
            iceServers: ICE_SERVERS.length > 0 ? ICE_SERVERS : undefined
        },
        router
    );
}

/**
 * Applies DTLS parameters to existing transport and marks it connected.
 * @param {string} id Correlation id of request.
 * @param {object} payload Request payload containing transport id and dtlsParameters.
 * @returns {Promise<object>} Success response for `transport.connectDtls`.
 */
async function opConnectDtls(id, payload) {
    const transportId = requireString(payload.transportId, 'transportId');
    const transportEntry = getTransportEntry(transportId);
    if (!payload.dtlsParameters || typeof payload.dtlsParameters !== 'object') {
        throw new Error(`Transport ${transportId} requires dtlsParameters for connect.`);
    }
    await transportEntry.transport.connect({ dtlsParameters: payload.dtlsParameters });

    return makeSuccess(
        id,
        `Transport ${transportId} connected.`,
        { transportId, connected: true, mode: 'dtls' },
        getRoomRouter(transportEntry.roomId)
    );
}

async function opAddIceCandidate(id, payload) {
    const transportId = requireString(payload.transportId, 'transportId');
    const transportEntry = getTransportEntry(transportId);

    return makeSuccess(
        id,
        `ICE candidate accepted for ${transportId}.`,
        {
            transportId,
            accepted: true,
            mode: 'noop'
        },
        getRoomRouter(transportEntry.roomId)
    );
}

/**
 * Creates producer on transport and stores producer ownership/track metadata in runtime state.
 * @param {string} id Correlation id of request.
 * @param {object} payload Request payload containing producer/transport/RTP details.
 * @returns {Promise<object>} Success response with producer identifiers.
 */
async function opProduce(id, payload) {
    const transportId = requireString(payload.transportId, 'transportId');
    const producerId = requireString(payload.producerId, 'producerId');
    const kind = requireString(payload.kind, 'kind');
    const rawTrackType = typeof payload.trackType === 'string' ? payload.trackType.trim() : '';
    const defaultTrackType = kind === 'video' ? 'camera' : 'microphone';
    const trackType = rawTrackType.length > 0 ? rawTrackType : defaultTrackType;
    const transportEntry = getTransportEntry(transportId);
    const router = getRoomRouter(transportEntry.roomId);
    ensurePeerJoined(transportEntry.roomId, transportEntry.peerId);

    if (state.producers.has(producerId)) {
        throw new Error(`Producer already exists: ${producerId}`);
    }
    ensureEntityCapacity(state.producers.size, MAX_TOTAL_PRODUCERS, 'Producer');
    ensureEntityCapacity(
        countEntitiesByRoom(state.producers, transportEntry.roomId),
        MAX_PRODUCERS_PER_ROOM,
        `Room ${transportEntry.roomId} producer`
    );
    if (!payload.rtpParameters || typeof payload.rtpParameters !== 'object') {
        throw new Error(`Producer ${producerId} requires valid rtpParameters.`);
    }

    const producer = await transportEntry.transport.produce({
        kind,
        rtpParameters: payload.rtpParameters,
        appData: { producerId, trackType }
    });

    producer.on('transportclose', () => {
        safeCloseProducerById(producerId);
    });
    producer.on('close', () => {
        safeCloseProducerById(producerId);
    });

    state.producers.set(producerId, {
        roomId: transportEntry.roomId,
        peerId: transportEntry.peerId,
        transportId,
        producer,
        kind,
        trackType
    });

    return makeSuccess(
        id,
        `Producer ${producerId} created.`,
        {
            producerId,
            kind,
            trackType,
            mediasoupProducerId: producer.id
        },
        router
    );
}

async function opPauseProducer(id, payload) {
    const producerId = requireString(payload.producerId, 'producerId');
    const producerEntry = state.producers.get(producerId);
    if (!producerEntry || !producerEntry.producer) {
        throw new Error(`Producer not found: ${producerId}`);
    }

    const peerId = typeof payload.peerId === 'string' ? payload.peerId.trim() : '';
    if (peerId.length > 0 && producerEntry.peerId !== peerId) {
        throw new Error(`Producer ${producerId} is not owned by peer ${peerId}.`);
    }

    if (!producerEntry.producer.paused) {
        await producerEntry.producer.pause();
    }

    return makeSuccess(
        id,
        `Producer ${producerId} paused.`,
        {
            producerId,
            roomId: producerEntry.roomId,
            peerId: producerEntry.peerId,
            kind: producerEntry.kind,
            trackType: producerEntry.trackType || null,
            paused: producerEntry.producer.paused
        },
        getRoomRouter(producerEntry.roomId)
    );
}

async function opResumeProducer(id, payload) {
    const producerId = requireString(payload.producerId, 'producerId');
    const producerEntry = state.producers.get(producerId);
    if (!producerEntry || !producerEntry.producer) {
        throw new Error(`Producer not found: ${producerId}`);
    }

    const peerId = typeof payload.peerId === 'string' ? payload.peerId.trim() : '';
    if (peerId.length > 0 && producerEntry.peerId !== peerId) {
        throw new Error(`Producer ${producerId} is not owned by peer ${peerId}.`);
    }

    if (producerEntry.producer.paused) {
        await producerEntry.producer.resume();
    }

    return makeSuccess(
        id,
        `Producer ${producerId} resumed.`,
        {
            producerId,
            roomId: producerEntry.roomId,
            peerId: producerEntry.peerId,
            kind: producerEntry.kind,
            trackType: producerEntry.trackType || null,
            paused: producerEntry.producer.paused
        },
        getRoomRouter(producerEntry.roomId)
    );
}

async function opCloseProducer(id, payload) {
    const producerId = requireString(payload.producerId, 'producerId');
    const producerEntry = state.producers.get(producerId);
    if (!producerEntry || !producerEntry.producer) {
        throw new Error(`Producer not found: ${producerId}`);
    }

    const peerId = typeof payload.peerId === 'string' ? payload.peerId.trim() : '';
    if (peerId.length > 0 && producerEntry.peerId !== peerId) {
        throw new Error(`Producer ${producerId} is not owned by peer ${peerId}.`);
    }

    const roomId = producerEntry.roomId;
    const ownerPeerId = producerEntry.peerId;
    const kind = producerEntry.kind;
    const trackType = producerEntry.trackType || null;

    safeCloseProducerById(producerId);

    for (const [consumerId, consumerEntry] of [...state.consumers.entries()]) {
        if (consumerEntry.producerId === producerId) {
            safeCloseConsumerById(consumerId);
        }
    }

    return makeSuccess(
        id,
        `Producer ${producerId} closed.`,
        {
            producerId,
            roomId,
            peerId: ownerPeerId,
            kind,
            trackType,
            closed: true
        },
        getRoomRouter(roomId)
    );
}

/**
 * Creates paused consumer for a producer after consumability checks and records consumer metadata.
 * @param {string} id Correlation id of request.
 * @param {object} payload Request payload containing consumer/producers/rtpCapabilities details.
 * @returns {Promise<object>} Success response with consumer identifiers and RTP parameters.
 */
async function opConsume(id, payload) {
    const transportId = requireString(payload.transportId, 'transportId');
    const producerId = requireString(payload.producerId, 'producerId');
    const consumerId = typeof payload.consumerId === 'string' && payload.consumerId.trim().length > 0
        ? payload.consumerId.trim()
        : `${producerId}-consumer-${Date.now()}`;
    const transportEntry = getTransportEntry(transportId);
    const producerEntry = state.producers.get(producerId);
    const router = getRoomRouter(transportEntry.roomId);
    ensurePeerJoined(transportEntry.roomId, transportEntry.peerId);

    if (!producerEntry) {
        throw new Error(`Producer not found: ${producerId}`);
    }
    if (state.consumers.has(consumerId)) {
        throw new Error(`Consumer already exists: ${consumerId}`);
    }
    ensureEntityCapacity(state.consumers.size, MAX_TOTAL_CONSUMERS, 'Consumer');
    ensureEntityCapacity(
        countEntitiesByRoom(state.consumers, transportEntry.roomId),
        MAX_CONSUMERS_PER_ROOM,
        `Room ${transportEntry.roomId} consumer`
    );
    if (!payload.rtpCapabilities || typeof payload.rtpCapabilities !== 'object') {
        throw new Error(`Consumer ${consumerId} requires valid rtpCapabilities.`);
    }
    if (!producerEntry.producer) {
        throw new Error(`Producer ${producerId} is not attached to mediasoup transport.`);
    }
    if (producerEntry.roomId !== transportEntry.roomId) {
        throw new Error(`Producer ${producerId} belongs to another room.`);
    }
    if (!router?.canConsume({ producerId: producerEntry.producer.id, rtpCapabilities: payload.rtpCapabilities })) {
        throw new Error(`Producer ${producerId} is not consumable with provided rtpCapabilities.`);
    }

    const consumer = await transportEntry.transport.consume({
        producerId: producerEntry.producer.id,
        rtpCapabilities: payload.rtpCapabilities,
        paused: true
    });

    consumer.on('transportclose', () => {
        safeCloseConsumerById(consumerId);
    });
    consumer.on('producerclose', () => {
        safeCloseConsumerById(consumerId);
    });
    consumer.on('close', () => {
        safeCloseConsumerById(consumerId);
    });

    const injectedStats = buildInjectedStatsFromPayload(payload);
    if (injectedStats) {
        const producerInjectedStats = normalizeInjectedStats(producerEntry.injectedStats);
        producerEntry.injectedStats = {
            totalPackets: producerInjectedStats.totalPackets + injectedStats.totalPackets,
            totalBytes: producerInjectedStats.totalBytes + injectedStats.totalBytes
        };
    }

    state.consumers.set(consumerId, {
        roomId: transportEntry.roomId,
        peerId: transportEntry.peerId,
        transportId,
        producerId,
        kind: consumer.kind,
        trackType: producerEntry.trackType || null,
        producerPeerId: producerEntry.peerId,
        consumer,
        injectedStats
    });

    return makeSuccess(
        id,
        `Consumer ${consumerId} created in paused mode.`,
        {
            consumerId,
            producerId,
            producerPeerId: producerEntry.peerId,
            mediasoupConsumerId: consumer.id,
            kind: consumer.kind,
            trackType: producerEntry.trackType || null,
            paused: consumer.paused,
            rtpParameters: consumer.rtpParameters
        },
        router
    );
}

/**
 * Resumes an existing paused consumer stream.
 * @param {string} id Correlation id of request.
 * @param {object} payload Request payload containing consumer id.
 * @returns {Promise<object>} Success response with updated consumer pause state.
 */
async function opResumeConsumer(id, payload) {
    const consumerId = requireString(payload.consumerId, 'consumerId');
    const consumerEntry = state.consumers.get(consumerId);
    if (!consumerEntry || !consumerEntry.consumer) {
        throw new Error(`Consumer not found: ${consumerId}`);
    }

    if (consumerEntry.consumer.paused) {
        await consumerEntry.consumer.resume();
    }

    return makeSuccess(
        id,
        `Consumer ${consumerId} resumed.`,
        {
            consumerId,
            producerId: consumerEntry.producerId,
            paused: consumerEntry.consumer.paused,
            kind: consumerEntry.kind,
            trackType: consumerEntry.trackType || null
        },
        getRoomRouter(consumerEntry.roomId)
    );
}

function makeSuccess(id, message, data, router) {
    const capabilitiesRouter = router || state.capabilityRouter;
    return {
        id,
        ok: true,
        message,
        data: data || {},
        signalingEvents: [],
        backend: {
            engine: 'mediasoup',
            version: mediasoup.version,
            routerRtpCapabilities: capabilitiesRouter?.rtpCapabilities || null
        }
    };
}

function makeFailure(id, code, message) {
    return {
        id,
        ok: false,
        code,
        message,
        signalingEvents: [],
        backend: {
            engine: 'mediasoup',
            version: mediasoup.version,
            routerRtpCapabilities: state.capabilityRouter?.rtpCapabilities || null
        }
    };
}

function requireString(value, fieldName) {
    if (typeof value !== 'string' || value.trim().length === 0) {
        throw new Error(`Field "${fieldName}" must be a non-empty string.`);
    }
    const normalized = value.trim();
    if (normalized.length > MAX_STRING_FIELD_LENGTH) {
        failWithCode(
            'invalid_request',
            `Field \"${fieldName}\" exceeds max length ${MAX_STRING_FIELD_LENGTH}.`
        );
    }
    return normalized;
}

function ensurePeerSet(roomId) {
    let peers = state.roomPeers.get(roomId);
    if (!peers) {
        peers = new Set();
        state.roomPeers.set(roomId, peers);
    }
    return peers;
}

function getTransportEntry(transportId) {
    const transportEntry = state.transports.get(transportId);
    if (!transportEntry) {
        throw new Error(`Transport not found: ${transportId}`);
    }
    return transportEntry;
}

async function createWorkerContext() {
    const workerId = `worker-${++state.workerSerial}`;
    const worker = await mediasoup.createWorker({
        rtcMinPort: RTC_MIN_PORT,
        rtcMaxPort: RTC_MAX_PORT,
        logLevel: LOG_LEVEL,
        logTags: LOG_TAGS
    });

    const webRtcServer = await worker.createWebRtcServer({
        listenInfos: [
            {
                protocol: 'udp',
                ip: RTC_LISTEN_IP,
                announcedAddress: ANNOUNCED_IP
            },
            {
                protocol: 'tcp',
                ip: RTC_LISTEN_IP,
                announcedAddress: ANNOUNCED_IP
            }
        ]
    });

    const workerContext = {
        workerId,
        worker,
        webRtcServer,
        createdAt: Date.now()
    };

    worker.on('died', () => {
        const prefix = `worker died pid=${worker.pid} workerId=${workerId}`;
        if (state.shuttingDown) {
            log(`${prefix} (shutdown in progress)`);
            return;
        }

        metrics.workerDeaths++;
        log('worker_died', 'error', { workerId, pid: worker.pid });
        removeWorkerContext(workerContext);

        if (state.capabilityWorkerId === workerId) {
            try {
                state.capabilityRouter?.close();
            } catch (_) {
            }
            state.capabilityRouter = null;
            state.capabilityWorkerId = null;
        }

        const affectedRooms = [];
        for (const [roomId, routerEntry] of state.routers.entries()) {
            if (routerEntry?.workerId === workerId) {
                affectedRooms.push(roomId);
            }
        }
        for (const roomId of affectedRooms) {
            closeRoom(roomId);
        }

        ensureWorkerPool().catch((error) => {
            log(`failed to replenish worker pool after ${workerId} death: ${error?.message || String(error)}`);
        });
    });

    log(`worker created pid=${worker.pid} workerId=${workerId}`);
    return workerContext;
}

function removeWorkerContext(workerContext) {
    state.workerPool = state.workerPool.filter((context) => context.workerId !== workerContext.workerId);
    if (state.workerCursor >= state.workerPool.length) {
        state.workerCursor = 0;
    }
}

function getWorkerContextById(workerId) {
    if (typeof workerId !== 'string' || workerId.length === 0) {
        return null;
    }
    for (const context of state.workerPool) {
        if (context.workerId === workerId) {
            return context;
        }
    }
    return null;
}

function workerDiagnostics() {
    const loadSnapshot = buildWorkerLoadSnapshot();
    return state.workerPool.map((context) => ({
        ...(loadSnapshot.get(context.workerId) || { roomCount: 0, peerCount: 0 }),
        workerId: context.workerId,
        pid: context.worker?.pid || null,
        uptimeMs: Math.max(0, Date.now() - context.createdAt),
        maxPeersPerWorker: MAX_PEERS_PER_WORKER,
        hasCapabilityRouter: state.capabilityWorkerId === context.workerId
    }));
}

function pickWorkerContextForRoom(roomId) {
    if (state.workerPool.length === 0) {
        throw new Error('No active mediasoup workers available.');
    }

    const roomPeers = state.roomPeers.get(roomId);
    const roomPeerCount = roomPeers ? roomPeers.size : 0;
    const loadSnapshot = buildWorkerLoadSnapshot();
    const poolSize = state.workerPool.length;
    let selectedCandidate = null;
    for (let offset = 0; offset < poolSize; offset += 1) {
        const index = (state.workerCursor + offset) % poolSize;
        const candidate = state.workerPool[index];
        if (!candidate || !candidate.worker || candidate.worker.closed || !candidate.webRtcServer || candidate.webRtcServer.closed) {
            continue;
        }
        const workerLoad = loadSnapshot.get(candidate.workerId) || { roomCount: 0, peerCount: 0 };
        const projectedPeerCount = workerLoad.peerCount + roomPeerCount;
        if (projectedPeerCount > MAX_PEERS_PER_WORKER) {
            continue;
        }

        if (!selectedCandidate) {
            selectedCandidate = {
                index,
                candidate,
                projectedPeerCount,
                roomCount: workerLoad.roomCount
            };
            continue;
        }

        if (projectedPeerCount < selectedCandidate.projectedPeerCount) {
            selectedCandidate = {
                index,
                candidate,
                projectedPeerCount,
                roomCount: workerLoad.roomCount
            };
            continue;
        }

        if (projectedPeerCount === selectedCandidate.projectedPeerCount
            && workerLoad.roomCount < selectedCandidate.roomCount) {
            selectedCandidate = {
                index,
                candidate,
                projectedPeerCount,
                roomCount: workerLoad.roomCount
            };
        }
    }

    if (!selectedCandidate) {
        failWithCode(
            'capacity_exceeded',
            `All workers reached peer capacity (${MAX_PEERS_PER_WORKER} peers per worker).`
        );
    }

    state.workerCursor = (selectedCandidate.index + 1) % poolSize;
    state.workerByRoom.set(roomId, selectedCandidate.candidate.workerId);
    return selectedCandidate.candidate;
}

function getRoomWorkerContext(roomId) {
    const workerId = state.workerByRoom.get(roomId);
    return getWorkerContextById(workerId);
}

function getRoomRouter(roomId) {
    const entry = state.routers.get(roomId);
    if (!entry || !entry.router || entry.router.closed) {
        return null;
    }
    return entry.router;
}

/**
 * Maintains worker pool size and health up to configured WORKER_COUNT.
 * @returns {Promise<Array<object>>} Active worker context list.
 */
async function ensureWorkerPool() {
    state.workerPool = state.workerPool.filter((context) => context?.worker && !context.worker.closed && context.webRtcServer && !context.webRtcServer.closed);
    if (state.workerCursor >= state.workerPool.length) {
        state.workerCursor = 0;
    }

    while (state.workerPool.length < WORKER_COUNT) {
        try {
            const workerContext = await createWorkerContext();
            state.workerPool.push(workerContext);
        } catch (error) {
            if (state.workerPool.length === 0) {
                throw new Error(`Failed to create mediasoup worker pool: ${error?.message || String(error)}`);
            }
            log(`worker pool partially available (${state.workerPool.length}/${WORKER_COUNT}): ${error?.message || String(error)}`);
            break;
        }
    }

    if (state.workerPool.length === 0) {
        throw new Error('No mediasoup workers available.');
    }

    return state.workerPool;
}

/**
 * Ensures singleton capability router exists and is bound to a live worker.
 * @returns {Promise<object>} Active capability router.
 */
async function ensureCapabilityRouter() {
    await ensureWorkerPool();
    if (state.capabilityRouter && !state.capabilityRouter.closed) {
        const activeContext = getWorkerContextById(state.capabilityWorkerId);
        if (activeContext) {
            return state.capabilityRouter;
        }
        try {
            state.capabilityRouter.close();
        } catch (_) {
        }
    }

    const capabilityContext = state.workerPool[0];
    state.capabilityRouter = await capabilityContext.worker.createRouter({ mediaCodecs: MEDIA_CODECS });
    state.capabilityWorkerId = capabilityContext.workerId;
    return state.capabilityRouter;
}

/**
 * Returns active router for room or creates one on selected worker with codec configuration.
 * @param {string} roomId Room identifier.
 * @returns {Promise<object>} Active room router instance.
 */
async function ensureRoomRouter(roomId) {
    await ensureWorkerPool();
    const existing = state.routers.get(roomId);
    if (existing && existing.router && !existing.router.closed) {
        const roomWorker = getRoomWorkerContext(roomId);
        if (roomWorker) {
            return existing.router;
        }

        try {
            existing.router.close();
        } catch (_) {
        }
        state.routers.delete(roomId);
        state.workerByRoom.delete(roomId);
    }

    const selectedWorker = getRoomWorkerContext(roomId) || pickWorkerContextForRoom(roomId);
    const router = await selectedWorker.worker.createRouter({ mediaCodecs: MEDIA_CODECS });
    state.routers.set(roomId, {
        roomId,
        workerId: selectedWorker.workerId,
        router,
        createdAt: Date.now()
    });
    return router;
}

function safeCloseTransportById(transportId) {
    const entry = state.transports.get(transportId);
    if (!entry) {
        return;
    }

    state.transports.delete(transportId);

    const transport = entry.transport;
    entry.transport = null;

    try {
        transport?.close();
    } catch (_) {
    }
}

function safeCloseProducerById(producerId) {
    const entry = state.producers.get(producerId);
    if (!entry) {
        return;
    }

    state.producers.delete(producerId);

    const producer = entry.producer;
    entry.producer = null;

    try {
        producer?.close();
    } catch (_) {
    }
}
function safeCloseConsumerById(consumerId) {
    const entry = state.consumers.get(consumerId);
    if (!entry) {
        return;
    }

    state.consumers.delete(consumerId);

    const consumer = entry.consumer;
    entry.consumer = null;

    try {
        consumer?.close();
    } catch (_) {
    }
}
function closePeerResources(roomId, peerId) {
    for (const [consumerId, entry] of [...state.consumers.entries()]) {
        if (entry.roomId === roomId && entry.peerId === peerId) {
            safeCloseConsumerById(consumerId);
        }
    }
    for (const [producerId, entry] of [...state.producers.entries()]) {
        if (entry.roomId === roomId && entry.peerId === peerId) {
            safeCloseProducerById(producerId);
        }
    }
    for (const [transportId, entry] of [...state.transports.entries()]) {
        if (entry.roomId === roomId && entry.peerId === peerId) {
            safeCloseTransportById(transportId);
        }
    }
    peerRateWindows.delete(peerId);
}

function closeRoom(roomId) {
    const peers = state.roomPeers.get(roomId);
    if (peers) {
        for (const peerId of peers) {
            closePeerResources(roomId, peerId);
        }
    }
    state.roomPeers.delete(roomId);

    const routerEntry = state.routers.get(roomId);
    if (routerEntry?.router) {
        try {
            routerEntry.router.close();
        } catch (_) {
        }
        state.routers.delete(roomId);
    }
    state.workerByRoom.delete(roomId);
}

function normalizeWsPath(pathValue) {
    if (typeof pathValue !== 'string' || pathValue.trim().length === 0) {
        return '/ws';
    }
    const normalized = pathValue.startsWith('/') ? pathValue : `/${pathValue}`;
    return normalized.replace(/\/+$/, '') || '/';
}

function log(message, level = 'info', extra = null) {
    const entry = {
        ts: new Date().toISOString(),
        svc: 'meetspace-mediasoup-backend',
        region: REGION,
        level,
        msg: message
    };
    if (extra) Object.assign(entry, extra);
    process.stdout.write(JSON.stringify(entry) + '\n');
}

function audit(peerId, operation, detail) {
    const entry = { ts: Date.now(), peerId, operation, detail };
    auditLog.push(entry);
    if (auditLog.length > MAX_AUDIT_ENTRIES) auditLog.shift();
}

function verifyAuthToken(tokenString) {
    if (!AUTH_ENABLED) return true;
    if (!tokenString || typeof tokenString !== 'string') return false;
    const parts = tokenString.split('.');
    if (parts.length !== 2) return false;
    const [payload, sig] = parts;
    try {
        const expected = crypto.createHmac('sha256', AUTH_SECRET).update(payload).digest('hex');
        return crypto.timingSafeEqual(Buffer.from(sig, 'hex'), Buffer.from(expected, 'hex'));
    } catch (_) {
        return false;
    }
}

/**
 * Builds health snapshot for readiness/liveness endpoint consumption.
 * @returns {object} Health payload with worker, room, peer, queue and runtime metrics.
 */
function buildHealthResponse() {
    const healthy = state.workerPool.length > 0 && !state.shuttingDown;
    return {
        ok: healthy,
        region: REGION,
        draining: state.shuttingDown,
        workers: state.workerPool.length,
        workersTarget: WORKER_COUNT,
        rooms: state.routers.size,
        peers: totalPeersCount(),
        transports: state.transports.size,
        producers: state.producers.size,
        consumers: state.consumers.size,
        wsClients: wss.clients.size,
        inflightOperations: state.inflightOperations,
        maxInflightOperations: MAX_INFLIGHT_OPERATIONS,
        globalPendingMessages: state.globalPendingMessages,
        maxGlobalPendingMessages: MAX_GLOBAL_PENDING_MESSAGES,
        maxPeersPerWorker: MAX_PEERS_PER_WORKER,
        uptimeSeconds: Math.floor(process.uptime())
    };
}

/**
 * Renders internal counters and gauges in Prometheus text exposition format.
 * @returns {string} Prometheus metrics body.
 */
function buildPrometheusMetrics() {
    const lines = [];
    const g = (name, help, value) => {
        lines.push(`# HELP ${name} ${help}`);
        lines.push(`# TYPE ${name} gauge`);
        lines.push(`${name}{region="${REGION}"} ${value}`);
    };
    const c = (name, help, value) => {
        lines.push(`# HELP ${name} ${help}`);
        lines.push(`# TYPE ${name} counter`);
        lines.push(`${name}{region="${REGION}"} ${value}`);
    };
    g('meetspace_workers', 'Active mediasoup workers', state.workerPool.length);
    g('meetspace_rooms', 'Active rooms', state.routers.size);
    g('meetspace_peers', 'Connected peers', totalPeersCount());
    g('meetspace_transports', 'Active transports', state.transports.size);
    g('meetspace_producers', 'Active producers', state.producers.size);
    g('meetspace_consumers', 'Active consumers', state.consumers.size);
    g('meetspace_ws_clients', 'WebSocket clients', wss.clients.size);
    g('meetspace_inflight_operations', 'Currently inflight signaling operations', state.inflightOperations);
    g('meetspace_pending_ws_messages', 'Queued websocket signaling messages', state.globalPendingMessages);
    g('meetspace_uptime_seconds', 'Process uptime', Math.floor(process.uptime()));
    g('meetspace_max_peers_per_worker', 'Configured max peers per worker', MAX_PEERS_PER_WORKER);
    g('meetspace_max_inflight_operations', 'Configured max inflight signaling operations', MAX_INFLIGHT_OPERATIONS);
    g('meetspace_max_global_pending_messages', 'Configured max pending websocket messages', MAX_GLOBAL_PENDING_MESSAGES);
    c('meetspace_ops_total', 'Total operations processed', metrics.opsTotal);
    c('meetspace_ops_success', 'Successful operations', metrics.opsSuccess);
    c('meetspace_ops_failed', 'Failed operations', metrics.opsFailed);
    c('meetspace_ws_connections', 'Total WS connections', metrics.wsConnections);
    c('meetspace_ws_disconnections', 'Total WS disconnections', metrics.wsDisconnections);
    c('meetspace_auth_rejections', 'Auth rejections', metrics.authRejections);
    c('meetspace_worker_deaths', 'Worker crashes', metrics.workerDeaths);
    c('meetspace_overload_rejections', 'Requests rejected due to overload protection', metrics.overloadRejections);
    if (metrics.opsTotal > 0) {
        g('meetspace_op_avg_latency_ms', 'Average operation latency ms', Math.round(metrics.opLatencySum / metrics.opsTotal));
    }
    return lines.join('\n') + '\n';
}

let qosTimer = null;
function startQosCollectionLoop() {
    if (qosTimer) return;
    qosTimer = setInterval(async () => {
        try {
            for (const [producerId, entry] of state.producers.entries()) {
                if (!entry.producer) continue;
                try {
                    const stats = await entry.producer.getStats();
                    const rows = Array.isArray(stats) ? stats : [];
                    for (const row of rows) {
                        if (!row || typeof row !== 'object') continue;
                        log('qos_producer', 'info', {
                            producerId, roomId: entry.roomId, peerId: entry.peerId,
                            kind: entry.kind, trackType: entry.trackType,
                            bitrate: toFiniteNumber(row.bitrate),
                            jitter: toFiniteNumber(row.jitter),
                            packetCount: toFiniteNumber(row.packetCount),
                            packetsLost: toFiniteNumber(row.packetsLost),
                            roundTripTime: toFiniteNumber(row.roundTripTime)
                        });
                    }
                } catch (_) {}
            }
            for (const [consumerId, entry] of state.consumers.entries()) {
                if (!entry.consumer) continue;
                try {
                    const stats = await entry.consumer.getStats();
                    const rows = Array.isArray(stats) ? stats : [];
                    for (const row of rows) {
                        if (!row || typeof row !== 'object') continue;
                        log('qos_consumer', 'info', {
                            consumerId, roomId: entry.roomId, peerId: entry.peerId,
                            kind: entry.kind, trackType: entry.trackType,
                            bitrate: toFiniteNumber(row.bitrate),
                            jitter: toFiniteNumber(row.jitter),
                            packetCount: toFiniteNumber(row.packetCount),
                            packetsLost: toFiniteNumber(row.packetsLost),
                            roundTripTime: toFiniteNumber(row.roundTripTime)
                        });
                    }
                } catch (_) {}
            }
        } catch (_) {}
    }, QOS_INTERVAL_MS);
    qosTimer.unref();
}

/**
 * Performs graceful backend shutdown with draining, mediasoup resource cleanup, and server close.
 * @returns {Promise<void>} Resolves when shutdown sequence reaches process-exit handoff.
 */
async function shutdown() {
    if (state.shuttingDown) {
        return;
    }
    state.shuttingDown = true;
    log('shutdown_start', 'warn', { drainTimeoutMs: DRAIN_TIMEOUT_MS });
    if (heartbeatTimer) {
        clearInterval(heartbeatTimer);
        heartbeatTimer = null;
    }
    if (qosTimer) {
        clearInterval(qosTimer);
        qosTimer = null;
    }

    // Graceful drain: wait for rooms to empty
    const drainStart = Date.now();
    while (state.routers.size > 0 && (Date.now() - drainStart) < DRAIN_TIMEOUT_MS) {
        await new Promise((r) => setTimeout(r, 500));
    }
    log('shutdown_drain_done', 'info', { remainingRooms: state.routers.size, elapsed: Date.now() - drainStart });

    try {
        for (const roomId of [...state.routers.keys()]) {
            closeRoom(roomId);
        }

        if (state.capabilityRouter && !state.capabilityRouter.closed) {
            state.capabilityRouter.close();
        }
        state.capabilityRouter = null;
        state.capabilityWorkerId = null;

        for (const workerContext of state.workerPool) {
            try {
                if (workerContext.webRtcServer && !workerContext.webRtcServer.closed) {
                    workerContext.webRtcServer.close();
                }
            } catch (_) {
            }
            try {
                workerContext.worker?.close();
            } catch (_) {
            }
        }
        state.workerPool = [];
        state.workerByRoom.clear();
        state.workerCursor = 0;
    } catch (_) {
    }

    try {
        wss.clients.forEach((ws) => ws.close());
    } catch (_) {
    }
    state.inflightOperations = 0;
    state.globalPendingMessages = 0;

    server.close(() => {
        process.exit(0);
    });

    setTimeout(() => process.exit(0), 1500).unref();
}

process.on('SIGINT', shutdown);
process.on('SIGTERM', shutdown);

(async () => {
    await ensureWorkerPool();
    await ensureCapabilityRouter();
    startHeartbeatLoop();
    startQosCollectionLoop();
    server.listen(PORT, HOST, () => {
        log('server_start', 'info', {
            listen: `${TLS_ENABLED ? 'wss' : 'ws'}://${HOST}:${PORT}${WS_PATH}`,
            rtcListenIp: RTC_LISTEN_IP,
            announcedIp: ANNOUNCED_IP || 'n/a',
            rtcPorts: `${RTC_MIN_PORT}-${RTC_MAX_PORT}`,
            workerPool: `${state.workerPool.length}/${WORKER_COUNT}`,
            region: REGION,
            authEnabled: AUTH_ENABLED,
            iceServers: ICE_SERVERS.length,
            qosIntervalMs: QOS_INTERVAL_MS,
            maxPeersPerWorker: MAX_PEERS_PER_WORKER,
            maxInflightOperations: MAX_INFLIGHT_OPERATIONS,
            maxGlobalPendingMessages: MAX_GLOBAL_PENDING_MESSAGES
        });
    });
})();
