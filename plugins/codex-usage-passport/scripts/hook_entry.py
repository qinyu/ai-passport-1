#!/usr/bin/env python3
"""Codex hook entry for the Passport companion.

SessionStart and UserPromptSubmit start the local bridge. PermissionRequest
asks the bridge for an on-device allow/deny and otherwise fails open so Codex
keeps its normal prompt.
"""

from __future__ import annotations

import argparse
import json
import os
import select
import socket
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

from codex_bridge_core import permission_hook_output


PLUGIN_ROOT = Path(__file__).resolve().parents[1]
START_BRIDGE = PLUGIN_ROOT / "scripts" / "start_bridge.py"
STATE_DIR = Path.home() / ".codex" / "codex-passport-bridge"
HOOK_LOG = STATE_DIR / "hook.log"
APPROVAL_SOCK = STATE_DIR / "approval.sock"
APPROVAL_WAIT_SEC = 45.0
APPROVAL_CONNECT_SEC = 4.0


def append_log(record: dict[str, Any]) -> None:
    try:
        STATE_DIR.mkdir(parents=True, exist_ok=True)
        with HOOK_LOG.open("a", encoding="utf-8") as handle:
            handle.write(json.dumps(record, ensure_ascii=False, sort_keys=True) + "\n")
    except OSError:
        pass


def read_stdin_text(wait_sec: float = 2.0) -> str:
    try:
        if sys.stdin is None or sys.stdin.closed or sys.stdin.isatty():
            return ""
        ready, _, _ = select.select([sys.stdin], [], [], wait_sec)
        if not ready:
            return ""
        return sys.stdin.read(65536)
    except OSError:
        return ""


def request_decision(hook_payload: dict[str, Any]) -> dict[str, Any] | None:
    request = {"type": "permission_request", "hook": hook_payload, "timeout": APPROVAL_WAIT_SEC}
    encoded = (json.dumps(request, separators=(",", ":"), ensure_ascii=False) + "\n").encode("utf-8")
    deadline = time.monotonic() + APPROVAL_CONNECT_SEC
    while True:
        try:
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
                sock.settimeout(max(0.2, min(1.0, deadline - time.monotonic())))
                sock.connect(str(APPROVAL_SOCK))
                sock.sendall(encoded)
                sock.settimeout(APPROVAL_WAIT_SEC + 2.0)
                raw = sock.makefile("rb").readline(4096)
            if not raw:
                return None
            response = json.loads(raw.decode("utf-8", errors="replace"))
            if not response.get("ok"):
                return None
            decision = response.get("decision")
            if decision == "allow":
                return permission_hook_output("allow", "Approved from Passport")
            if decision == "deny":
                return permission_hook_output("deny", "Denied from Passport")
            return None
        except (FileNotFoundError, ConnectionRefusedError, socket.timeout, OSError):
            if time.monotonic() >= deadline:
                return None
            time.sleep(0.2)
        except json.JSONDecodeError:
            return None


def main() -> int:
    parser = argparse.ArgumentParser(description="Passport Codex Buddy hook")
    parser.add_argument("--event", default="unknown")
    args = parser.parse_args()
    stdin_text = read_stdin_text()
    append_log({"event": args.event, "phase": "received", "cwd": os.getcwd()})
    try:
        proc = subprocess.run(
            [sys.executable, str(START_BRIDGE)],
            cwd=str(PLUGIN_ROOT),
            capture_output=True,
            text=True,
            timeout=6,
            check=False,
        )
        append_log({"event": args.event, "phase": "start_bridge", "returncode": proc.returncode})
    except (OSError, subprocess.TimeoutExpired) as exc:
        append_log({"event": args.event, "phase": "start_error", "error": type(exc).__name__})

    if args.event == "PermissionRequest":
        try:
            hook_payload = json.loads(stdin_text) if stdin_text else {}
        except json.JSONDecodeError:
            return 0
        if not isinstance(hook_payload, dict):
            return 0
        decision = request_decision(hook_payload)
        if decision:
            sys.stdout.write(json.dumps(decision, separators=(",", ":"), ensure_ascii=False))
            sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
