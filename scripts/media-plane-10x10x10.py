#!/usr/bin/env python3
import argparse
import asyncio
import json
import math
import statistics
import time
import uuid
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional, Tuple

import websockets


def now_iso_utc() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def percentile(values: List[float], p: float) -> float:
    if not values:
        return 0.0
    if p <= 0:
        return min(values)
    if p >= 100:
        return max(values)
    ordered = sorted(values)
    idx = (len(ordered) - 1) * (p / 100.0)
    low = math.floor(idx)
    high = math.ceil(idx)
    if low == high:
        return ordered[low]
    frac = idx - low
    return ordered[low] * (1.0 - frac) + ordered[high] * frac


def parse_json_payload(raw: str) -> Dict[str, Any]:
    try:
        parsed = json.loads(raw)
    except Exception:
        return {}
    return parsed if isinstance(parsed, dict) else {}


def split_pairs(items: List[Any]) -> List[Tuple[Any, Any]]:
    pairs: List[Tuple[Any, Any]] = []
    for idx in range(0, len(items) - 1, 2):
        pairs.append((items[idx], items[idx + 1]))
    return pairs


def build_user_pool(total_needed: int, provided: List[str], run_id: str) -> List[str]:
    if provided:
        if len(provided) >= total_needed:
            return provided[:total_needed]
        result: List[str] = []
        while len(result) < total_needed:
            result.extend(provided)
        return result[:total_needed]
    return [
        str(uuid.uuid5(uuid.NAMESPACE_DNS, f"media-plane-{run_id}-{idx}"))
        for idx in range(total_needed)
    ]


def find_router_caps(response: Dict[str, Any]) -> Dict[str, Any]:
    data = response.get("data")
    if isinstance(data, dict):
        caps = data.get("routerRtpCapabilities")
        if isinstance(caps, dict):
            return caps
        backend = data.get("backend")
        if isinstance(backend, dict):
            caps = backend.get("routerRtpCapabilities")
            if isinstance(caps, dict):
                return caps
    backend = response.get("backend")
    if isinstance(backend, dict):
        caps = backend.get("routerRtpCapabilities")
        if isinstance(caps, dict):
            return caps
    return {}


def build_rtp_capabilities(router_caps: Dict[str, Any]) -> Dict[str, Any]:
    codecs = router_caps.get("codecs")
    header_extensions = router_caps.get("headerExtensions")
    if not isinstance(codecs, list):
        codecs = []
    if not isinstance(header_extensions, list):
        header_extensions = []
    return {
        "codecs": codecs,
        "headerExtensions": header_extensions,
    }


def build_rtp_parameters(kind: str, router_caps: Dict[str, Any], ssrc_seed: int) -> Dict[str, Any]:
    codecs = router_caps.get("codecs")
    if not isinstance(codecs, list):
        codecs = []

    codec_choice: Dict[str, Any] = {}
    target_prefix = f"{kind.lower()}/"
    for item in codecs:
        if not isinstance(item, dict):
            continue
        mime_type = str(item.get("mimeType", "")).lower()
        if mime_type.startswith(target_prefix):
            codec_choice = item
            break

    if not codec_choice:
        raise RuntimeError(f"No router codec for kind={kind}")

    payload_type = codec_choice.get("preferredPayloadType", codec_choice.get("payloadType"))
    if not isinstance(payload_type, int):
        raise RuntimeError(f"Codec has no payloadType for kind={kind}")

    clock_rate = codec_choice.get("clockRate")
    if not isinstance(clock_rate, int):
        raise RuntimeError(f"Codec has no clockRate for kind={kind}")

    codec_payload: Dict[str, Any] = {
        "mimeType": codec_choice.get("mimeType", f"{kind}/unknown"),
        "payloadType": payload_type,
        "clockRate": clock_rate,
        "parameters": codec_choice.get("parameters", {}) if isinstance(codec_choice.get("parameters"), dict) else {},
        "rtcpFeedback": codec_choice.get("rtcpFeedback", []) if isinstance(codec_choice.get("rtcpFeedback"), list) else [],
    }
    if kind.lower() == "audio":
        channels = codec_choice.get("channels")
        if isinstance(channels, int):
            codec_payload["channels"] = channels

    return {
        "codecs": [codec_payload],
        "headerExtensions": [],
        "encodings": [{"ssrc": ssrc_seed}],
        "rtcp": {
            "cname": f"cname-{ssrc_seed}",
            "reducedSize": True,
            "mux": True,
        },
    }


