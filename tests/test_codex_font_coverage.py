#!/usr/bin/env python3
"""Check the Codex Buddy UI strings against the generated LVGL fonts.

Dynamic prompt hints are not part of this fixed set. The bridge sends short
ASCII tool text; characters outside the subset stay visible as LVGL placeholders.
"""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FONTS = (
    ROOT / "assets" / "fonts" / "codex_font_16.c",
    ROOT / "assets" / "fonts" / "codex_font_20.c",
)
SOURCES = (
    ROOT / "main" / "codex_ui.c",
    ROOT / "main" / "codex_pet.c",
)
SYMBOLS = ROOT / "assets" / "fonts" / "codex_buddy_symbols.txt"
MISSING_PROBE = 0x9F98


def font_codepoints(path: Path) -> set[int]:
    text = path.read_text(encoding="utf-8")
    lists: dict[str, list[int]] = {}
    for name, body in re.findall(
        r"static const uint16_t (unicode_list_\d+)\[\] = \{(.*?)\};", text, re.S
    ):
        lists[name] = [int(item, 0) for item in re.findall(r"0x[0-9a-fA-F]+|\d+", body)]
    cmap_at = text.find("cmaps[]")
    if cmap_at < 0:
        raise SystemExit(f"{path.name}: missing cmap table")
    points: set[int] = set()
    for body in re.findall(r"\{([^{}]+)\}", text[cmap_at:]):
        if "range_start" not in body:
            continue
        start = int(re.search(r"range_start = (\d+)", body).group(1))  # type: ignore[union-attr]
        length = int(re.search(r"range_length = (\d+)", body).group(1))  # type: ignore[union-attr]
        kind = re.search(r"type = (\w+)", body).group(1)  # type: ignore[union-attr]
        if kind.endswith("FORMAT0_TINY"):
            points.update(range(start, start + length))
            continue
        listed = re.search(r"unicode_list = (unicode_list_\d+)", body)
        if listed is None:
            raise SystemExit(f"{path.name}: sparse cmap has no unicode list")
        for offset in lists[listed.group(1)]:
            points.add(start + offset)
    return points


def c_string_literals(path: Path) -> list[str]:
    text = path.read_text(encoding="utf-8")
    values: list[str] = []
    index = 0
    while True:
        quote = text.find('"', index)
        if quote < 0:
            break
        index = quote + 1
        chars: list[str] = []
        while index < len(text):
            char = text[index]
            if char == "\\":
                index += 1
                if index >= len(text):
                    break
                escaped = text[index]
                mapping = {"n": "\n", "t": "\t", "r": "\r", '"': '"', "\\": "\\"}
                chars.append(mapping.get(escaped, escaped))
                index += 1
                continue
            if char == '"':
                values.append("".join(chars))
                index += 1
                break
            chars.append(char)
            index += 1
    return values


def main() -> None:
    required: set[int] = set()
    for source in SOURCES:
        for literal in c_string_literals(source):
            for char in literal:
                if char.isprintable() and char not in "\u200b":
                    required.add(ord(char))
    symbols = SYMBOLS.read_text(encoding="utf-8").strip()
    symbol_points = {ord(char) for char in symbols}
    cjk_required = {point for point in required if point > 0x7E}
    if cjk_required != symbol_points:
        missing = "".join(chr(point) for point in sorted(cjk_required - symbol_points))
        extra = "".join(chr(point) for point in sorted(symbol_points - cjk_required))
        raise SystemExit(f"symbol file mismatch missing={missing!r} extra={extra!r}")
    for path in FONTS:
        covered = font_codepoints(path)
        absent = sorted(point for point in required if point not in covered)
        if absent:
            shown = " ".join(f"U+{point:04X}" for point in absent)
            raise SystemExit(f"{path.name} missing {shown}")
        if MISSING_PROBE in covered:
            raise SystemExit(f"{path.name} unexpectedly contains U+9F98")
        if not all(point in covered for point in range(0x20, 0x7F)):
            raise SystemExit(f"{path.name} is missing printable ASCII")
    print("test_codex_font_coverage: PASS")


if __name__ == "__main__":
    main()
