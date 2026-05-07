'use strict';

const { WebSocket } = require('ws');
const { performance } = require('node:perf_hooks');

function readIntEnv(name, fallbackValue, minValue, maxValue) {
    const parsed = Number.parseInt(process.env[name] || '', 10);
    if (!Number.isFinite(parsed)) {
        return Math.max(minValue, Math.min(maxValue, Math.trunc(fallbackValue)));
    }
    return Math.max(minValue, Math.min(maxValue, Math.trunc(parsed)));
}

function readFloatEnv(name, fallbackValue, minValue, maxValue) {
    const parsed = Number.parseFloat(process.env[name] || '');
    if (!Number.isFinite(parsed)) {
        return Math.max(minValue, Math.min(maxValue, fallbackValue));
    }
    return Math.max(minValue, Math.min(maxValue, parsed));
}

function quantile(values, percentile) {
    if (!Array.isArray(values) || values.length === 0) {
        return null;
    }
    const sorted = [...values].sort((a, b) => a - b);
    const rank = (percentile / 100) * (sorted.length - 1);
    const lower = Math.floor(rank);
    const upper = Math.ceil(rank);
    if (lower === upper) {
        return sorted[lower];
    }
    const fraction = rank - lower;
    return sorted[lower] + (sorted[upper] - sorted[lower]) * fraction;
}

function formatMs(value) {
    if (!Number.isFinite(value)) {
        return null;
    }
    return Number(value.toFixed(2));
}

async function runWithConcurrency(items, concurrency, worker) {
    const safeConcurrency = Math.max(1, Math.min(items.length || 1, concurrency));
    let cursor = 0;
    const runners = [];
    for (let i = 0; i < safeConcurrency; i += 1) {
        runners.push((async () => {
            while (true) {
                const currentIndex = cursor;
                cursor += 1;
                if (currentIndex >= items.length) {
                    return;
                }
                await worker(items[currentIndex], currentIndex);
            }
        })());
    }
    await Promise.all(runners);
}

function findCodec(routerCaps, kind) {
    const codecs = Array.isArray(routerCaps?.codecs) ? routerCaps.codecs : [];
    const prefix = `${String(kind || '').toLowerCase()}/`;
    for (const codec of codecs) {
        const mimeType = String(codec?.mimeType || '').toLowerCase();
        if (mimeType.startsWith(prefix)) {
            return codec;
        }
    }
    return null;
}

function buildRtpParameters(routerCaps, kind, ssrcSeed) {
    const codec = findCodec(routerCaps, kind);
    if (!codec) {
        throw new Error(`No codec found for kind=${kind}`);
    }
    const payloadType = Number.isFinite(codec.preferredPayloadType)
        ? codec.preferredPayloadType
        : codec.payloadType;
    if (!Number.isFinite(payloadType)) {
        throw new Error(`Codec for kind=${kind} has no payloadType`);
    }

    const codecPayload = {
        mimeType: codec.mimeType,
        payloadType,
        clockRate: codec.clockRate,
        parameters: typeof codec.parameters === 'object' && codec.parameters !== null ? codec.parameters : {},
        rtcpFeedback: Array.isArray(codec.rtcpFeedback) ? codec.rtcpFeedback : []
    };
    if (kind === 'audio' && Number.isFinite(codec.channels)) {
        codecPayload.channels = codec.channels;
    }

    return {
        codecs: [codecPayload],
        headerExtensions: [],
        encodings: [{ ssrc: ssrcSeed }],
        rtcp: {
            cname: `stress-${ssrcSeed}`,
            reducedSize: true,
            mux: true
        }
    };
}

function buildRtpCapabilities(routerCaps) {
    return {
        codecs: Array.isArray(routerCaps?.codecs) ? routerCaps.codecs : [],
        headerExtensions: Array.isArray(routerCaps?.headerExtensions) ? routerCaps.headerExtensions : []
    };
}

class BackendClient {
    constructor(peerLabel, url, requestTimeoutMs, connectTimeoutMs) {
        this.peerLabel = peerLabel;
        this.url = url;
        this.requestTimeoutMs = requestTimeoutMs;
        this.connectTimeoutMs = connectTimeoutMs;
        this.sequence = 0;
        this.socket = null;
        this.pending = new Map();
    }