@dataclass
class Config:
    host: str
    port: int
    conference_users: int
    dm_users: int
    chat_users: int
    messages_per_chat_user: int
    action_timeout_s: float
    concurrency: int
    settle_s: float
    run_id: str
    user_pool: List[str]

    @property
    def total_users(self) -> int:
        return self.conference_users + self.dm_users + self.chat_users

    @property
    def ws_url(self) -> str:
        return f"ws://{self.host}:{self.port}"


@dataclass
class Metrics:
    op_total: int = 0
    op_ok: int = 0
    op_fail: int = 0
    by_op: Dict[str, Dict[str, Any]] = field(default_factory=dict)
    failures: List[Dict[str, str]] = field(default_factory=list)
    connect_ok: int = 0
    connect_fail: int = 0
    auth_ok: int = 0
    auth_fail: int = 0
    events: Counter = field(default_factory=Counter)
    stage_seconds: Dict[str, float] = field(default_factory=dict)

    def ensure_op(self, op_name: str) -> Dict[str, Any]:
        if op_name not in self.by_op:
            self.by_op[op_name] = {
                "total": 0,
                "ok": 0,
                "fail": 0,
                "latencies": [],
            }
        return self.by_op[op_name]

    def record(self, op_name: str, ok: bool, latency_ms: float, message: str = "", code: str = "") -> None:
        self.op_total += 1
        entry = self.ensure_op(op_name)
        entry["total"] += 1
        entry["latencies"].append(latency_ms)
        if ok:
            self.op_ok += 1
            entry["ok"] += 1
            return
        self.op_fail += 1
        entry["fail"] += 1
        if len(self.failures) < 120:
            self.failures.append(
                {
                    "op": op_name,
                    "code": code or "operation_failed",
                    "message": message[:400],
                }
            )

    def latency_report(self) -> Dict[str, Any]:
        report: Dict[str, Any] = {}
        for op_name, entry in self.by_op.items():
            latencies = entry["latencies"]
            report[op_name] = {
                "count": len(latencies),
                "p50": round(percentile(latencies, 50), 3),
                "p95": round(percentile(latencies, 95), 3),
                "p99": round(percentile(latencies, 99), 3),
                "max": round(max(latencies), 3) if latencies else 0.0,
                "avg": round(statistics.fmean(latencies), 3) if latencies else 0.0,
            }
        return report


