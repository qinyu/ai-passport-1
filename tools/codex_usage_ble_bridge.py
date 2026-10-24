#!/usr/bin/env python3
"""Launch the Passport Codex Buddy bridge shipped with this branch."""

from __future__ import annotations

import runpy
import sys
from pathlib import Path


TARGET = Path(__file__).resolve().parents[1] / "plugins" / "codex-usage-passport" / "scripts" / "codex_usage_ble_bridge.py"
sys.path.insert(0, str(TARGET.parent))
runpy.run_path(str(TARGET), run_name="__main__")