    async connect() {
        await new Promise((resolve, reject) => {
            const socket = new WebSocket(this.url, {
                handshakeTimeout: this.connectTimeoutMs
            });
            this.socket = socket;

            let settled = false;
            const connectTimer = setTimeout(() => {
                if (settled) {
                    return;
                }
                settled = true;
                try {
                    socket.terminate();
                } catch (_) {
                }
                reject(new Error(`connect timeout after ${this.connectTimeoutMs}ms`));
            }, this.connectTimeoutMs);
            connectTimer.unref();

            socket.on('open', () => {
                if (settled) {
                    return;
                }
                settled = true;
                clearTimeout(connectTimer);
                resolve();
            });

            socket.on('message', (rawBuffer) => {
                this.handleMessage(rawBuffer);
            });

            socket.on('close', () => {
                this.rejectAllPending(new Error('socket closed'));
            });

            socket.on('error', (error) => {
                if (!settled) {
                    settled = true;
                    clearTimeout(connectTimer);
                    reject(error);
                    return;
                }
                this.rejectAllPending(error);
            });
        });
    }

    handleMessage(rawBuffer) {
        let message = null;
        try {
            const text = Buffer.isBuffer(rawBuffer)
                ? rawBuffer.toString('utf8')
                : String(rawBuffer ?? '');
            message = JSON.parse(text);
        } catch (_) {
            return;
        }
        const requestId = message?.id;
        if (typeof requestId !== 'string') {
            return;
        }
        const entry = this.pending.get(requestId);
        if (!entry) {
            return;
        }
        this.pending.delete(requestId);
        clearTimeout(entry.timeoutHandle);
        entry.resolve(message);
    }

    rejectAllPending(error) {
        for (const [requestId, entry] of this.pending.entries()) {
            this.pending.delete(requestId);
            clearTimeout(entry.timeoutHandle);
            entry.reject(error);
        }
    }

    async request(operation, payload) {
        if (!this.socket || this.socket.readyState !== WebSocket.OPEN) {
            throw new Error('socket is not open');
        }

        this.sequence += 1;
        const requestId = `${this.peerLabel}-${this.sequence}`;
        const startedAt = performance.now();

        const response = await new Promise((resolve, reject) => {
            const timeoutHandle = setTimeout(() => {
                this.pending.delete(requestId);
                reject(new Error(`request timeout after ${this.requestTimeoutMs}ms`));
            }, this.requestTimeoutMs);
            timeoutHandle.unref();

            this.pending.set(requestId, { resolve, reject, timeoutHandle });
            this.socket.send(JSON.stringify({
                id: requestId,
                operation,
                payload: payload || {}
            }), (error) => {
                if (!error) {
                    return;
                }
                const entry = this.pending.get(requestId);
                if (!entry) {
                    return;
                }
                this.pending.delete(requestId);
                clearTimeout(entry.timeoutHandle);
                entry.reject(error);
            });
        });

        const latencyMs = performance.now() - startedAt;
        return { response, latencyMs };
    }

    close() {
        this.rejectAllPending(new Error('socket closing'));
        if (!this.socket) {
            return;
        }
        try {
            if (this.socket.readyState === WebSocket.OPEN || this.socket.readyState === WebSocket.CONNECTING) {
                this.socket.close();
            }
        } catch (_) {
        }
    }
}