class WsClient:
    def __init__(self, index: int, user_id: str, email: str) -> None:
        self.index = index
        self.user_id = user_id
        self.email = email
        self.ws: Optional[websockets.WebSocketClientProtocol] = None
        self.peer_id: str = ""
        self.reader_task: Optional[asyncio.Task] = None
        self.pending: Dict[str, asyncio.Future] = {}
        self.invite_queue: asyncio.Queue = asyncio.Queue()
        self.event_counts: Counter = Counter()
        self.last_error: str = ""

    async def connect(self, ws_url: str, timeout_s: float) -> None:
        self.ws = await asyncio.wait_for(
            websockets.connect(
                ws_url,
                ping_interval=20,
                ping_timeout=20,
                close_timeout=3,
                max_size=4 * 1024 * 1024,
            ),
            timeout=timeout_s,
        )
        first_raw = await asyncio.wait_for(self.ws.recv(), timeout=timeout_s)
        first_msg = parse_json_payload(first_raw)
        if first_msg.get("type") != "peer_assigned":
            raise RuntimeError(f"unexpected first message: {first_msg}")
        peer = first_msg.get("peer")
        if not isinstance(peer, str) or not peer:
            raise RuntimeError("peer assignment is missing")
        self.peer_id = peer
        self.reader_task = asyncio.create_task(self._reader())

    async def _reader(self) -> None:
        try:
            assert self.ws is not None
            async for raw in self.ws:
                msg = parse_json_payload(raw)
                msg_type = str(msg.get("type", ""))
                if msg_type == "dispatch_result":
                    corr = msg.get("clientRequestId") or msg.get("correlationId")
                    if isinstance(corr, str) and corr:
                        pending = self.pending.pop(corr, None)
                        if pending is not None and not pending.done():
                            pending.set_result((msg, time.perf_counter()))
                    continue
                if not msg_type:
                    continue
                self.event_counts[msg_type] += 1
                if msg_type == "direct_call_invite":
                    await self.invite_queue.put(msg)
        except Exception as ex:
            self.last_error = f"{type(ex).__name__}: {ex}"
        finally:
            for fut in self.pending.values():
                if not fut.done():
                    fut.set_exception(RuntimeError("websocket reader stopped"))
            self.pending.clear()

    async def send_action(
        self,
        object_type: str,
        agent_type: str,
        action_type: str,
        ctx: Dict[str, Any],
        timeout_s: float,
    ) -> Tuple[Dict[str, Any], float]:
        if self.ws is None:
            raise RuntimeError("websocket is not connected")
        if self.peer_id == "":
            raise RuntimeError("peer_id is not assigned")

        request_ctx = dict(ctx)
        corr = request_ctx.get("clientRequestId") or request_ctx.get("correlationId")
        if not isinstance(corr, str) or not corr:
            corr = f"{action_type}-{uuid.uuid4().hex[:14]}"
            request_ctx["clientRequestId"] = corr

        payload = {
            "object": object_type,
            "agent": agent_type,
            "action": action_type,
            "ctx": request_ctx,
        }

        fut = asyncio.get_running_loop().create_future()
        self.pending[corr] = fut
        started = time.perf_counter()
        await self.ws.send(json.dumps(payload, separators=(",", ":")))
        result, finished = await asyncio.wait_for(fut, timeout=timeout_s)
        return result, (finished - started) * 1000.0

    async def wait_invite(self, timeout_s: float) -> Dict[str, Any]:
        return await asyncio.wait_for(self.invite_queue.get(), timeout=timeout_s)

    async def close(self) -> None:
        if self.ws is not None:
            try:
                await self.ws.close()
            except Exception:
                pass
        if self.reader_task is not None:
            try:
                await asyncio.wait_for(self.reader_task, timeout=2.0)
            except Exception:
                self.reader_task.cancel()
                try:
                    await self.reader_task
                except Exception:
                    pass


async def run_limited(items: List[Any], concurrency: int, worker) -> None:
    sem = asyncio.Semaphore(max(1, concurrency))

    async def wrapped(item: Any) -> None:
        async with sem:
            await worker(item)

    await asyncio.gather(*(wrapped(item) for item in items))


async def call_action(
    client: WsClient,
    metrics: Metrics,
    op_name: str,
    object_type: str,
    agent_type: str,
    action_type: str,
    ctx: Dict[str, Any],
    timeout_s: float,
) -> Dict[str, Any]:
    started = time.perf_counter()
    try:
        response, latency_ms = await client.send_action(
            object_type=object_type,
            agent_type=agent_type,
            action_type=action_type,
            ctx=ctx,
            timeout_s=timeout_s,
        )
        ok = response.get("ok") is True
        metrics.record(
            op_name=op_name,
            ok=ok,
            latency_ms=latency_ms,
            message=str(response.get("message", "")),
            code=str(response.get("code", "")),
        )
        return response
    except Exception as ex:
        latency_ms = (time.perf_counter() - started) * 1000.0
        metrics.record(
            op_name=op_name,
            ok=False,
            latency_ms=latency_ms,
            message=f"{type(ex).__name__}: {ex}",
            code="transport_exception",
        )
        return {"ok": False, "message": f"{type(ex).__name__}: {ex}"}


async def connect_and_auth_all(clients: List[WsClient], cfg: Config, metrics: Metrics) -> List[WsClient]:
    async def worker(client: WsClient) -> None:
        try:
            await client.connect(cfg.ws_url, cfg.action_timeout_s)
            metrics.connect_ok += 1
        except Exception as ex:
            metrics.connect_fail += 1
            if len(metrics.failures) < 120:
                metrics.failures.append(
                    {"op": "connect", "code": "connect_fail", "message": f"u{client.index}: {type(ex).__name__}: {ex}"}
                )
            return

        bind_resp = await call_action(
            client=client,
            metrics=metrics,
            op_name="auth.bind_session",
            object_type="auth",
            agent_type="session",
            action_type="bind_session",
            ctx={
                "accessToken": f"dev:{client.user_id}|{client.email}",
                "deviceId": f"media-plane-dev-{client.index}",
            },
            timeout_s=cfg.action_timeout_s,
        )
        if bind_resp.get("ok") is True:
            metrics.auth_ok += 1
        else:
            metrics.auth_fail += 1

    await run_limited(clients, cfg.concurrency, worker)
    return [client for client in clients if client.ws is not None and client.peer_id]


