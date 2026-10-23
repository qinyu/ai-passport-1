#!/usr/bin/env python3
"""Host checks for the Passport Codex bridge packet and hook mapping."""

from __future__ import annotations

import json
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
)


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
    print("test_codex_bridge: PASS")


if __name__ == "__main__":
    main()
