"""Pure helpers for the Passport Codex usage bridge.

The wire packet matches the Stick Codex Buddy fields the Passport firmware
parses: state, 5h/7d percents, reset epochs, and a prompt object. This module
does not open Bluetooth or sockets.
"""

from __future__ import annotations

import json
from dataclasses import dataclass
from datetime import datetime, timezone
from typing import Any


PRIMARY_WINDOW_SEC = 5 * 60 * 60
SECONDARY_WINDOW_SEC = 7 * 24 * 60 * 60
STATES = ("sleep", "idle", "busy", "attention", "completed", "dizzy", "heart")


def clamp_percent(value: Any) -> int:
    try:
        number = int(float(value))
    except (TypeError, ValueError):
        return 0
    return max(0, min(100, number))


def parse_timestamp(value: Any) -> float | None:
    if value is None:
        return None
    if isinstance(value, (int, float)):
        number = float(value)
        if number > 10_000_000_000:
            number /= 1000.0
        return number
    text = str(value).strip()
    if not text:
        return None
    if text.endswith("Z"):
        text = text[:-1] + "+00:00"
    try:
        return datetime.fromisoformat(text).timestamp()
    except ValueError:
        return None


def roll_reset_at(reset_at: int, window_sec: int, now: int) -> int:
    if reset_at <= 0 or window_sec <= 0 or reset_at > now:
        return reset_at
    elapsed = (now - reset_at) // window_sec + 1
    return reset_at + elapsed * window_sec


def reset_window_for_label(label: str | None) -> int:
    key = str(label or "").strip().lower()
    if key == "5h":
        return PRIMARY_WINDOW_SEC
    if key == "7d":
        return SECONDARY_WINDOW_SEC
    return 0


@dataclass
class UsageSnapshot:
    tokens: int = 0
    primary: int = 0
    secondary: int = 0
    primary_resets_at: int = 0
    secondary_resets_at: int = 0
    event_ts: float | None = None
    limit_id: str | None = None
    task_started_at: float | None = None
    task_complete_at: float | None = None
    attention_at: float | None = None
    last_activity_at: float | None = None

    def packet(self, state: str, now: int | None = None) -> dict[str, Any]:
        moment = int(now if now is not None else datetime.now(timezone.utc).timestamp())
        return {
            "state": state if state in STATES else "idle",
            "tokens": int(self.tokens),
            "primary": clamp_percent(self.primary),
            "secondary": clamp_percent(self.secondary),
            "primary_resets_at": roll_reset_at(self.primary_resets_at, PRIMARY_WINDOW_SEC, moment),
            "secondary_resets_at": roll_reset_at(
                self.secondary_resets_at, SECONDARY_WINDOW_SEC, moment
            ),
            "primary_label": "5h",
            "secondary_label": "7d",
            "now": moment,
            "agent": "CODEX",
        }


class ActivityTracker:
    def __init__(self) -> None:
        self.last_tokens: int | None = None
        self.last_event_ts: float | None = None
        self.last_growth_at: float | None = None

    def state_for(self, snapshot: UsageSnapshot, busy_window: float, now: float) -> str:
        if self.last_tokens is None:
            self.last_tokens = snapshot.tokens
            self.last_event_ts = snapshot.event_ts
            if snapshot.event_ts is not None and now - snapshot.event_ts <= busy_window:
                self.last_growth_at = now
                return "busy"
            return "idle"
        if snapshot.tokens != self.last_tokens:
            self.last_tokens = snapshot.tokens
            self.last_event_ts = snapshot.event_ts
            self.last_growth_at = now
            return "busy"
        if snapshot.event_ts is not None and snapshot.event_ts != self.last_event_ts:
            self.last_event_ts = snapshot.event_ts
            if now - snapshot.event_ts <= busy_window:
                self.last_growth_at = now
                return "busy"
        if self.last_growth_at is not None and now - self.last_growth_at <= busy_window:
            return "busy"
        return "idle"


def _newer(left: float | None, right: float | None) -> float | None:
    if right is None:
        return left
    if left is None or right > left:
        return right
    return left


def snapshot_from_event(event: dict[str, Any]) -> UsageSnapshot | None:
    payload = event.get("payload")
    if not isinstance(payload, dict) or payload.get("type") != "token_count":
        return None
    info = payload.get("info") if isinstance(payload.get("info"), dict) else {}
    total = info.get("total_token_usage") if isinstance(info.get("total_token_usage"), dict) else {}
    limits = payload.get("rate_limits") if isinstance(payload.get("rate_limits"), dict) else {}
    primary = limits.get("primary") if isinstance(limits.get("primary"), dict) else {}
    secondary = limits.get("secondary") if isinstance(limits.get("secondary"), dict) else {}
    return UsageSnapshot(
        tokens=int(total.get("total_tokens") or 0),
        primary=clamp_percent(primary.get("used_percent")),
        secondary=clamp_percent(secondary.get("used_percent")),
        primary_resets_at=int(primary.get("resets_at") or 0),
        secondary_resets_at=int(secondary.get("resets_at") or 0),
        event_ts=parse_timestamp(event.get("timestamp")),
        limit_id=limits.get("limit_id") if isinstance(limits.get("limit_id"), str) else None,
    )