async def run_conference_group(clients: List[WsClient], cfg: Config, metrics: Metrics) -> None:
    pairs = split_pairs(clients)
    async def worker(item: Tuple[int, Tuple[WsClient, WsClient]]) -> None:
        pair_index, pair = item
        client_a, client_b = pair
        conference_id = f"mp-conf-{cfg.run_id}-{pair_index}"
        transport_a = f"mp-conf-tr-a-{cfg.run_id}-{pair_index}"
        transport_b = f"mp-conf-tr-b-{cfg.run_id}-{pair_index}"

        await call_action(client_a, metrics, "conference.create", "conference", "lifecycle", "create_conference", {"conferenceId": conference_id}, cfg.action_timeout_s)
        await call_action(client_a, metrics, "conference.join", "conference", "membership", "join_conference", {"conferenceId": conference_id}, cfg.action_timeout_s)
        await call_action(client_b, metrics, "conference.join", "conference", "membership", "join_conference", {"conferenceId": conference_id}, cfg.action_timeout_s)

        open_a = await call_action(client_a, metrics, "conference.open_transport", "conference", "lifecycle", "open_transport", {"conferenceId": conference_id, "transportId": transport_a}, cfg.action_timeout_s)
        open_b = await call_action(client_b, metrics, "conference.open_transport", "conference", "lifecycle", "open_transport", {"conferenceId": conference_id, "transportId": transport_b}, cfg.action_timeout_s)

        caps_a = find_router_caps(open_a)
        caps_b = find_router_caps(open_b)
        caps = caps_a if caps_a else caps_b
        dtls_a = open_a.get("data", {}).get("dtlsParameters") if isinstance(open_a.get("data"), dict) else None
        dtls_b = open_b.get("data", {}).get("dtlsParameters") if isinstance(open_b.get("data"), dict) else None

        if isinstance(dtls_a, dict):
            await call_action(client_a, metrics, "conference.webrtc_offer", "conference", "lifecycle", "webrtc_offer", {"conferenceId": conference_id, "transportId": transport_a, "dtlsParameters": dtls_a}, cfg.action_timeout_s)
        if isinstance(dtls_b, dict):
            await call_action(client_b, metrics, "conference.webrtc_offer", "conference", "lifecycle", "webrtc_offer", {"conferenceId": conference_id, "transportId": transport_b, "dtlsParameters": dtls_b}, cfg.action_timeout_s)

        await call_action(client_a, metrics, "conference.webrtc_ice", "conference", "lifecycle", "webrtc_ice", {"conferenceId": conference_id, "transportId": transport_a, "candidate": "candidate:1 1 udp 2122260223 127.0.0.1 50000 typ host"}, cfg.action_timeout_s)
        await call_action(client_b, metrics, "conference.webrtc_ice", "conference", "lifecycle", "webrtc_ice", {"conferenceId": conference_id, "transportId": transport_b, "candidate": "candidate:1 1 udp 2122260223 127.0.0.1 50001 typ host"}, cfg.action_timeout_s)

        if caps:
            audio_params_a = build_rtp_parameters("audio", caps, 110000 + pair_index * 10 + 1)
            video_params_a = build_rtp_parameters("video", caps, 120000 + pair_index * 10 + 1)
            audio_params_b = build_rtp_parameters("audio", caps, 210000 + pair_index * 10 + 1)
            video_params_b = build_rtp_parameters("video", caps, 220000 + pair_index * 10 + 1)
            rtp_caps = build_rtp_capabilities(caps)

            producer_a_audio = f"mp-conf-pa-audio-{pair_index}"
            producer_a_video = f"mp-conf-pa-video-{pair_index}"
            producer_b_audio = f"mp-conf-pb-audio-{pair_index}"
            producer_b_video = f"mp-conf-pb-video-{pair_index}"

            await call_action(client_a, metrics, "conference.publish_track", "conference", "lifecycle", "publish_track", {"conferenceId": conference_id, "transportId": transport_a, "producerId": producer_a_audio, "kind": "audio", "trackType": "microphone", "rtpParameters": audio_params_a}, cfg.action_timeout_s)
            await call_action(client_a, metrics, "conference.publish_track", "conference", "lifecycle", "publish_track", {"conferenceId": conference_id, "transportId": transport_a, "producerId": producer_a_video, "kind": "video", "trackType": "camera", "rtpParameters": video_params_a}, cfg.action_timeout_s)
            await call_action(client_b, metrics, "conference.publish_track", "conference", "lifecycle", "publish_track", {"conferenceId": conference_id, "transportId": transport_b, "producerId": producer_b_audio, "kind": "audio", "trackType": "microphone", "rtpParameters": audio_params_b}, cfg.action_timeout_s)
            await call_action(client_b, metrics, "conference.publish_track", "conference", "lifecycle", "publish_track", {"conferenceId": conference_id, "transportId": transport_b, "producerId": producer_b_video, "kind": "video", "trackType": "camera", "rtpParameters": video_params_b}, cfg.action_timeout_s)

            consume_matrix = [
                (client_a, transport_a, producer_b_audio, f"mp-conf-ca-audio-{pair_index}"),
                (client_a, transport_a, producer_b_video, f"mp-conf-ca-video-{pair_index}"),
                (client_b, transport_b, producer_a_audio, f"mp-conf-cb-audio-{pair_index}"),
                (client_b, transport_b, producer_a_video, f"mp-conf-cb-video-{pair_index}"),
            ]
            for consumer_client, consumer_transport, producer_id, consumer_id in consume_matrix:
                consume_resp = await call_action(
                    consumer_client,
                    metrics,
                    "conference.consume_track",
                    "conference",
                    "lifecycle",
                    "consume_track",
                    {
                        "conferenceId": conference_id,
                        "transportId": consumer_transport,
                        "producerId": producer_id,
                        "consumerId": consumer_id,
                        "rtpCapabilities": rtp_caps,
                    },
                    cfg.action_timeout_s,
                )
                resolved_consumer_id = consumer_id
                if isinstance(consume_resp.get("data"), dict):
                    resolved_consumer_id = str(consume_resp["data"].get("consumerId", consumer_id))
                await call_action(
                    consumer_client,
                    metrics,
                    "conference.consumer_ready",
                    "conference",
                    "lifecycle",
                    "consumer_ready",
                    {"conferenceId": conference_id, "consumerId": resolved_consumer_id},
                    cfg.action_timeout_s,
                )

            await call_action(client_a, metrics, "conference.media_stats", "conference", "lifecycle", "media_stats", {"conferenceId": conference_id}, cfg.action_timeout_s)
            await call_action(client_b, metrics, "conference.media_stats", "conference", "lifecycle", "media_stats", {"conferenceId": conference_id}, cfg.action_timeout_s)

        await call_action(client_a, metrics, "conference.leave", "conference", "membership", "leave_conference", {"conferenceId": conference_id}, cfg.action_timeout_s)
        await call_action(client_b, metrics, "conference.leave", "conference", "membership", "leave_conference", {"conferenceId": conference_id}, cfg.action_timeout_s)
        await call_action(client_a, metrics, "conference.close", "conference", "lifecycle", "close_conference", {"conferenceId": conference_id}, cfg.action_timeout_s)

    await run_limited(list(enumerate(pairs)), max(1, cfg.concurrency // 2), worker)


async def run_direct_call_group(clients: List[WsClient], cfg: Config, metrics: Metrics) -> None:
    pairs = split_pairs(clients)

    async def worker(item: Tuple[int, Tuple[WsClient, WsClient]]) -> None:
        pair_index, pair = item
        caller, callee = pair

        create_resp = await call_action(
            caller,
            metrics,
            "direct_call.create_call",
            "direct_call",
            "lifecycle",
            "create_call",
            {"targetUserId": callee.user_id, "clientRequestId": f"mp-call-create-{cfg.run_id}-{pair_index}"},
            cfg.action_timeout_s,
        )
        if create_resp.get("ok") is not True:
            return

        call_id = ""
        try:
            invite = await callee.wait_invite(cfg.action_timeout_s)
            call_id = str(invite.get("callId", ""))
        except Exception as ex:
            metrics.record("direct_call.invite", False, 0.0, f"{type(ex).__name__}: {ex}", "invite_timeout")
            return
        metrics.record("direct_call.invite", bool(call_id), 0.0, "missing callId" if not call_id else "", "invite_missing_call_id" if not call_id else "")
        if not call_id:
            return

        await call_action(callee, metrics, "direct_call.accept_call", "direct_call", "lifecycle", "accept_call", {"callId": call_id}, cfg.action_timeout_s)

        transport_a = f"mp-call-tr-a-{cfg.run_id}-{pair_index}"
        transport_b = f"mp-call-tr-b-{cfg.run_id}-{pair_index}"
        open_a = await call_action(caller, metrics, "direct_call.open_transport", "direct_call", "lifecycle", "open_transport", {"callId": call_id, "transportId": transport_a}, cfg.action_timeout_s)
        open_b = await call_action(callee, metrics, "direct_call.open_transport", "direct_call", "lifecycle", "open_transport", {"callId": call_id, "transportId": transport_b}, cfg.action_timeout_s)

        caps_a = find_router_caps(open_a)
        caps_b = find_router_caps(open_b)
        caps = caps_a if caps_a else caps_b
        dtls_a = open_a.get("data", {}).get("dtlsParameters") if isinstance(open_a.get("data"), dict) else None
        dtls_b = open_b.get("data", {}).get("dtlsParameters") if isinstance(open_b.get("data"), dict) else None

        if isinstance(dtls_a, dict):
            await call_action(caller, metrics, "direct_call.webrtc_offer", "direct_call", "lifecycle", "webrtc_offer", {"callId": call_id, "transportId": transport_a, "dtlsParameters": dtls_a}, cfg.action_timeout_s)
        if isinstance(dtls_b, dict):
            await call_action(callee, metrics, "direct_call.webrtc_offer", "direct_call", "lifecycle", "webrtc_offer", {"callId": call_id, "transportId": transport_b, "dtlsParameters": dtls_b}, cfg.action_timeout_s)

        await call_action(caller, metrics, "direct_call.webrtc_ice", "direct_call", "lifecycle", "webrtc_ice", {"callId": call_id, "transportId": transport_a, "candidate": "candidate:1 1 udp 2122260223 127.0.0.1 51000 typ host"}, cfg.action_timeout_s)
        await call_action(callee, metrics, "direct_call.webrtc_ice", "direct_call", "lifecycle", "webrtc_ice", {"callId": call_id, "transportId": transport_b, "candidate": "candidate:1 1 udp 2122260223 127.0.0.1 51001 typ host"}, cfg.action_timeout_s)

        if caps:
            audio_params_a = build_rtp_parameters("audio", caps, 310000 + pair_index * 10 + 1)
            video_params_a = build_rtp_parameters("video", caps, 320000 + pair_index * 10 + 1)
            audio_params_b = build_rtp_parameters("audio", caps, 410000 + pair_index * 10 + 1)
            video_params_b = build_rtp_parameters("video", caps, 420000 + pair_index * 10 + 1)
            rtp_caps = build_rtp_capabilities(caps)

            producer_a_audio = f"mp-call-pa-audio-{pair_index}"
            producer_a_video = f"mp-call-pa-video-{pair_index}"
            producer_b_audio = f"mp-call-pb-audio-{pair_index}"
            producer_b_video = f"mp-call-pb-video-{pair_index}"

            await call_action(caller, metrics, "direct_call.publish_track", "direct_call", "lifecycle", "publish_track", {"callId": call_id, "transportId": transport_a, "producerId": producer_a_audio, "kind": "audio", "trackType": "microphone", "rtpParameters": audio_params_a}, cfg.action_timeout_s)
            await call_action(caller, metrics, "direct_call.publish_track", "direct_call", "lifecycle", "publish_track", {"callId": call_id, "transportId": transport_a, "producerId": producer_a_video, "kind": "video", "trackType": "camera", "rtpParameters": video_params_a}, cfg.action_timeout_s)
            await call_action(callee, metrics, "direct_call.publish_track", "direct_call", "lifecycle", "publish_track", {"callId": call_id, "transportId": transport_b, "producerId": producer_b_audio, "kind": "audio", "trackType": "microphone", "rtpParameters": audio_params_b}, cfg.action_timeout_s)
            await call_action(callee, metrics, "direct_call.publish_track", "direct_call", "lifecycle", "publish_track", {"callId": call_id, "transportId": transport_b, "producerId": producer_b_video, "kind": "video", "trackType": "camera", "rtpParameters": video_params_b}, cfg.action_timeout_s)

            consume_matrix = [
                (caller, transport_a, producer_b_audio, f"mp-call-ca-audio-{pair_index}"),
                (caller, transport_a, producer_b_video, f"mp-call-ca-video-{pair_index}"),
                (callee, transport_b, producer_a_audio, f"mp-call-cb-audio-{pair_index}"),
                (callee, transport_b, producer_a_video, f"mp-call-cb-video-{pair_index}"),
            ]
            for consumer_client, consumer_transport, producer_id, consumer_id in consume_matrix:
                consume_resp = await call_action(
                    consumer_client,
                    metrics,
                    "direct_call.consume_track",
                    "direct_call",
                    "lifecycle",
                    "consume_track",
                    {
                        "callId": call_id,
                        "transportId": consumer_transport,
                        "producerId": producer_id,
                        "consumerId": consumer_id,
                        "rtpCapabilities": rtp_caps,
                    },
                    cfg.action_timeout_s,
                )
                resolved_consumer_id = consumer_id
                if isinstance(consume_resp.get("data"), dict):
                    resolved_consumer_id = str(consume_resp["data"].get("consumerId", consumer_id))
                await call_action(
                    consumer_client,
                    metrics,
                    "direct_call.consumer_ready",
                    "direct_call",
                    "lifecycle",
                    "consumer_ready",
                    {"callId": call_id, "consumerId": resolved_consumer_id},
                    cfg.action_timeout_s,
                )

            await call_action(caller, metrics, "direct_call.media_stats", "direct_call", "lifecycle", "media_stats", {"callId": call_id}, cfg.action_timeout_s)
            await call_action(callee, metrics, "direct_call.media_stats", "direct_call", "lifecycle", "media_stats", {"callId": call_id}, cfg.action_timeout_s)

        await call_action(caller, metrics, "direct_call.hangup_call", "direct_call", "lifecycle", "hangup_call", {"callId": call_id}, cfg.action_timeout_s)

    await run_limited(list(enumerate(pairs)), max(1, cfg.concurrency // 2), worker)


async def run_direct_chat_group(clients: List[WsClient], cfg: Config, metrics: Metrics) -> None:
    pairs = split_pairs(clients)
    items: List[Tuple[WsClient, WsClient, int]] = []
    for left, right in pairs:
        for i in range(cfg.messages_per_chat_user):
            items.append((left, right, i))
            items.append((right, left, i))

    async def worker(item: Tuple[WsClient, WsClient, int]) -> None:
        sender, receiver, idx = item
        await call_action(
            sender,
            metrics,
            "direct_chat.send_message",
            "direct_chat",
            "messaging",
            "send_message",
            {
                "targetUserId": receiver.user_id,
                "text": f"mp-chat-{cfg.run_id}-u{sender.index}-m{idx}",
            },
            cfg.action_timeout_s,
        )

    await run_limited(items, cfg.concurrency, worker)


async def run_suite(cfg: Config) -> Dict[str, Any]:
    metrics = Metrics()
    started_total = time.perf_counter()

    users = build_user_pool(cfg.total_users, cfg.user_pool, cfg.run_id)
    clients = [WsClient(idx, users[idx], f"mp-{cfg.run_id}-{idx}@example.com") for idx in range(cfg.total_users)]

    started = time.perf_counter()
    alive_clients = await connect_and_auth_all(clients, cfg, metrics)
    metrics.stage_seconds["connect_auth"] = round(time.perf_counter() - started, 3)

    if len(alive_clients) < 6:
        for client in alive_clients:
            await client.close()
        return {
            "timestampUtc": now_iso_utc(),
            "pass": False,
            "reason": "too_few_alive_clients_after_auth",
            "config": cfg.__dict__,
            "metrics": {
                "connectOk": metrics.connect_ok,
                "connectFail": metrics.connect_fail,
                "authOk": metrics.auth_ok,
                "authFail": metrics.auth_fail,
                "failureSample": metrics.failures[:50],
            },
        }

    conf_slice = alive_clients[: min(cfg.conference_users, len(alive_clients))]
    dm_start = len(conf_slice)
    dm_slice = alive_clients[dm_start: dm_start + min(cfg.dm_users, max(0, len(alive_clients) - dm_start))]
    chat_start = dm_start + len(dm_slice)
    chat_slice = alive_clients[chat_start: chat_start + min(cfg.chat_users, max(0, len(alive_clients) - chat_start))]

    started = time.perf_counter()
    await run_conference_group(conf_slice, cfg, metrics)
    metrics.stage_seconds["conference_group"] = round(time.perf_counter() - started, 3)

    started = time.perf_counter()
    await run_direct_call_group(dm_slice, cfg, metrics)
    metrics.stage_seconds["direct_call_group"] = round(time.perf_counter() - started, 3)

    started = time.perf_counter()
    await run_direct_chat_group(chat_slice, cfg, metrics)
    metrics.stage_seconds["direct_chat_group"] = round(time.perf_counter() - started, 3)

    if cfg.settle_s > 0:
        started = time.perf_counter()
        await asyncio.sleep(cfg.settle_s)
        metrics.stage_seconds["settle"] = round(time.perf_counter() - started, 3)

    for client in alive_clients:
        metrics.events.update(client.event_counts)
        if client.last_error and len(metrics.failures) < 120:
            metrics.failures.append({"op": "ws.reader", "code": "reader_error", "message": f"u{client.index}: {client.last_error}"})

    for client in clients:
        await client.close()

    metrics.stage_seconds["total"] = round(time.perf_counter() - started_total, 3)

    op_failure_rate = metrics.op_fail / max(1, metrics.op_total)
    critical_ops = [
        "conference.open_transport",
        "direct_call.open_transport",
        "direct_call.create_call",
        "direct_call.accept_call",
        "direct_chat.send_message",
    ]
    critical_success: Dict[str, float] = {}
    for op in critical_ops:
        entry = metrics.by_op.get(op, {"ok": 0, "total": 0})
        critical_success[op] = round(entry["ok"] / max(1, entry["total"]), 4)

    pass_ok = (
        metrics.connect_fail == 0
        and metrics.auth_fail == 0
        and op_failure_rate <= 0.05
        and all(value >= 0.9 for value in critical_success.values())
    )

    summary = {
        "timestampUtc": now_iso_utc(),
        "pass": pass_ok,
        "config": cfg.__dict__,
        "clientGroups": {
            "aliveTotal": len(alive_clients),
            "conferenceClients": len(conf_slice),
            "dmClients": len(dm_slice),
            "chatClients": len(chat_slice),
            "conferencePairs": len(split_pairs(conf_slice)),
            "dmPairs": len(split_pairs(dm_slice)),
            "chatPairs": len(split_pairs(chat_slice)),
        },
        "metrics": {
            "connectOk": metrics.connect_ok,
            "connectFail": metrics.connect_fail,
            "authOk": metrics.auth_ok,
            "authFail": metrics.auth_fail,
            "operationsTotal": metrics.op_total,
            "operationsOk": metrics.op_ok,
            "operationsFail": metrics.op_fail,
            "operationFailureRate": round(op_failure_rate, 4),
            "criticalSuccessRate": critical_success,
            "events": dict(metrics.events),
            "latencyMs": metrics.latency_report(),
            "stageSeconds": metrics.stage_seconds,
            "failureSample": metrics.failures[:120],
        },
    }
    return summary


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="10/10/10 media-plane integration test: conference + DM calls + direct chat.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9002)
    parser.add_argument("--conference-users", type=int, default=10)
    parser.add_argument("--dm-users", type=int, default=10)
    parser.add_argument("--chat-users", type=int, default=10)
    parser.add_argument("--messages-per-chat-user", type=int, default=3)
    parser.add_argument("--action-timeout-s", type=float, default=18.0)
    parser.add_argument("--concurrency", type=int, default=24)
    parser.add_argument("--settle-s", type=float, default=1.5)
    parser.add_argument("--run-id", default=uuid.uuid4().hex[:10])
    parser.add_argument(
        "--user-pool",
        nargs="*",
        default=[],
        help="Optional list of user UUIDs; if fewer than total users they will be reused cyclically.",
    )
    return parser.parse_args()


async def amain() -> int:
    args = parse_args()
    cfg = Config(
        host=args.host,
        port=max(1, args.port),
        conference_users=max(0, args.conference_users),
        dm_users=max(0, args.dm_users),
        chat_users=max(0, args.chat_users),
        messages_per_chat_user=max(0, args.messages_per_chat_user),
        action_timeout_s=max(1.0, args.action_timeout_s),
        concurrency=max(1, args.concurrency),
        settle_s=max(0.0, args.settle_s),
        run_id=args.run_id,
        user_pool=list(args.user_pool),
    )
    summary = await run_suite(cfg)
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0 if summary.get("pass") else 2


def main() -> int:
    try:
        return asyncio.run(amain())
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
