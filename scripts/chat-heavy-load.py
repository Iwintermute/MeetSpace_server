#!/usr/bin/env python3
import argparse
import asyncio
import json
import math
import statistics
import time
import uuid
from collections import Counter
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional, Tuple

import websockets


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


def now_iso_utc() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


@dataclass
class Counters:
    connect_ok: int = 0
    connect_fail: int = 0
    auth_ok: int = 0
    auth_fail: int = 0
    conference_create_ok: int = 0
    conference_create_fail: int = 0
    conference_join_ok: int = 0
    conference_join_fail: int = 0
    conference_transport_ok: int = 0
    conference_transport_fail: int = 0
    chat_send_ok: int = 0
    chat_send_fail: int = 0
    call_create_ok: int = 0
    call_create_fail: int = 0
    call_invite_ok: int = 0
    call_invite_fail: int = 0
    call_accept_ok: int = 0
    call_accept_fail: int = 0
    call_transport_ok: int = 0
    call_transport_fail: int = 0
    call_hangup_ok: int = 0
    call_hangup_fail: int = 0
    ws_disconnects: int = 0
    chat_messages_received: int = 0


@dataclass
class StageDurations:
    connect_auth_s: float = 0.0
    conference_setup_s: float = 0.0
    conference_media_setup_s: float = 0.0
    chat_burst_s: float = 0.0
    direct_call_s: float = 0.0
    settle_s: float = 0.0
    total_s: float = 0.0


@dataclass
class Config:
    host: str
    port: int
    clients: int
    conferences: int
    messages_per_client: int
    call_pairs: int
    concurrency: int
    action_timeout_s: float
    settle_after_chat_s: float
    enable_conference_transport: bool
    enable_call_transport: bool
    enable_hangup: bool
    user_pool: List[str]
    run_id: str


@dataclass
class ClientState:
    index: int
    user_id: str
    email: str
    ws: Optional[websockets.WebSocketClientProtocol] = None
    peer_id: str = ""
    recv_task: Optional[asyncio.Task] = None
    pending: Dict[str, asyncio.Future] = field(default_factory=dict)
    invite_queue: asyncio.Queue = field(default_factory=asyncio.Queue)
    latencies_ms: List[float] = field(default_factory=list)
    errors: List[str] = field(default_factory=list)
    disconnects: int = 0
    chat_messages_received: int = 0

    async def connect(self, uri: str, timeout_s: float) -> None:
        self.ws = await asyncio.wait_for(
            websockets.connect(
                uri,
                ping_interval=20,
                ping_timeout=20,
                close_timeout=3,
                max_size=4 * 1024 * 1024,
            ),
            timeout=timeout_s,
        )
        assigned_raw = await asyncio.wait_for(self.ws.recv(), timeout=timeout_s)
        assigned = json.loads(assigned_raw)
        if assigned.get("type") != "peer_assigned" or not assigned.get("peer"):
            raise RuntimeError(f"unexpected first message: {assigned}")
        self.peer_id = assigned["peer"]
        self.recv_task = asyncio.create_task(self._reader())

    async def _reader(self) -> None:
        try:
            assert self.ws is not None
            async for raw in self.ws:
                try:
                    msg = json.loads(raw)
                except Exception:
                    continue
                msg_type = msg.get("type")
                if msg_type == "dispatch_result":
                    corr = msg.get("clientRequestId") or msg.get("correlationId")
                    if corr:
                        fut = self.pending.pop(corr, None)
                        if fut is not None and not fut.done():
                            fut.set_result((msg, time.perf_counter()))
                elif msg_type == "direct_call_invite":
                    await self.invite_queue.put(msg)
                elif msg_type == "chat_message":
                    self.chat_messages_received += 1
        except Exception as ex:
            self.errors.append(f"reader:{type(ex).__name__}:{ex}")
        finally:
            self.disconnects += 1
            for fut in self.pending.values():
                if not fut.done():
                    fut.set_exception(RuntimeError("connection closed"))
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
            raise RuntimeError("websocket not connected")
        request_ctx = dict(ctx)
        corr = request_ctx.get("clientRequestId") or request_ctx.get("correlationId")
        if not corr:
            corr = f"{action_type}-{uuid.uuid4().hex[:12]}"
            request_ctx["clientRequestId"] = corr
        payload = {
            "object": object_type,
            "agent": agent_type,
            "action": action_type,
            "ctx": request_ctx,
        }
        fut: asyncio.Future = asyncio.get_running_loop().create_future()
        self.pending[corr] = fut
        started = time.perf_counter()
        await self.ws.send(json.dumps(payload, separators=(",", ":")))
        msg, ended = await asyncio.wait_for(fut, timeout=timeout_s)
        latency_ms = (ended - started) * 1000.0
        self.latencies_ms.append(latency_ms)
        return msg, latency_ms

    async def wait_invite(self, timeout_s: float) -> Dict[str, Any]:
        msg = await asyncio.wait_for(self.invite_queue.get(), timeout=timeout_s)
        return msg

    async def close(self) -> None:
        if self.ws is not None:
            try:
                await self.ws.close()
            except Exception:
                pass
        if self.recv_task is not None:
            try:
                await asyncio.wait_for(self.recv_task, timeout=2.0)
            except Exception:
                self.recv_task.cancel()
                try:
                    await self.recv_task
                except Exception:
                    pass


