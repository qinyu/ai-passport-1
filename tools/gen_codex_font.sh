#!/bin/sh
# Regenerate the Codex Buddy LVGL subsets from WenQuanYi Micro Hei.
# Converter: lv_font_conv 1.5.3. Source face: WenQuanYi Micro Hei 0.2.0-beta
# (Apache-2.0 or GPL-3.0-with-font-exception). Do not commit the full face.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
symbols=$(tr -d '\n' < "$root/assets/fonts/codex_buddy_symbols.txt")
src=${WQY_TTF:-/tmp/wqy-microhei.ttf}
collection=${WQY_TTC:-/usr/share/fonts/truetype/wqy/wqy-microhei.ttc}

if [ ! -f "$src" ]; then
    python3 - << PY
from fontTools.ttLib import TTCollection
TTCollection("$collection").fonts[0].save("$src")
PY
fi

for size in 16 20; do
    npx --yes lv_font_conv@1.5.3 \
        --font "$src" \
        --range 0x20-0x7E \
        --symbols "$symbols" \
        --size "$size" --bpp 4 --format lvgl --no-compress --no-kerning \
        --lv-font-name "codex_font_${size}" --lv-include lvgl.h \
        --output "$root/assets/fonts/codex_font_${size}.c"
done