def activity_from_event(event: dict[str, Any]) -> tuple[str | None, float | None]:
    payload = event.get("payload")
    if not isinstance(payload, dict):
        return None, None
    kind = str(payload.get("type") or "")
    stamp = parse_timestamp(event.get("timestamp"))
    if kind in {"token_count", "task_started", "task_complete"}:
        return kind, stamp
    lowered = kind.lower()
    if "permission" in lowered or "approval" in lowered:
        return "attention", stamp
    return None, stamp


def reduce_rollout_lines(lines: list[str], preferred_limit_id: str = "codex") -> UsageSnapshot | None:
    best: UsageSnapshot | None = None
    task_started_at: float | None = None
    task_complete_at: float | None = None
    attention_at: float | None = None
    last_activity_at: float | None = None
    for line in lines:
        if "token_count" not in line and "task_" not in line and "approval" not in line and "permission" not in line:
            continue
        try:
            event = json.loads(line)
        except json.JSONDecodeError:
            continue
        if not isinstance(event, dict):
            continue
        kind, stamp = activity_from_event(event)
        if kind in {"token_count", "task_started", "task_complete"}:
            last_activity_at = _newer(last_activity_at, stamp)
        if kind == "task_started":
            task_started_at = _newer(task_started_at, stamp)
        elif kind == "task_complete":
            task_complete_at = _newer(task_complete_at, stamp)
        elif kind == "attention":
            attention_at = _newer(attention_at, stamp)
        snapshot = snapshot_from_event(event)
        if snapshot is None:
            continue
        if snapshot.limit_id not in (None, preferred_limit_id):
            continue
        if best is None or (snapshot.event_ts or 0) >= (best.event_ts or 0):
            best = snapshot
    if best is None:
        return None
    best.task_started_at = task_started_at
    best.task_complete_at = task_complete_at
    best.attention_at = attention_at
    best.last_activity_at = last_activity_at
    return best


def choose_state(
    snapshot: UsageSnapshot,
    tracker: ActivityTracker,
    now: float,
    busy_window: float = 20.0,
    completed_window: float = 8.0,
    attention_window: float = 45.0,
    sleep_window: float = 180.0,
    forced: str | None = None,
) -> str:
    if forced and forced != "auto":
        return forced if forced in STATES else "idle"
    if snapshot.attention_at is not None and 0 <= now - snapshot.attention_at <= attention_window:
        return "attention"
    latest_start = snapshot.task_started_at or 0
    if (
        snapshot.task_complete_at is not None
        and snapshot.task_complete_at >= latest_start
        and 0 <= now - snapshot.task_complete_at <= completed_window
    ):
        return "completed"
    state = tracker.state_for(snapshot, busy_window, now)
    last_activity = snapshot.last_activity_at or snapshot.event_ts
    if state == "idle" and last_activity is not None and now - last_activity >= sleep_window:
        return "sleep"
    return state


def short_text(value: Any, fallback: str, limit: int) -> str:
    text = str(value if value not in (None, "") else fallback).replace("\n", " ").strip()
    if len(text) <= limit:
        return text
    return text[:limit]


def prompt_packet(prompt_id: str, tool: str, hint: str) -> dict[str, Any]:
    return {
        "prompt": {
            "id": prompt_id,
            "tool": short_text(tool, "TOOL", 19),
            "hint": short_text(hint, "Codex permission", 43),
        }
    }


def clear_prompt_packet() -> dict[str, Any]:
    return {"prompt": None}


def normalize_device_decision(raw: str) -> str | None:
    value = str(raw or "").strip().lower()
    if value in {"accept", "approve", "approved", "once"}:
        return "accept"
    if value in {"cancel", "deny", "denied", "decline", "abort"}:
        return "cancel"
    return None


def hook_behavior(device_decision: str) -> str | None:
    if device_decision == "accept":
        return "allow"
    if device_decision == "cancel":
        return "deny"
    return None


def permission_hook_output(behavior: str, message: str) -> dict[str, Any]:
    return {
        "continue": True,
        "hookSpecificOutput": {
            "hookEventName": "PermissionRequest",
            "decision": {"behavior": behavior, "message": message},
        },
    }


def prompt_from_hook(payload: dict[str, Any]) -> tuple[str, str]:
    tool = short_text(payload.get("tool_name"), "PERMISSION", 19)
    tool_input = payload.get("tool_input")
    hint: Any = None
    if isinstance(tool_input, dict):
        hint = (
            tool_input.get("command")
            or tool_input.get("cmd")
            or tool_input.get("path")
            or tool_input.get("file")
        )
        if isinstance(hint, list):
            hint = " ".join(str(item) for item in hint)
    elif tool_input:
        hint = tool_input
    if not hint:
        hint = payload.get("cwd") or "Codex permission request"
    return tool, short_text(hint, "Codex permission request", 43)
