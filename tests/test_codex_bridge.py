#!/usr/bin/env python3
"""Host checks for the Passport Codex bridge packet and hook mapping."""

from __future__ import annotations

import asyncio
import json
import socket
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "plugins" / "codex-usage-passport" / "scripts"))

from codex_bridge_core import (  # noqa: E402
    ActivityTracker,
    choose_state,
    hook_behavior,
    normalize_device_decision,
    prompt_from_hook,
    prompt_packet,
    reduce_rollout_lines,
    roll_reset_at,
    session_origin,
    short_text,
)
from codex_usage_ble_bridge import Bridge  # noqa: E402


def expect(cond: bool, what: str) -> None:
    if not cond:
        raise SystemExit(f"FAIL {what}")


def main() -> None:
    line = json.dumps(
        {
            "timestamp": "2026-10-23T00:00:00+00:00",
            "payload": {
                "type": "token_count",
                "info": {"total_token_usage": {"total_tokens": 1800}},
                "rate_limits": {
                    "limit_id": "codex",
                    "primary": {"used_percent": 42.2, "resets_at": 1_775_731_234},
                    "secondary": {"used_percent": 15, "resets_at": 1_776_331_234},
                },
            },
        }
    )
    started = json.dumps(
        {"timestamp": "2026-10-23T00:00:05+00:00", "payload": {"type": "task_started"}}
    )
    snapshot = reduce_rollout_lines([started, line, "not-json"])
    expect(snapshot is not None, "snapshot")
    assert snapshot is not None
    expect(snapshot.tokens == 1800, "tokens")
    expect(snapshot.primary == 42 and snapshot.secondary == 15, "percents")
    expect(snapshot.task_started_at is not None, "task start")

    now = snapshot.event_ts or 0
    state = choose_state(snapshot, ActivityTracker(), now + 1, busy_window=20, sleep_window=180)
    expect(state == "busy", f"busy got {state}")
    state = choose_state(snapshot, ActivityTracker(), now + 1000, busy_window=20, sleep_window=30)
    expect(state == "sleep", f"sleep got {state}")
    expect(choose_state(snapshot, ActivityTracker(), now, forced="dizzy") == "dizzy", "forced")

    packet = snapshot.packet("busy", now=1_775_700_000)
    expect(packet["agent"] == "PI" and packet["via"] == "pi", "default pi origin")
    expect(snapshot.packet("busy", now=1_775_700_000, via="herdr")["via"] == "herdr", "herdr via")
    expect(session_origin("/Users/yu/.pi/agent/sessions/a.jsonl") == "pi", "local pi path")
    expect(session_origin("mobile-relay hdr herdr") == "herdr", "herdr marker")
    expect(packet["state"] == "busy" and packet["primary_label"] == "5h", "packet labels")
    expect(packet["secondary_label"] == "7d" and packet["now"] == 1_775_700_000, "packet now")
    expect(roll_reset_at(100, 50, 99) == 100, "future reset stays")
    expect(roll_reset_at(100, 50, 100) == 150, "reset rolls at the boundary")
    expect(roll_reset_at(100, 50, 160) == 200, "past reset rolls")

    expect(normalize_device_decision("accept") == "accept", "accept")
    expect(normalize_device_decision("deny") == "cancel", "deny maps to cancel")
    expect(hook_behavior("accept") == "allow", "hook allow")
    expect(hook_behavior("cancel") == "deny", "hook deny")
    tool, hint = prompt_from_hook({"tool_name": "Bash", "tool_input": {"command": "echo hi"}})
    expect(tool == "Bash" and hint == "echo hi", "hook prompt")
    shown = prompt_packet("h1", tool, hint)
    expect(shown["prompt"]["id"] == "h1", "prompt id")
    clipped = short_text("echo build && rm -rf ~/project and then more", "fallback", 43)
    expect(clipped.endswith("...") and len(clipped) == 43, "ellipsis stays inside the limit")
    expect(short_text("echo hi", "fallback", 43) == "echo hi", "short text unchanged")
    asyncio.run(check_approval_paths())
    print("test_codex_bridge: PASS")


class _Gatt:
    def __init__(self, mtu: int, connected: bool = True, fail: bool = False) -> None:
        self.mtu_size = mtu
        self.is_connected = connected
        self.fail = fail
        self.writes: list[bytes] = []

    async def write_gatt_char(self, _uuid: str, data: bytes, response: bool = False) -> None:
        del response
        if self.fail:
            raise ConnectionError("write failed")
        self.writes.append(bytes(data))


class _Args:
    verbose = False


async def _exchange(bridge: Bridge, payload: bytes) -> bytes:
    left, right = socket.socketpair()
    left.setblocking(False)
    right.setblocking(False)
    loop = asyncio.get_running_loop()
    await loop.sock_sendall(right, payload)
    task = asyncio.create_task(bridge._approval_client(left))
    received = bytearray()
    while b"\n" not in received:
        piece = await asyncio.wait_for(loop.sock_recv(right, 256), 1.0)
        if not piece:
            break
        received.extend(piece)
    await asyncio.wait_for(task, 1.0)
    right.close()
    return bytes(received)


async def check_approval_paths() -> None:
    offline = Bridge(_Args())
    offline.client = None
    reply = await _exchange(offline, b'{"type":"permission_request","hook":{},"timeout":45}\n')
    expect(b'"reason":"offline"' in reply, "offline fails fast")
    expect(not offline.hub._pending and not offline.hub._queue, "offline does not queue")

    failing = Bridge(_Args())
    failing.client = _Gatt(23, fail=True)
    task = asyncio.create_task(
        _exchange(failing, b'{"type":"permission_request","hook":{"tool_name":"Bash"},"timeout":45}\n')
    )
    await asyncio.wait_for(task, 1.0)
    expect(not failing.hub._pending and not failing.hub._queue, "failed write clears the hub")
    expect(failing.hub.current() is None, "failed write does not stay current")

    gatt = _Gatt(23)
    bridge = Bridge(_Args())
    bridge.client = gatt
    await bridge.write_json({"state": "busy", "primary": 1, "pad": "x" * 40})
    expect(len(gatt.writes) > 1, "mtu splits the write")
    expect(all(len(chunk) <= 20 for chunk in gatt.writes), "chunks fit the default mtu")
    expect(b"".join(gatt.writes).endswith(b"\n"), "chunked write keeps the newline")


if __name__ == "__main__":
    main()
