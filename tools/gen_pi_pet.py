#!/usr/bin/env python3
"""Turn Stick Pi GIF frames into LVGL RGB565A8 images.

The frames come from the Pi character pack already used by Codex Buddy.
Near-black background becomes transparent. Frames are cropped together and
nearest-neighbor scaled so the pet stays a pixel sprite on the Passport.
"""

from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PACK = Path("/tmp/refs/codex-buddy/data/characters/Pi")
OUT = ROOT / "assets" / "images" / "pi_pet.c"
STATES = {
    "pi_pet_idle": "idle.gif",
    "pi_pet_busy": "busy.gif",
    "pi_pet_attention": "attention.gif",
    "pi_pet_dizzy": "dizzy.gif",
}
TARGET_H = 144
ALPHA_CUTOFF = 18


def first_frame(path: Path) -> Image.Image:
    image = Image.open(path)
    image.seek(0)
    return image.convert("RGBA")


def content_box(images: list[Image.Image]) -> tuple[int, int, int, int]:
    left, top, right, bottom = 10**9, 10**9, 0, 0
    for image in images:
        pixels = image.load()
        width, height = image.size
        for y in range(height):
            for x in range(width):
                red, green, blue, alpha = pixels[x, y]
                if alpha < 16 or max(red, green, blue) < ALPHA_CUTOFF:
                    continue
                left = min(left, x)
                top = min(top, y)
                right = max(right, x + 1)
                bottom = max(bottom, y + 1)
    pad = 2
    return max(0, left - pad), max(0, top - pad), right + pad, bottom + pad


def rgb565(red: int, green: int, blue: int) -> int:
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)


def emit(name: str, image: Image.Image) -> str:
    width, height = image.size
    pixels = image.load()
    color = bytearray()
    alpha = bytearray()
    for y in range(height):
        for x in range(width):
            red, green, blue, src_alpha = pixels[x, y]
            if src_alpha < 16 or max(red, green, blue) < ALPHA_CUTOFF:
                color += b"\x00\x00"
                alpha.append(0)
                continue
            value = rgb565(red, green, blue)
            color.append(value & 0xFF)
            color.append((value >> 8) & 0xFF)
            alpha.append(255)
    blob = color + alpha
    lines = [f"static const uint8_t {name}_map[] = {{"]
    for index in range(0, len(blob), 16):
        chunk = ", ".join(f"0x{byte:02x}" for byte in blob[index : index + 16])
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append(f"const lv_image_dsc_t {name} = {{")
    lines.append("    .header.magic = LV_IMAGE_HEADER_MAGIC,")
    lines.append("    .header.cf = LV_COLOR_FORMAT_RGB565A8,")
    lines.append("    .header.flags = 0,")
    lines.append(f"    .header.w = {width},")
    lines.append(f"    .header.h = {height},")
    lines.append(f"    .header.stride = {width * 2},")
    lines.append(f"    .data_size = {len(blob)},")
    lines.append(f"    .data = {name}_map,")
    lines.append("};")
    return "\n".join(lines)


def main() -> int:
    pack = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_PACK
    frames = {name: first_frame(pack / filename) for name, filename in STATES.items()}
    box = content_box(list(frames.values()))
    cropped = [image.crop(box) for image in frames.values()]
    height = cropped[0].size[1]
    scale = TARGET_H / height
    scaled = [
        image.resize((max(1, round(image.size[0] * scale)), TARGET_H), Image.Resampling.NEAREST)
        for image in cropped
    ]
    parts = [
        "/* Pi buddy frames from the Stick character pack. Black keyed out; nearest scaled. */",
        "#include \"lvgl.h\"",
        "",
    ]
    for (name, _), image in zip(STATES.items(), scaled):
        parts.append(emit(name, image))
        parts.append("")
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("\n".join(parts), encoding="utf-8")
    print(f"wrote {OUT} size={scaled[0].size} bytes={OUT.stat().st_size}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
