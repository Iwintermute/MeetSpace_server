'use strict';

const http = require('node:http');
const https = require('node:https');
const fs = require('node:fs');
const os = require('node:os');
const mediasoup = require('mediasoup');
const { WebSocketServer } = require('ws');

function defaultWorkerCount() {
    const cpuCount = Array.isArray(os.cpus()) ? os.cpus().length : 1;
    return Math.max(1, Math.min(16, cpuCount));
}

function clampWorkerCount(value) {
    if (!Number.isFinite(value)) {
        return 1;
    }
    return Math.max(1, Math.min(64, Math.trunc(value)));
}

function readBoundedIntFromEnv(name, fallbackValue, minValue, maxValue) {
    const parsed = Number.parseInt(process.env[name] || '', 10);
    if (!Number.isFinite(parsed)) {
        return Math.max(minValue, Math.min(maxValue, Math.trunc(fallbackValue)));
    }
    return Math.max(minValue, Math.min(maxValue, Math.trunc(parsed)));
}

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
const MAX_STRING_FIELD_LENGTH = readBoundedIntFromEnv('MEDIASOUP_MAX_STRING_FIELD_LENGTH', 256, 16, 2048);
const LOG_LEVEL = process.env.MEDIASOUP_LOG_LEVEL || 'warn';
const LOG_TAGS = (process.env.MEDIASOUP_LOG_TAGS || 'info,ice,dtls,rtp,rtcp,srtp')
    .split(',')
    .map((item) => item.trim())
    .filter((item) => item.length > 0);

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
    shuttingDown: false
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

