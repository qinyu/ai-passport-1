#!/usr/bin/env python3
"""Local BLE bridge from Codex hooks to a Passport Codex Buddy.

Hooks and this process stay on the computer. The Passport only receives the
compact usage packet and permission prompts over Nordic UART, the same shape
the Stick firmware accepts. An app-server TUI proxy is intentionally not the
companion path.
"""

from __future__ import annotations

import argparse
import asyncio
import contextlib
import json
import socket
import sqlite3
import sys
import time
from pathlib import Path
from typing import Any

from codex_bridge_core import (
    STATES,
    ActivityTracker,
    UsageSnapshot,
    choose_state,
    clear_prompt_packet,
    hook_behavior,
    normalize_device_decision,
    prompt_from_hook,
    prompt_packet,
    reduce_rollout_lines,
)


NUS_SERVICE = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
NUS_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
STATE_DIR = Path.home() / ".codex" / "codex-passport-bridge"


def latest_rollout_paths(codex_home: Path, limit: int) -> list[Path]:
    database = codex_home / "state_5.sqlite"
    if not database.exists():
        return []
    connection = sqlite3.connect(f"file:{database}?mode=ro", uri=True)
    try:
        rows = connection.execute(
            """
            select rollout_path
            from threads
            where rollout_path is not null and rollout_path != ''
            order by coalesce(updated_at_ms, updated_at * 1000) desc
            limit ?
            """,
            (limit,),
        ).fetchall()
    finally:
        connection.close()
    paths: list[Path] = []
    for (raw,) in rows:
        path = Path(str(raw)).expanduser()
        if path.exists():
            paths.append(path)
    return paths


def tail_lines(path: Path, max_bytes: int) -> list[str]:
    size = path.stat().st_size
    with path.open("rb") as handle:
        if size > max_bytes:
            handle.seek(size - max_bytes)
            handle.readline()
        data = handle.read()
    return data.decode("utf-8", errors="replace").splitlines()


def read_usage(codex_home: Path, thread_limit: int, tail_bytes: int) -> UsageSnapshot | None:
    best: UsageSnapshot | None = None
    for path in latest_rollout_paths(codex_home, thread_limit):
        snapshot = reduce_rollout_lines(tail_lines(path, tail_bytes))
        if snapshot is None:
            continue
        if best is None or (snapshot.event_ts or 0) >= (best.event_ts or 0):
            best = snapshot
    return best


def manual_snapshot(args: argparse.Namespace) -> UsageSnapshot:
    now = int(time.time())
    return UsageSnapshot(
        tokens=args.tokens,
        primary=args.primary,
        secondary=args.secondary,
        primary_resets_at=args.primary_resets_at or now + 5 * 60 * 60,
        secondary_resets_at=args.secondary_resets_at or now + 7 * 24 * 60 * 60,
        event_ts=time.time(),
        last_activity_at=time.time(),
    )


class ApprovalHub:
    def __init__(self) -> None:
        self._next = 1
        self._active: str | None = None
        self._queue: list[str] = []
        self._pending: dict[str, asyncio.Future[str]] = {}
        self._text: dict[str, tuple[str, str]] = {}

    def add(self, future: asyncio.Future[str], tool: str, hint: str) -> str:
        prompt_id = f"h{self._next}"
        self._next += 1
        self._pending[prompt_id] = future
        self._text[prompt_id] = (tool, hint)
        self._queue.append(prompt_id)
        return prompt_id

    def text_for(self, prompt_id: str) -> tuple[str, str]:
        return self._text.get(prompt_id, ("PERMISSION", "Codex permission request"))

    def current(self) -> str | None:
        if self._active in self._pending:
            return self._active
        while self._queue:
            prompt_id = self._queue[0]
            if prompt_id in self._pending:
                self._active = prompt_id
                return prompt_id
            self._queue.pop(0)
        self._active = None
        return None

    def resolve(self, prompt_id: str, decision: str) -> bool:
        future = self._pending.pop(prompt_id, None)
        if prompt_id in self._queue:
            self._queue.remove(prompt_id)
        if self._active == prompt_id:
            self._active = None
        self._text.pop(prompt_id, None)
        if future is None or future.done():
            return False
        future.set_result(decision)
        return True


