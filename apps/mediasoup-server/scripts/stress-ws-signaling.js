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

async function runWithConcurrency(items, concurrency, worker) {
    let cursor = 0;
    const workers = [];
    const safeConcurrency = Math.max(1, Math.min(items.length || 1, concurrency));
    for (let i = 0; i < safeConcurrency; i += 1) {
        workers.push((async () => {
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
    await Promise.all(workers);
}

function formatMs(value) {
    if (!Number.isFinite(value)) {
        return null;
    }
    return Number(value.toFixed(2));
}

function shuffleInPlace(values) {
    for (let i = values.length - 1; i > 0; i -= 1) {
        const j = Math.floor(Math.random() * (i + 1));
        const tmp = values[i];
        values[i] = values[j];
        values[j] = tmp;
    }
    return values;
}

class SignalingClient {
    constructor(peerId, url, requestTimeoutMs, connectTimeoutMs) {
        this.peerId = peerId;
        this.url = url;
        this.requestTimeoutMs = requestTimeoutMs;
        this.connectTimeoutMs = connectTimeoutMs;
        this.socket = null;
        this.sequence = 0;
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
        const requestId = `${this.peerId}-${this.sequence}`;
        const startedAt = performance.now();

        const response = await new Promise((resolve, reject) => {
            const timeoutHandle = setTimeout(() => {
                this.pending.delete(requestId);
                reject(new Error(`request timeout after ${this.requestTimeoutMs}ms`));
            }, this.requestTimeoutMs);
            timeoutHandle.unref();

            this.pending.set(requestId, {
                timeoutHandle,
                resolve,
                reject
            });

            this.socket.send(JSON.stringify({
                id: requestId,
                operation,
                payload: payload || {}
            }), (error) => {
                if (!error) {
                    return;
                }
                const pendingEntry = this.pending.get(requestId);
                if (!pendingEntry) {
                    return;
                }
                this.pending.delete(requestId);
                clearTimeout(pendingEntry.timeoutHandle);
                pendingEntry.reject(error);
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
        url: process.env.MEDIASOUP_STRESS_URL || 'ws://127.0.0.1:5001/ws',
        roomId: process.env.MEDIASOUP_STRESS_ROOM_ID || 'stress-room-500',
        clients: readIntEnv('MEDIASOUP_STRESS_CLIENTS', 500, 1, 5000),
        concurrency: readIntEnv('MEDIASOUP_STRESS_CONCURRENCY', 50, 1, 1000),
        churnRounds: readIntEnv('MEDIASOUP_STRESS_CHURN_ROUNDS', 2, 0, 50),
        churnPercent: readIntEnv('MEDIASOUP_STRESS_CHURN_PERCENT', 10, 0, 100),
        requestTimeoutMs: readIntEnv('MEDIASOUP_STRESS_REQUEST_TIMEOUT_MS', 8000, 500, 120000),
        connectTimeoutMs: readIntEnv('MEDIASOUP_STRESS_CONNECT_TIMEOUT_MS', 8000, 500, 120000),
        maxFailureRate: readFloatEnv('MEDIASOUP_STRESS_MAX_FAILURE_RATE', 0.02, 0, 1)
    };

    const metrics = {
        connectionsAttempted: config.clients,
        connectionsSucceeded: 0,
        connectionsFailed: 0,
        operationsTotal: 0,
        operationsSucceeded: 0,
        operationsFailed: 0,
        operationsByName: {},
        failuresByCode: {},
        latenciesByName: {},
        sampleFailures: []
    };

    const clients = [];
    const startedAt = performance.now();

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
        if (metrics.sampleFailures.length < 15) {
            metrics.sampleFailures.push({
                operation,
                code,
                message: failureMessage || null
            });
        }
    };

    const runOperation = async (client, operation, payload) => {
        const requestStartedAt = performance.now();
        try {
            const { response, latencyMs } = await client.request(operation, payload);
            const ok = Boolean(response?.ok);
            recordOperation(
                operation,
                ok,
                latencyMs,
                response?.code,
                ok ? null : response?.message
            );
            return ok;
        } catch (error) {
            const latencyMs = performance.now() - requestStartedAt;
            recordOperation(
                operation,
                false,
                latencyMs,
                'request_error',
                error?.message || String(error)
            );
            return false;
        }
    };

    console.log('[stress] connecting clients...');
    await runWithConcurrency(
        Array.from({ length: config.clients }, (_, index) => index),
        config.concurrency,
        async (index) => {
            const peerId = `peer-${index + 1}`;
            const client = new SignalingClient(
                peerId,
                config.url,
                config.requestTimeoutMs,
                config.connectTimeoutMs
            );
            try {
                await client.connect();
                metrics.connectionsSucceeded += 1;
                clients.push(client);
            } catch (error) {
                metrics.connectionsFailed += 1;
                if (metrics.sampleFailures.length < 15) {
                    metrics.sampleFailures.push({
                        operation: 'connect',
                        code: 'connect_error',
                        message: error?.message || String(error)
                    });
                }
            }
        }
    );

    if (clients.length === 0) {
        throw new Error('All clients failed to connect.');
    }

    console.log(`[stress] connected ${clients.length}/${config.clients} clients`);
    console.log('[stress] join phase...');
    await runWithConcurrency(clients, config.concurrency, async (client) => {
        await runOperation(client, 'router.joinPeer', {
            roomId: config.roomId,
            peerId: client.peerId
        });
    });

    if (config.churnRounds > 0 && config.churnPercent > 0) {
        const churnCount = Math.max(1, Math.floor(clients.length * (config.churnPercent / 100)));
        for (let round = 1; round <= config.churnRounds; round += 1) {
            const selected = shuffleInPlace([...clients]).slice(0, churnCount);
            console.log(`[stress] churn round ${round}/${config.churnRounds} (${selected.length} peers)...`);
            await runWithConcurrency(selected, config.concurrency, async (client) => {
                await runOperation(client, 'router.leavePeer', {
                    roomId: config.roomId,
                    peerId: client.peerId
                });
                await runOperation(client, 'router.joinPeer', {
                    roomId: config.roomId,
                    peerId: client.peerId
                });
            });
        }
    }

    console.log('[stress] liveness check...');
    await runOperation(clients[0], 'system.getCapabilities', {});

    console.log('[stress] teardown phase...');
    await runWithConcurrency(clients, config.concurrency, async (client) => {
        await runOperation(client, 'router.leavePeer', {
            roomId: config.roomId,
            peerId: client.peerId
        });
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

    const failureRate = metrics.operationsTotal > 0
        ? metrics.operationsFailed / metrics.operationsTotal
        : 1;
    const connectionFailureRate = metrics.connectionsAttempted > 0
        ? metrics.connectionsFailed / metrics.connectionsAttempted
        : 1;

    const summary = {
        config,
        durationMs: formatMs(durationMs),
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
            failureRate: Number(failureRate.toFixed(4)),
            byOperation: metrics.operationsByName,
            failuresByCode: metrics.failuresByCode,
            latency: latenciesSummary
        },
        sampleFailures: metrics.sampleFailures
    };

    console.log('[stress] summary');
    console.log(JSON.stringify(summary, null, 2));

    const isPassing = connectionFailureRate <= config.maxFailureRate
        && failureRate <= config.maxFailureRate;
    process.exitCode = isPassing ? 0 : 2;
}

main().catch((error) => {
    console.error('[stress] fatal', error?.stack || error?.message || String(error));
    process.exitCode = 1;
});