async function main() {
    const config = {
        url: process.env.MEDIASOUP_MEDIA_STRESS_URL || 'ws://127.0.0.1:5001/ws',
        conferenceUsers: readIntEnv('MEDIASOUP_MEDIA_STRESS_CONFERENCE_USERS', 10, 0, 200),
        dmUsers: readIntEnv('MEDIASOUP_MEDIA_STRESS_DM_USERS', 10, 0, 200),
        pairConcurrency: readIntEnv('MEDIASOUP_MEDIA_STRESS_PAIR_CONCURRENCY', 4, 1, 100),
        connectConcurrency: readIntEnv('MEDIASOUP_MEDIA_STRESS_CONNECT_CONCURRENCY', 20, 1, 200),
        requestTimeoutMs: readIntEnv('MEDIASOUP_MEDIA_STRESS_REQUEST_TIMEOUT_MS', 10000, 500, 120000),
        connectTimeoutMs: readIntEnv('MEDIASOUP_MEDIA_STRESS_CONNECT_TIMEOUT_MS', 8000, 500, 120000),
        maxFailureRate: readFloatEnv('MEDIASOUP_MEDIA_STRESS_MAX_FAILURE_RATE', 0.05, 0, 1)
    };
    config.conferenceUsers -= config.conferenceUsers % 2;
    config.dmUsers -= config.dmUsers % 2;
    const totalUsers = config.conferenceUsers + config.dmUsers;

    const metrics = {
        connectionsAttempted: totalUsers,
        connectionsSucceeded: 0,
        connectionsFailed: 0,
        operationsTotal: 0,
        operationsSucceeded: 0,
        operationsFailed: 0,
        operationsByName: {},
        failuresByCode: {},
        latenciesByName: {},
        pairSuccess: 0,
        pairFailed: 0,
        sampleFailures: []
    };

    const startedAt = performance.now();
    const clients = [];

    const recordOperation = (operation, ok, latencyMs, failureCode, failureMessage) => {
        metrics.operationsTotal += 1;
        if (!metrics.operationsByName[operation]) {
            metrics.operationsByName[operation] = { total: 0, ok: 0, failed: 0 };
        }
        if (!metrics.latenciesByName[operation]) {
            metrics.latenciesByName[operation] = [];
        }
        const opEntry = metrics.operationsByName[operation];
        opEntry.total += 1;
        metrics.latenciesByName[operation].push(latencyMs);

        if (ok) {
            metrics.operationsSucceeded += 1;
            opEntry.ok += 1;
            return;
        }

        metrics.operationsFailed += 1;
        opEntry.failed += 1;
        const code = typeof failureCode === 'string' && failureCode.length > 0
            ? failureCode
            : 'request_error';
        metrics.failuresByCode[code] = (metrics.failuresByCode[code] || 0) + 1;
        if (metrics.sampleFailures.length < 30) {
            metrics.sampleFailures.push({
                operation,
                code,
                message: failureMessage || null
            });
        }
    };

    const callOp = async (client, operation, payload) => {
        const started = performance.now();
        try {
            const { response, latencyMs } = await client.request(operation, payload);
            const ok = Boolean(response?.ok);
            recordOperation(operation, ok, latencyMs, response?.code, ok ? null : response?.message);
            return { ok, response };
        } catch (error) {
            recordOperation(
                operation,
                false,
                performance.now() - started,
                'request_error',
                error?.message || String(error)
            );
            return { ok: false, response: null };
        }
    };

    const peerDescriptors = [];
    for (let i = 0; i < config.conferenceUsers; i += 1) {
        peerDescriptors.push({ group: 'conference', index: i, peerId: `conf-peer-${i + 1}` });
    }
    for (let i = 0; i < config.dmUsers; i += 1) {
        peerDescriptors.push({ group: 'dm', index: i, peerId: `dm-peer-${i + 1}` });
    }

    console.log(`[media-stress] connecting ${totalUsers} peers...`);
    await runWithConcurrency(peerDescriptors, config.connectConcurrency, async (peer) => {
        const client = new BackendClient(
            `${peer.group}-${peer.index + 1}`,
            config.url,
            config.requestTimeoutMs,
            config.connectTimeoutMs
        );
        try {
            await client.connect();
            metrics.connectionsSucceeded += 1;
            peer.client = client;
            clients.push(client);
        } catch (error) {
            metrics.connectionsFailed += 1;
            if (metrics.sampleFailures.length < 30) {
                metrics.sampleFailures.push({
                    operation: 'connect',
                    code: 'connect_error',
                    message: `[${peer.group}/${peer.peerId}] ${error?.message || String(error)}`
                });
            }
        }
    });

    if (clients.length === 0) {
        throw new Error('All mediasoup peers failed to connect.');
    }

    const bootstrapClient = peerDescriptors.find((item) => item.client)?.client;
    if (!bootstrapClient) {
        throw new Error('No bootstrap client for capabilities.');
    }

    const capsResult = await callOp(bootstrapClient, 'system.getCapabilities', {});
    if (!capsResult.ok || !capsResult.response) {
        throw new Error('Failed to read mediasoup capabilities.');
    }
    const routerCaps = capsResult.response?.backend?.routerRtpCapabilities
        || capsResult.response?.data?.routerRtpCapabilities
        || null;
    if (!routerCaps || typeof routerCaps !== 'object') {
        throw new Error('routerRtpCapabilities are missing.');
    }
    const consumeCaps = buildRtpCapabilities(routerCaps);

    const pairTasks = [];
    const conferencePeers = peerDescriptors.filter((item) => item.group === 'conference' && item.client);
    const dmPeers = peerDescriptors.filter((item) => item.group === 'dm' && item.client);

    for (let i = 0; i + 1 < conferencePeers.length; i += 2) {
        const roomId = `media-conf-room-${Math.floor(i / 2) + 1}`;
        pairTasks.push({ roomType: 'conference', roomId, left: conferencePeers[i], right: conferencePeers[i + 1] });
    }
    for (let i = 0; i + 1 < dmPeers.length; i += 2) {
        const roomId = `media-dm-room-${Math.floor(i / 2) + 1}`;
        pairTasks.push({ roomType: 'dm', roomId, left: dmPeers[i], right: dmPeers[i + 1] });
    }

    console.log(`[media-stress] running ${pairTasks.length} room pairs (conference+dm media only)...`);
    await runWithConcurrency(pairTasks, config.pairConcurrency, async (pair, pairIndex) => {
        const left = pair.left;
        const right = pair.right;
        const leftClient = left.client;
        const rightClient = right.client;
        if (!leftClient || !rightClient) {
            metrics.pairFailed += 1;
            return;
        }

        let pairOk = true;
        const leftTransportId = `${pair.roomType}-tr-${pairIndex + 1}-a`;
        const rightTransportId = `${pair.roomType}-tr-${pairIndex + 1}-b`;
        const leftAudioProducerId = `${pair.roomType}-prod-audio-${pairIndex + 1}-a`;
        const leftVideoProducerId = `${pair.roomType}-prod-video-${pairIndex + 1}-a`;
        const rightAudioProducerId = `${pair.roomType}-prod-audio-${pairIndex + 1}-b`;
        const rightVideoProducerId = `${pair.roomType}-prod-video-${pairIndex + 1}-b`;

        const markFail = () => {
            pairOk = false;
        };

        const safeCall = async (client, operation, payload) => {
            const result = await callOp(client, operation, payload);
            if (!result.ok) {
                markFail();
            }
            return result;
        };

        try {
            await Promise.all([
                safeCall(leftClient, 'router.joinPeer', { roomId: pair.roomId, peerId: left.peerId }),
                safeCall(rightClient, 'router.joinPeer', { roomId: pair.roomId, peerId: right.peerId })
            ]);

            const [leftOpen, rightOpen] = await Promise.all([
                safeCall(leftClient, 'router.createWebRtcTransport', { roomId: pair.roomId, peerId: left.peerId, transportId: leftTransportId }),
                safeCall(rightClient, 'router.createWebRtcTransport', { roomId: pair.roomId, peerId: right.peerId, transportId: rightTransportId })
            ]);

            const leftDtls = leftOpen.response?.data?.dtlsParameters;
            const rightDtls = rightOpen.response?.data?.dtlsParameters;
            if (!leftDtls || !rightDtls) {
                markFail();
            } else {
                await Promise.all([
                    safeCall(leftClient, 'transport.connectDtls', { transportId: leftTransportId, dtlsParameters: leftDtls }),
                    safeCall(rightClient, 'transport.connectDtls', { transportId: rightTransportId, dtlsParameters: rightDtls })
                ]);
            }

            const leftAudioParams = buildRtpParameters(routerCaps, 'audio', 100000 + pairIndex * 20 + 1);
            const leftVideoParams = buildRtpParameters(routerCaps, 'video', 100000 + pairIndex * 20 + 2);
            const rightAudioParams = buildRtpParameters(routerCaps, 'audio', 200000 + pairIndex * 20 + 1);
            const rightVideoParams = buildRtpParameters(routerCaps, 'video', 200000 + pairIndex * 20 + 2);

            await Promise.all([
                safeCall(leftClient, 'transport.produce', {
                    transportId: leftTransportId,
                    producerId: leftAudioProducerId,
                    kind: 'audio',
                    trackType: 'microphone',
                    rtpParameters: leftAudioParams
                }),
                safeCall(leftClient, 'transport.produce', {
                    transportId: leftTransportId,
                    producerId: leftVideoProducerId,
                    kind: 'video',
                    trackType: 'camera',
                    rtpParameters: leftVideoParams
                }),
                safeCall(rightClient, 'transport.produce', {
                    transportId: rightTransportId,
                    producerId: rightAudioProducerId,
                    kind: 'audio',
                    trackType: 'microphone',
                    rtpParameters: rightAudioParams
                }),
                safeCall(rightClient, 'transport.produce', {
                    transportId: rightTransportId,
                    producerId: rightVideoProducerId,
                    kind: 'video',
                    trackType: 'camera',
                    rtpParameters: rightVideoParams
                })
            ]);

            const consumeCalls = [
                { client: leftClient, transportId: leftTransportId, producerId: rightAudioProducerId, consumerId: `${pair.roomType}-cons-audio-${pairIndex + 1}-a` },
                { client: leftClient, transportId: leftTransportId, producerId: rightVideoProducerId, consumerId: `${pair.roomType}-cons-video-${pairIndex + 1}-a` },
                { client: rightClient, transportId: rightTransportId, producerId: leftAudioProducerId, consumerId: `${pair.roomType}-cons-audio-${pairIndex + 1}-b` },
                { client: rightClient, transportId: rightTransportId, producerId: leftVideoProducerId, consumerId: `${pair.roomType}-cons-video-${pairIndex + 1}-b` }
            ];

            for (const consumeCall of consumeCalls) {
                const consumeResult = await safeCall(consumeCall.client, 'transport.consume', {
                    transportId: consumeCall.transportId,
                    producerId: consumeCall.producerId,
                    consumerId: consumeCall.consumerId,
                    rtpCapabilities: consumeCaps
                });
                const resolvedConsumerId = consumeResult.response?.data?.consumerId || consumeCall.consumerId;
                await safeCall(consumeCall.client, 'consumer.resume', {
                    consumerId: resolvedConsumerId
                });
            }

            await safeCall(leftClient, 'system.getMediaStats', { roomId: pair.roomId });
        } finally {
            await Promise.all([
                callOp(leftClient, 'producer.close', { producerId: leftAudioProducerId, peerId: left.peerId }).catch(() => null),
                callOp(leftClient, 'producer.close', { producerId: leftVideoProducerId, peerId: left.peerId }).catch(() => null),
                callOp(rightClient, 'producer.close', { producerId: rightAudioProducerId, peerId: right.peerId }).catch(() => null),
                callOp(rightClient, 'producer.close', { producerId: rightVideoProducerId, peerId: right.peerId }).catch(() => null),
                callOp(leftClient, 'router.leavePeer', { roomId: pair.roomId, peerId: left.peerId }).catch(() => null),
                callOp(rightClient, 'router.leavePeer', { roomId: pair.roomId, peerId: right.peerId }).catch(() => null)
            ]);
        }

        if (pairOk) {
            metrics.pairSuccess += 1;
        } else {
            metrics.pairFailed += 1;
        }
    });

    for (const client of clients) {
        client.close();
    }

    const durationMs = performance.now() - startedAt;
    const latenciesSummary = {};
    for (const [operation, latencies] of Object.entries(metrics.latenciesByName)) {
        latenciesSummary[operation] = {
            count: latencies.length,
            p50Ms: formatMs(quantile(latencies, 50)),
            p95Ms: formatMs(quantile(latencies, 95)),
            p99Ms: formatMs(quantile(latencies, 99)),
            maxMs: formatMs(Math.max(...latencies))
        };
    }

    const opFailureRate = metrics.operationsTotal > 0
        ? metrics.operationsFailed / metrics.operationsTotal
        : 1;
    const connectionFailureRate = metrics.connectionsAttempted > 0
        ? metrics.connectionsFailed / metrics.connectionsAttempted
        : 1;
    const pairFailureRate = pairTasks.length > 0
        ? metrics.pairFailed / pairTasks.length
        : 1;

    const summary = {
        mode: 'mediasoup_only_media_signaling',
        config,
        durationMs: formatMs(durationMs),
        rooms: {
            totalPairs: pairTasks.length,
            pairSuccess: metrics.pairSuccess,
            pairFailed: metrics.pairFailed,
            pairFailureRate: Number(pairFailureRate.toFixed(4))
        },
        connections: {
            attempted: metrics.connectionsAttempted,
            succeeded: metrics.connectionsSucceeded,
            failed: metrics.connectionsFailed,
            failureRate: Number(connectionFailureRate.toFixed(4))
        },
        operations: {
            total: metrics.operationsTotal,
            succeeded: metrics.operationsSucceeded,
            failed: metrics.operationsFailed,
            failureRate: Number(opFailureRate.toFixed(4)),
            byOperation: metrics.operationsByName,
            failuresByCode: metrics.failuresByCode,
            latency: latenciesSummary
        },
        sampleFailures: metrics.sampleFailures
    };

    console.log('[media-stress] summary');
    console.log(JSON.stringify(summary, null, 2));

    const isPassing = connectionFailureRate <= config.maxFailureRate
        && opFailureRate <= config.maxFailureRate
        && pairFailureRate <= config.maxFailureRate;
    process.exitCode = isPassing ? 0 : 2;
}

main().catch((error) => {
    console.error('[media-stress] fatal', error?.stack || error?.message || String(error));
    process.exitCode = 1;
});