class Bridge:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.hub = ApprovalHub()
        self.tracker = ActivityTracker()
        self.buffer = bytearray()
        self.client: Any = None

    def current_snapshot(self) -> UsageSnapshot:
        if self.args.manual:
            return manual_snapshot(self.args)
        found = read_usage(self.args.codex_home, self.args.thread_limit, self.args.tail_bytes)
        if found is not None:
            return found
        return manual_snapshot(self.args)

    def current_packet(self) -> dict[str, Any]:
        snapshot = self.current_snapshot()
        state = choose_state(
            snapshot,
            self.tracker,
            time.time(),
            busy_window=self.args.busy_window,
            completed_window=self.args.completed_window,
            attention_window=self.args.attention_window,
            sleep_window=self.args.sleep_window,
            forced=self.args.state,
        )
        return snapshot.packet(state)

    async def write_json(self, payload: dict[str, Any]) -> None:
        if self.client is None:
            return
        raw = (json.dumps(payload, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8")
        await self.client.write_gatt_char(NUS_RX, raw, response=False)

    def handle_notify(self, _sender: Any, data: bytearray) -> None:
        self.buffer.extend(data)
        while b"\n" in self.buffer:
            line, _, rest = self.buffer.partition(b"\n")
            self.buffer = bytearray(rest)
            text = line.decode("utf-8", errors="replace").strip()
            if not text:
                continue
            try:
                message = json.loads(text)
            except json.JSONDecodeError:
                print(f"[ble] non-json notify: {text[:120]}", file=sys.stderr)
                continue
            if not isinstance(message, dict) or message.get("cmd") != "permission":
                if self.args.verbose:
                    print(f"[ble] {message}", file=sys.stderr)
                continue
            decision = normalize_device_decision(str(message.get("decision") or ""))
            prompt_id = str(message.get("id") or "")
            if decision is None or not self.hub.resolve(prompt_id, decision):
                print(f"[approval] ignored decision for {prompt_id}", file=sys.stderr)

    async def serve_approvals(self) -> None:
        sock_path = self.args.approval_sock
        sock_path.parent.mkdir(parents=True, exist_ok=True)
        if sock_path.exists():
            sock_path.unlink()
        server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        server.bind(str(sock_path))
        server.listen(4)
        server.setblocking(False)
        loop = asyncio.get_running_loop()
        try:
            while True:
                client, _addr = await loop.sock_accept(server)
                asyncio.create_task(self._approval_client(client))
        finally:
            server.close()
            with contextlib.suppress(OSError):
                sock_path.unlink()

    async def _approval_client(self, client: socket.socket) -> None:
        loop = asyncio.get_running_loop()
        try:
            chunks = bytearray()
            while b"\n" not in chunks and len(chunks) < 65536:
                piece = await loop.sock_recv(client, 4096)
                if not piece:
                    break
                chunks.extend(piece)
            request = json.loads(chunks.decode("utf-8", errors="replace").splitlines()[0])
            hook = request.get("hook") if isinstance(request.get("hook"), dict) else {}
            timeout = float(request.get("timeout") or 45)
            future: asyncio.Future[str] = loop.create_future()
            tool, hint = prompt_from_hook(hook)
            prompt_id = self.hub.add(future, tool, hint)
            if self.hub.current() == prompt_id:
                await self.write_json(prompt_packet(prompt_id, tool, hint))
            try:
                decision = await asyncio.wait_for(future, timeout)
            except asyncio.TimeoutError:
                self.hub.resolve(prompt_id, "cancel")
                response = {"ok": False, "reason": "timeout"}
            else:
                behavior = hook_behavior(decision)
                response = {"ok": True, "decision": behavior} if behavior else {"ok": False, "reason": "decision"}
            await self.write_json(clear_prompt_packet())
            nxt = self.hub.current()
            if nxt is not None:
                nxt_tool, nxt_hint = self.hub.text_for(nxt)
                await self.write_json(prompt_packet(nxt, nxt_tool, nxt_hint))
            payload = (json.dumps(response, separators=(",", ":")) + "\n").encode("utf-8")
            await loop.sock_sendall(client, payload)
        except Exception as exc:
            print(f"[approval] {exc}", file=sys.stderr)
        finally:
            client.close()

    async def usage_loop(self) -> None:
        while True:
            if self.client is not None:
                try:
                    await self.write_json(self.current_packet())
                except Exception as exc:
                    print(f"[ble] write failed: {exc}", file=sys.stderr)
            await asyncio.sleep(self.args.interval)

    async def connect_forever(self) -> None:
        from bleak import BleakClient, BleakScanner

        while True:
            try:
                device = await self._find(BleakScanner)
                if device is None:
                    print("[ble] Passport not found", file=sys.stderr)
                    await asyncio.sleep(self.args.restart_delay)
                    continue
                async with BleakClient(device) as client:
                    self.client = client
                    await client.start_notify(NUS_TX, self.handle_notify)
                    print(f"[ble] connected {getattr(device, 'name', device)}", file=sys.stderr)
                    while client.is_connected:
                        await asyncio.sleep(1)
            except Exception as exc:
                print(f"[ble] {exc}", file=sys.stderr)
            finally:
                self.client = None
            await asyncio.sleep(self.args.restart_delay)

    async def _find(self, scanner: Any) -> Any:
        if self.args.address:
            return self.args.address
        devices = await scanner.discover(timeout=self.args.scan_timeout, return_adv=True)
        for device, adv in devices.values():
            name = device.name or adv.local_name or ""
            if self.args.name and self.args.name not in name:
                continue
            uuids = {str(item).lower() for item in (adv.service_uuids or [])}
            if NUS_SERVICE in uuids or self.args.name in name:
                return device
        return None

    async def run(self) -> None:
        if self.args.print_packet:
            print(json.dumps(self.current_packet(), ensure_ascii=False))
            return
        tasks = [
            asyncio.create_task(self.serve_approvals()),
            asyncio.create_task(self.usage_loop()),
            asyncio.create_task(self.connect_forever()),
        ]
        done, pending = await asyncio.wait(tasks, return_when=asyncio.FIRST_EXCEPTION)
        for task in pending:
            task.cancel()
        for task in done:
            task.result()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Passport Codex Buddy BLE bridge")
    parser.add_argument("--name", default="Codex-")
    parser.add_argument("--address", default=None)
    parser.add_argument("--interval", type=float, default=5.0)
    parser.add_argument("--scan-timeout", type=float, default=8.0)
    parser.add_argument("--restart-delay", type=float, default=5.0)
    parser.add_argument("--state", default="auto", choices=("auto", *STATES))
    parser.add_argument("--manual", action="store_true")
    parser.add_argument("--primary", type=int, default=0)
    parser.add_argument("--secondary", type=int, default=0)
    parser.add_argument("--tokens", type=int, default=0)
    parser.add_argument("--primary-resets-at", type=int, default=0)
    parser.add_argument("--secondary-resets-at", type=int, default=0)
    parser.add_argument("--codex-home", type=Path, default=Path.home() / ".codex")
    parser.add_argument("--thread-limit", type=int, default=4)
    parser.add_argument("--tail-bytes", type=int, default=2_000_000)
    parser.add_argument("--busy-window", type=float, default=20.0)
    parser.add_argument("--completed-window", type=float, default=8.0)
    parser.add_argument("--attention-window", type=float, default=45.0)
    parser.add_argument("--sleep-window", type=float, default=180.0)
    parser.add_argument("--approval-sock", type=Path, default=STATE_DIR / "approval.sock")
    parser.add_argument("--print-packet", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    try:
        asyncio.run(Bridge(args).run())
    except KeyboardInterrupt:
        return 0
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
