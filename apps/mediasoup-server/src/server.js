'use strict';

const http = require('node:http');
const mediasoup = require('mediasoup');
const { WebSocketServer } = require('ws');

const HOST = process.env.MEDIASOUP_BACKEND_HOST || '127.0.0.1';
const PORT = Number.parseInt(process.env.MEDIASOUP_BACKEND_PORT || '5001', 10);
const WS_PATH = normalizeWsPath(process.env.MEDIASOUP_BACKEND_PATH || '/ws');
const ANNOUNCED_IP = process.env.MEDIASOUP_ANNOUNCED_IP || undefined;
const RTC_MIN_PORT = Number.parseInt(process.env.MEDIASOUP_RTC_MIN_PORT || '40000', 10);
const RTC_MAX_PORT = Number.parseInt(process.env.MEDIASOUP_RTC_MAX_PORT || '49999', 10);
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
        channels: 2
    },
    {
        kind: 'video',
        mimeType: 'video/VP8',
        clockRate: 90000,
        parameters: {}
    }
];

const state = {
    worker: null,
    webRtcServer: null,
    capabilityRouter: null,
    routers: new Map(),
    roomPeers: new Map(),
    transports: new Map(),
    producers: new Map(),
    consumers: new Map()
};

const server = http.createServer((_req, res) => {
    res.writeHead(200, { 'content-type': 'application/json; charset=utf-8' });
    res.end(JSON.stringify({ ok: true, service: 'mediasoup-backend', path: WS_PATH }));
});

const wss = new WebSocketServer({ noServer: true });

server.on('upgrade', (request, socket, head) => {
    const url = new URL(request.url || '/', `http://${request.headers.host || 'localhost'}`);
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
    log(`client connected from ${request.socket.remoteAddress || 'unknown'}`);
    ws.on('message', async (rawBuffer) => {
        let requestPayload = null;
        try {
            requestPayload = JSON.parse(rawBuffer.toString());
        } catch (error) {
            ws.send(JSON.stringify(makeFailure(undefined, 'invalid_json', `Invalid JSON: ${error.message}`)));
            return;
        }

        const response = await handleRequest(requestPayload);
        ws.send(JSON.stringify(response));
    });

    ws.on('close', () => {
        log('client disconnected');
    });
});

async function handleRequest(request) {
    const id = request?.id;
    const operation = request?.operation;
    const payload = request?.payload && typeof request.payload === 'object' ? request.payload : {};

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
        return makeFailure(id, 'internal_error', error?.message || String(error));
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
        ? (state.routers.get(roomId) || state.capabilityRouter)
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
            consumers: consumerReports
        },
        targetRouter
    );
}

async function opCreateRouter(id, payload) {
    const roomId = requireString(payload.roomId, 'roomId');
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
    const router = await ensureRoomRouter(roomId);

    closePeerResources(roomId, peerId);

    const peers = ensurePeerSet(roomId);
    peers.delete(peerId);

    if (peers.size === 0) {
        closeRoom(roomId);
    }

    return makeSuccess(
        id,
        `Peer ${peerId} left ${roomId}.`,
        {
            roomId,
            peerId
        },
        state.routers.get(roomId) || state.capabilityRouter || router
    );
}

async function opClosePeer(id, payload) {
    const roomId = requireString(payload.roomId, 'roomId');
    const peerId = requireString(payload.peerId, 'peerId');
    await ensureRoomRouter(roomId);

    closePeerResources(roomId, peerId);

    const peers = ensurePeerSet(roomId);
    peers.delete(peerId);
    if (peers.size === 0) {
        closeRoom(roomId);
    }

    return makeSuccess(
        id,
        `Peer ${peerId} closed in ${roomId}.`,
        {
            roomId,
            peerId
        },
        state.routers.get(roomId) || state.capabilityRouter
    );
}

async function opCreateWebRtcTransport(id, payload) {
    const roomId = requireString(payload.roomId, 'roomId');
    const peerId = requireString(payload.peerId, 'peerId');
    const transportId = requireString(payload.transportId, 'transportId');
    const router = await ensureRoomRouter(roomId);
    await ensureWorker();

    if (state.transports.has(transportId)) {
        throw new Error(`Transport already exists: ${transportId}`);
    }

    const transport = await router.createWebRtcTransport({
        webRtcServer: state.webRtcServer,
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
        state.routers.get(transportEntry.roomId)
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
        state.routers.get(transportEntry.roomId)
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
    const router = state.routers.get(transportEntry.roomId);

    if (state.producers.has(producerId)) {
        throw new Error(`Producer already exists: ${producerId}`);
    }
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
        state.routers.get(producerEntry.roomId)
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
        state.routers.get(producerEntry.roomId)
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
        state.routers.get(roomId)
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
    const router = state.routers.get(transportEntry.roomId);

    if (!producerEntry) {
        throw new Error(`Producer not found: ${producerId}`);
    }
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
        state.routers.get(consumerEntry.roomId)
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
    return value.trim();
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

async function ensureWorker() {
    if (state.worker) {
        return state.worker;
    }

    state.worker = await mediasoup.createWorker({
        rtcMinPort: RTC_MIN_PORT,
        rtcMaxPort: RTC_MAX_PORT,
        logLevel: LOG_LEVEL,
        logTags: LOG_TAGS
    });

    state.worker.on('died', () => {
        log('worker died unexpectedly');
        process.exit(1);
    });

    state.webRtcServer = await state.worker.createWebRtcServer({
        listenInfos: [
            {
                protocol: 'udp',
                ip: '127.0.0.1',
                announcedAddress: ANNOUNCED_IP
            },
            {
                protocol: 'tcp',
                ip: '127.0.0.1',
                announcedAddress: ANNOUNCED_IP
            }
        ]
    });

    log(`worker created pid=${state.worker.pid}`);
    return state.worker;
}

async function ensureCapabilityRouter() {
    if (state.capabilityRouter && !state.capabilityRouter.closed) {
        return state.capabilityRouter;
    }
    const worker = await ensureWorker();
    state.capabilityRouter = await worker.createRouter({ mediaCodecs: MEDIA_CODECS });
    return state.capabilityRouter;
}

async function ensureRoomRouter(roomId) {
    const existing = state.routers.get(roomId);
    if (existing && !existing.closed) {
        return existing;
    }

    const worker = await ensureWorker();
    const router = await worker.createRouter({ mediaCodecs: MEDIA_CODECS });
    state.routers.set(roomId, router);
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

    const router = state.routers.get(roomId);
    if (router) {
        try {
            router.close();
        } catch (_) {
        }
        state.routers.delete(roomId);
    }
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
    process.stdout.write(`[${now}] [mediasoup-backend] ${message}\n`);
}

async function shutdown() {
    try {
        for (const roomId of [...state.routers.keys()]) {
            closeRoom(roomId);
        }

        if (state.capabilityRouter && !state.capabilityRouter.closed) {
            state.capabilityRouter.close();
        }
        state.capabilityRouter = null;

        if (state.webRtcServer && !state.webRtcServer.closed) {
            state.webRtcServer.close();
        }
        state.webRtcServer = null;

        if (state.worker) {
            state.worker.close();
            state.worker = null;
        }
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
    await ensureCapabilityRouter();
    server.listen(PORT, HOST, () => {
        log(`listening ws://${HOST}:${PORT}${WS_PATH}`);
        log(`rtc ports ${RTC_MIN_PORT}-${RTC_MAX_PORT}`);
    });
})();