async def run_limited(items: List[Any], limit: int, worker):
    semaphore = asyncio.Semaphore(max(1, limit))
    results: List[Any] = [None] * len(items)

    async def wrapped(i: int, item: Any):
        async with semaphore:
            results[i] = await worker(item)

    await asyncio.gather(*(wrapped(i, it) for i, it in enumerate(items)))
    return results


def stable_uuid_for_index(prefix: str, index: int) -> str:
    return str(uuid.uuid5(uuid.NAMESPACE_DNS, f"{prefix}-{index}"))


async def run_load(config: Config) -> Dict[str, Any]:
    counters = Counters()
    stage = StageDurations()
    failures: List[str] = []
    uri = f"ws://{config.host}:{config.port}"

    start_total = time.perf_counter()
    clients: List[ClientState] = []
    if not config.user_pool:
        raise ValueError("user_pool must contain at least one UUID")
    for i in range(config.clients):
        user_id = config.user_pool[i % len(config.user_pool)]
        email = f"load-{i}@example.com"
        clients.append(ClientState(index=i, user_id=user_id, email=email))

    async def connect_and_auth(client: ClientState) -> None:
        try:
            await client.connect(uri, timeout_s=config.action_timeout_s)
            counters.connect_ok += 1
        except Exception as ex:
            counters.connect_fail += 1
            failures.append(f"connect[{client.index}]={type(ex).__name__}:{ex}")
            return
        try:
            msg, _ = await client.send_action(
                "auth",
                "session",
                "bind_session",
                {
                    "accessToken": f"dev:{client.user_id}|{client.email}",
                    "deviceId": f"load-dev-{client.index}",
                },
                timeout_s=config.action_timeout_s,
            )
            if msg.get("ok") is True:
                counters.auth_ok += 1
            else:
                counters.auth_fail += 1
                failures.append(
                    f"auth[{client.index}]={msg.get('message','unknown')}"
                )
        except Exception as ex:
            counters.auth_fail += 1
            failures.append(f"auth[{client.index}]={type(ex).__name__}:{ex}")

    stage_start = time.perf_counter()
    await run_limited(clients, config.concurrency, connect_and_auth)
    stage.connect_auth_s = time.perf_counter() - stage_start

    alive_clients = [c for c in clients if c.ws is not None and c.peer_id]
    if not alive_clients:
        stage.total_s = time.perf_counter() - start_total
        return {
            "timestampUtc": now_iso_utc(),
            "config": config.__dict__,
            "counters": counters.__dict__,
            "stageDurationsSec": stage.__dict__,
            "latencyMs": {},
            "failureSample": failures[:50],
            "pass": False,
            "note": "no alive websocket clients after connect/auth",
        }

    conf_count = max(1, min(config.conferences, len(alive_clients)))
    conf_ids = [f"conf-load-{config.run_id}-{i}" for i in range(conf_count)]
    conf_buckets: List[List[ClientState]] = [[] for _ in range(conf_count)]
    for i, client in enumerate(alive_clients):
        conf_buckets[i % conf_count].append(client)

    stage_start = time.perf_counter()
    for conf_index, members in enumerate(conf_buckets):
        owner = members[0]
        conf_id = conf_ids[conf_index]
        try:
            msg, _ = await owner.send_action(
                "conference",
                "lifecycle",
                "create_conference",
                {"conferenceId": conf_id},
                timeout_s=config.action_timeout_s,
            )
            if msg.get("ok") is True:
                counters.conference_create_ok += 1
            else:
                counters.conference_create_fail += 1
                failures.append(f"conf.create[{conf_id}]={msg.get('message','unknown')}")
        except Exception as ex:
            counters.conference_create_fail += 1
            failures.append(f"conf.create[{conf_id}]={type(ex).__name__}:{ex}")

    async def join_one(item: Tuple[ClientState, str]) -> None:
        client, conf_id = item
        try:
            msg, _ = await client.send_action(
                "conference",
                "lifecycle",
                "join_conference",
                {"conferenceId": conf_id},
                timeout_s=config.action_timeout_s,
            )
            if msg.get("ok") is True:
                counters.conference_join_ok += 1
            else:
                counters.conference_join_fail += 1
                failures.append(
                    f"conf.join[{client.index}:{conf_id}]={msg.get('message','unknown')}"
                )
        except Exception as ex:
            counters.conference_join_fail += 1
            failures.append(f"conf.join[{client.index}:{conf_id}]={type(ex).__name__}:{ex}")

    join_items: List[Tuple[ClientState, str]] = []
    for conf_index, members in enumerate(conf_buckets):
        conf_id = conf_ids[conf_index]
        for client in members:
            join_items.append((client, conf_id))
    await run_limited(join_items, config.concurrency, join_one)
    stage.conference_setup_s = time.perf_counter() - stage_start

    if config.enable_conference_transport:
        stage_start = time.perf_counter()

        async def open_transport_one(item: Tuple[ClientState, str]) -> None:
            client, conf_id = item
            try:
                msg, _ = await client.send_action(
                    "conference",
                    "lifecycle",
                    "open_transport",
                    {
                        "conferenceId": conf_id,
                        "transportId": f"tr-conf-{config.run_id}-{client.index}",
                    },
                    timeout_s=config.action_timeout_s,
                )
                if msg.get("ok") is True:
                    counters.conference_transport_ok += 1
                else:
                    counters.conference_transport_fail += 1
                    failures.append(
                        f"conf.transport[{client.index}:{conf_id}]={msg.get('message','unknown')}"
                    )
            except Exception as ex:
                counters.conference_transport_fail += 1
                failures.append(
                    f"conf.transport[{client.index}:{conf_id}]={type(ex).__name__}:{ex}"
                )

        transport_items: List[Tuple[ClientState, str]] = []
        for conf_index, members in enumerate(conf_buckets):
            conf_id = conf_ids[conf_index]
            for client in members:
                transport_items.append((client, conf_id))
        await run_limited(transport_items, config.concurrency, open_transport_one)
        stage.conference_media_setup_s = time.perf_counter() - stage_start

    stage_start = time.perf_counter()
    chat_items: List[Tuple[ClientState, str, int]] = []
    for conf_index, members in enumerate(conf_buckets):
        conf_id = conf_ids[conf_index]
        for client in members:
            for m in range(config.messages_per_client):
                chat_items.append((client, conf_id, m))

    async def send_chat_one(item: Tuple[ClientState, str, int]) -> None:
        client, conf_id, m = item
        text = f"LOAD::{config.run_id}::u{client.index}::m{m}"
        try:
            msg, _ = await client.send_action(
                "chat",
                "messaging",
                "send_message",
                {
                    "conferenceId": conf_id,
                    "text": text,
                },
                timeout_s=config.action_timeout_s,
            )
            if msg.get("ok") is True:
                counters.chat_send_ok += 1
            else:
                counters.chat_send_fail += 1
                failures.append(
                    f"chat.send[{client.index}:{conf_id}]={msg.get('message','unknown')}"
                )
        except Exception as ex:
            counters.chat_send_fail += 1
            failures.append(f"chat.send[{client.index}:{conf_id}]={type(ex).__name__}:{ex}")

    await run_limited(chat_items, config.concurrency, send_chat_one)
    stage.chat_burst_s = time.perf_counter() - stage_start

    if config.settle_after_chat_s > 0:
        stage_start = time.perf_counter()
        await asyncio.sleep(config.settle_after_chat_s)
        stage.settle_s = time.perf_counter() - stage_start

    stage_start = time.perf_counter()
    max_pairs = min(config.call_pairs, len(alive_clients) // 2)
    call_pairs: List[Tuple[ClientState, ClientState]] = []
    for i in range(max_pairs):
        a = alive_clients[i * 2]
        b = alive_clients[i * 2 + 1]
        call_pairs.append((a, b))

    async def run_pair(pair: Tuple[ClientState, ClientState]) -> None:
        caller, callee = pair
        call_id = ""
        try:
            create_msg, _ = await caller.send_action(
                "direct_call",
                "lifecycle",
                "create_call",
                {"targetUserId": callee.user_id},
                timeout_s=config.action_timeout_s,
            )
            if create_msg.get("ok") is True:
                counters.call_create_ok += 1
            else:
                counters.call_create_fail += 1
                failures.append(
                    f"call.create[{caller.index}->{callee.index}]={create_msg.get('message','unknown')}"
                )
                return
        except Exception as ex:
            counters.call_create_fail += 1
            failures.append(
                f"call.create[{caller.index}->{callee.index}]={type(ex).__name__}:{ex}"
            )
            return

        try:
            invite = await callee.wait_invite(timeout_s=config.action_timeout_s)
            call_id = str(invite.get("callId") or "")
            if call_id:
                counters.call_invite_ok += 1
            else:
                counters.call_invite_fail += 1
                failures.append(
                    f"call.invite[{caller.index}->{callee.index}]=missing_call_id"
                )
                return
        except Exception as ex:
            counters.call_invite_fail += 1
            failures.append(
                f"call.invite[{caller.index}->{callee.index}]={type(ex).__name__}:{ex}"
            )
            return

        try:
            accept_msg, _ = await callee.send_action(
                "direct_call",
                "lifecycle",
                "accept_call",
                {"callId": call_id},
                timeout_s=config.action_timeout_s,
            )
            if accept_msg.get("ok") is True:
                counters.call_accept_ok += 1
            else:
                counters.call_accept_fail += 1
                failures.append(
                    f"call.accept[{caller.index}->{callee.index}]={accept_msg.get('message','unknown')}"
                )
                return
        except Exception as ex:
            counters.call_accept_fail += 1
            failures.append(
                f"call.accept[{caller.index}->{callee.index}]={type(ex).__name__}:{ex}"
            )
            return

        if config.enable_call_transport:
            try:
                open_a, open_b = await asyncio.gather(
                    caller.send_action(
                        "direct_call",
                        "lifecycle",
                        "open_transport",
                        {"callId": call_id, "transportId": f"tr-call-a-{config.run_id}-{caller.index}"},
                        timeout_s=config.action_timeout_s,
                    ),
                    callee.send_action(
                        "direct_call",
                        "lifecycle",
                        "open_transport",
                        {"callId": call_id, "transportId": f"tr-call-b-{config.run_id}-{callee.index}"},
                        timeout_s=config.action_timeout_s,
                    ),
                )
                ok_a = open_a[0].get("ok") is True
                ok_b = open_b[0].get("ok") is True
                if ok_a and ok_b:
                    counters.call_transport_ok += 2
                else:
                    counters.call_transport_fail += 2 - int(ok_a) - int(ok_b)
                    if not ok_a:
                        failures.append(
                            f"call.transport.a[{caller.index}->{callee.index}]={open_a[0].get('message','unknown')}"
                        )
                    if not ok_b:
                        failures.append(
                            f"call.transport.b[{caller.index}->{callee.index}]={open_b[0].get('message','unknown')}"
                        )
            except Exception as ex:
                counters.call_transport_fail += 2
                failures.append(
                    f"call.transport[{caller.index}->{callee.index}]={type(ex).__name__}:{ex}"
                )

        if config.enable_hangup:
            try:
                hangup_msg, _ = await caller.send_action(
                    "direct_call",
                    "lifecycle",
                    "hangup_call",
                    {"callId": call_id},
                    timeout_s=config.action_timeout_s,
                )
                if hangup_msg.get("ok") is True:
                    counters.call_hangup_ok += 1
                else:
                    counters.call_hangup_fail += 1
                    failures.append(
                        f"call.hangup[{caller.index}->{callee.index}]={hangup_msg.get('message','unknown')}"
                    )
            except Exception as ex:
                counters.call_hangup_fail += 1
                failures.append(
                    f"call.hangup[{caller.index}->{callee.index}]={type(ex).__name__}:{ex}"
                )

    await run_limited(call_pairs, max(8, config.concurrency // 2), run_pair)
    stage.direct_call_s = time.perf_counter() - stage_start

    for client in clients:
        counters.chat_messages_received += client.chat_messages_received
        counters.ws_disconnects += client.disconnects
        if client.errors:
            failures.extend([f"client[{client.index}]::{e}" for e in client.errors[:3]])

    all_latencies = [lat for client in clients for lat in client.latencies_ms]
    latency_stats = {
        "count": len(all_latencies),
        "avg": round(statistics.fmean(all_latencies), 3) if all_latencies else 0.0,
        "p50": round(percentile(all_latencies, 50), 3),
        "p95": round(percentile(all_latencies, 95), 3),
        "p99": round(percentile(all_latencies, 99), 3),
        "max": round(max(all_latencies), 3) if all_latencies else 0.0,
    }

    stage.total_s = time.perf_counter() - start_total

    expected_chat = len(chat_items)
    connect_ratio = counters.connect_ok / max(1, config.clients)
    auth_ratio = counters.auth_ok / max(1, counters.connect_ok)
    conf_join_ratio = counters.conference_join_ok / max(1, len(join_items))
    chat_ratio = counters.chat_send_ok / max(1, expected_chat)
    call_ratio = counters.call_accept_ok / max(1, max_pairs)
    pass_ok = (
        connect_ratio >= 0.98
        and auth_ratio >= 0.98
        and conf_join_ratio >= 0.98
        and chat_ratio >= 0.97
        and (max_pairs == 0 or call_ratio >= 0.95)
    )

    disconnect_samples = Counter()
    for client in clients:
        if client.disconnects > 0:
            disconnect_samples["disconnects"] += client.disconnects

    for client in clients:
        await client.close()

    return {
        "timestampUtc": now_iso_utc(),
        "config": config.__dict__,
        "counters": counters.__dict__,
        "ratios": {
            "connect": round(connect_ratio, 4),
            "auth": round(auth_ratio, 4),
            "conferenceJoin": round(conf_join_ratio, 4),
            "chatSend": round(chat_ratio, 4),
            "callAccept": round(call_ratio, 4) if max_pairs > 0 else None,
        },
        "latencyMs": latency_stats,
        "disconnectSummary": dict(disconnect_samples),
        "stageDurationsSec": stage.__dict__,
        "failureSample": failures[:120],
        "pass": pass_ok,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="High-concurrency websocket load test for chat/direct-call signaling."
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9002)
    parser.add_argument("--clients", type=int, default=520)
    parser.add_argument("--conferences", type=int, default=10)
    parser.add_argument("--messages-per-client", type=int, default=1)
    parser.add_argument("--call-pairs", type=int, default=120)
    parser.add_argument("--concurrency", type=int, default=120)
    parser.add_argument("--action-timeout-s", type=float, default=18.0)
    parser.add_argument("--settle-after-chat-s", type=float, default=2.0)
    parser.add_argument("--no-conference-transport", action="store_true")
    parser.add_argument("--no-call-transport", action="store_true")
    parser.add_argument("--no-hangup", action="store_true")
    parser.add_argument(
        "--user-pool",
        nargs="+",
        default=[
            "c4487900-a777-45ba-a813-5ddd453d9c4d",
            "f9a25664-c27d-475c-9a74-105975f77161",
        ],
        help="List of existing user UUIDs used cyclically by load clients.",
    )
    parser.add_argument("--run-id", default=uuid.uuid4().hex[:10])
    return parser.parse_args()


async def amain() -> int:
    args = parse_args()
    cfg = Config(
        host=args.host,
        port=args.port,
        clients=max(1, args.clients),
        conferences=max(1, args.conferences),
        messages_per_client=max(0, args.messages_per_client),
        call_pairs=max(0, args.call_pairs),
        concurrency=max(1, args.concurrency),
        action_timeout_s=max(1.0, args.action_timeout_s),
        settle_after_chat_s=max(0.0, args.settle_after_chat_s),
        enable_conference_transport=not args.no_conference_transport,
        enable_call_transport=not args.no_call_transport,
        enable_hangup=not args.no_hangup,
        user_pool=list(args.user_pool),
        run_id=args.run_id,
    )
    summary = await run_load(cfg)
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0 if summary.get("pass") else 2


def main() -> int:
    try:
        return asyncio.run(amain())
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
