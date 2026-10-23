#!/usr/bin/env python3
"""Start one background Passport BLE bridge, or report that it is already up."""

from __future__ import annotations

import fcntl
import os
import subprocess
import sys
from pathlib import Path


STATE_DIR = Path.home() / ".codex" / "codex-passport-bridge"
PID_PATH = STATE_DIR / "bridge.pid"
BRIDGE = Path(__file__).resolve().parent / "codex_usage_ble_bridge.py"
LOG_PATH = STATE_DIR / "bridge.log"


def pid_alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    return True


def main() -> int:
    STATE_DIR.mkdir(parents=True, exist_ok=True)
    lock = (STATE_DIR / "bridge.lock").open("a", encoding="utf-8")
    try:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if PID_PATH.exists():
            try:
                pid = int(PID_PATH.read_text().strip())
            except ValueError:
                pid = 0
            if pid and pid_alive(pid):
                print(f"bridge already running pid={pid}")
                return 0
        log = LOG_PATH.open("a", encoding="utf-8")
        proc = subprocess.Popen(
            [sys.executable, str(BRIDGE)],
            cwd=str(BRIDGE.parent),
            stdout=log,
            stderr=log,
            start_new_session=True,
        )
        PID_PATH.write_text(str(proc.pid))
        print(f"bridge started pid={proc.pid}")
        return 0
    finally:
        fcntl.flock(lock, fcntl.LOCK_UN)
        lock.close()


if __name__ == "__main__":
    raise SystemExit(main())