const requestHandler = (_req, res) => {
    res.writeHead(200, { 'content-type': 'application/json; charset=utf-8' });
    res.end(JSON.stringify({
        ok: true,
        service: 'meetspace-mediasoup-backend',
        path: WS_PATH,
        transport: TLS_ENABLED ? 'wss' : 'ws',
        workers: WORKER_COUNT,
        limits: {
            wsMaxConnections: WS_MAX_CONNECTIONS,
            wsMaxPayloadBytes: WS_MAX_PAYLOAD_BYTES,
            maxRooms: MAX_ROOMS,
            maxPeersPerRoom: MAX_PEERS_PER_ROOM,
            maxTransportsPerRoom: MAX_TRANSPORTS_PER_ROOM,
            maxProducersPerRoom: MAX_PRODUCERS_PER_ROOM,
            maxConsumersPerRoom: MAX_CONSUMERS_PER_ROOM
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
    log(`client connected from ${request.socket.remoteAddress || 'unknown'}`);
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
        runtimeState.queue.length = 0;
        log('client disconnected');
    });

    ws.on('error', (error) => {
        log(`client websocket error: ${error?.message || String(error)}`);
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

function handleWsMessage(ws, rawBuffer) {
    const runtimeState = socketRuntimeState.get(ws);
    if (!runtimeState || runtimeState.closed) {
        return;
    }
    if (runtimeState.queue.length >= WS_MAX_PENDING_MESSAGES_PER_SOCKET) {
        safeSend(ws, makeFailure(undefined, 'overloaded', 'WebSocket queue overflow. Reduce signaling rate.'));
        return;
    }

    const payloadText = Buffer.isBuffer(rawBuffer)
        ? rawBuffer.toString('utf8')
        : String(rawBuffer ?? '');
    runtimeState.queue.push(payloadText);
    drainSocketQueue(ws, runtimeState).catch((error) => {
        log(`socket queue drain failure: ${error?.message || String(error)}`);
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
            let requestPayload = null;
            try {
                requestPayload = JSON.parse(rawPayload);
            } catch (error) {
                safeSend(ws, makeFailure(undefined, 'invalid_json', `Invalid JSON: ${error.message}`));
                continue;
            }

            const response = await withOperationTimeout(handleRequest(requestPayload), WS_OPERATION_TIMEOUT_MS)
                .catch((error) => makeFailure(requestPayload?.id, 'timeout', error?.message || String(error)));
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
                runtimeState.queue.length = 0;
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
}

function ensurePeerJoined(roomId, peerId) {
    const peers = state.roomPeers.get(roomId);
    if (peers?.has(peerId)) {
        return;
    }
    failWithCode('peer_not_joined', `Peer ${peerId} is not joined in room ${roomId}.`);
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

    try {
        switch (operation) {
            case 'system.getCapabilities':
                return await opGetCapabilities(id);
            case 'system.getMediaStats':
                return await opGetMediaStats(id, payload);
            case 'worker.createRouter':
                return await opCreateRouter(id, payload);
            case 'router.joinPeer':
                return await opJoinPeer(id, payload);
            case 'router.leavePeer':
                return await opLeavePeer(id, payload);
            case 'router.closePeer':
                return await opClosePeer(id, payload);
            case 'router.createWebRtcTransport':
                return await opCreateWebRtcTransport(id, payload);
            case 'transport.connectDtls':
                return await opConnectDtls(id, payload);
            case 'transport.addIceCandidate':
                return await opAddIceCandidate(id, payload);
            case 'transport.produce':
                return await opProduce(id, payload);
            case 'producer.pause':
                return await opPauseProducer(id, payload);
            case 'producer.resume':
                return await opResumeProducer(id, payload);
            case 'producer.close':
                return await opCloseProducer(id, payload);
            case 'transport.consume':
                return await opConsume(id, payload);
            case 'consumer.resume':
                return await opResumeConsumer(id, payload);
            default:
                return makeFailure(id, 'unsupported_operation', `Unsupported operation: ${operation}`);
        }
    } catch (error) {
        const code = typeof error?.code === 'string' && error.code.length > 0
            ? error.code
            : 'internal_error';
        return makeFailure(id, code, error?.message || String(error));
    }
}

async function opGetCapabilities(id) {
    const router = await ensureCapabilityRouter();
    return makeSuccess(id, 'Mediasoup backend capabilities.', {}, router);
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
    }
    return {
        rowsCount: normalizedRows.length,
        totalBytes,
        totalPackets
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
        let summary = { rowsCount: 0, totalBytes: 0, totalPackets: 0 };
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
            statsError
        });
    }

    for (const [producerId, entry] of state.producers.entries()) {
        if (!roomFilter(entry.roomId)) {
            continue;
        }
        let summary = { rowsCount: 0, totalBytes: 0, totalPackets: 0 };
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
            statsError
        });
    }

    for (const [consumerId, entry] of state.consumers.entries()) {
        if (!roomFilter(entry.roomId)) {
            continue;
        }
        let summary = { rowsCount: 0, totalBytes: 0, totalPackets: 0 };
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

async function opJoinPeer(id, payload) {
    const roomId = requireString(payload.roomId, 'roomId');
    const peerId = requireString(payload.peerId, 'peerId');
    ensureRoomCapacityForJoin(roomId, peerId);
    const router = await ensureRoomRouter(roomId);
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
            sctpParameters: transport.sctpParameters
        },
        router
    );
}

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

        log(`${prefix}; draining affected rooms`);
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
    return state.workerPool.map((context) => ({
        workerId: context.workerId,
        pid: context.worker?.pid || null,
        uptimeMs: Math.max(0, Date.now() - context.createdAt),
        roomCount: [...state.workerByRoom.values()].filter((workerId) => workerId === context.workerId).length,
        hasCapabilityRouter: state.capabilityWorkerId === context.workerId
    }));
}

function pickWorkerContextForRoom(roomId) {
    if (state.workerPool.length === 0) {
        throw new Error('No active mediasoup workers available.');
    }

    const poolSize = state.workerPool.length;
    for (let offset = 0; offset < poolSize; offset += 1) {
        const index = (state.workerCursor + offset) % poolSize;
        const candidate = state.workerPool[index];
        if (!candidate || !candidate.worker || candidate.worker.closed || !candidate.webRtcServer || candidate.webRtcServer.closed) {
            continue;
        }
        state.workerCursor = (index + 1) % poolSize;
        state.workerByRoom.set(roomId, candidate.workerId);
        return candidate;
    }

    throw new Error('No healthy mediasoup worker contexts available.');
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

function log(message) {
    const now = new Date().toISOString();
    process.stdout.write(`[${now}] [meetspace-mediasoup-backend] ${message}\n`);
}

async function shutdown() {
    if (state.shuttingDown) {
        return;
    }
    state.shuttingDown = true;
    if (heartbeatTimer) {
        clearInterval(heartbeatTimer);
        heartbeatTimer = null;
    }
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
    server.listen(PORT, HOST, () => {
        log(`listening ${TLS_ENABLED ? 'wss' : 'ws'}://${HOST}:${PORT}${WS_PATH}`);
        log(`rtc listen ip ${RTC_LISTEN_IP}, announced ip ${ANNOUNCED_IP || 'n/a'}`);
        log(`rtc ports ${RTC_MIN_PORT}-${RTC_MAX_PORT}`);
        log(`worker pool size ${state.workerPool.length}/${WORKER_COUNT}`);
    });
})();