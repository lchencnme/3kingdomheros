#!/bin/sh
# ROM-only build (no tests): used for quick iteration. Same flags as build.sh.
set -e
cd "$(dirname "$0")/.."
LCC=${GBDK_LCC:-tools/gbdk/bin/lcc}
mkdir -p build/gb
$LCC -Wa-l -Wl-m -Wl-j -Wm-yC -Wm-yt0x1B -Wm-yo512 -Wm-ya16 -Wm-ynSANGUOHJZ \
     -Wl-g.STACK=0xD000 -Wl-b_DATA=0xC600 -o build/gb/sanguo_hjz.gbc \
     src/gb/*.c src/core/rng.c src/core/rules.c src/core/path.c src/core/battle.c src/core/ai.c \
     src/gen/*.c 2>&1 | grep -v "warning 110\|^src/" || true
python3 -I tools/check_banks.py build/gb/sanguo_hjz.map
