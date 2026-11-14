#!/bin/sh
# ROM-only build (no tests): used for quick iteration. Same flags as build.sh.
set -e
cd "$(dirname "$0")/.."
LCC=${GBDK_LCC:-tools/gbdk/bin/lcc}
mkdir -p build/gb
$LCC -Wa-l -Wl-m -Wl-j -Wm-yC -Wm-yt0x1B -Wm-yo32 -Wm-ya16 -Wm-ynSANGUOHJZ \
     -Wl-g.STACK=0xD000 -Wl-b_DATA=0xC600 -o build/gb/sanguo_hjz.gbc \
     src/gb/*.c src/core/rng.c src/core/rules.c src/core/path.c src/core/battle.c src/core/ai.c src/core/save.c src/core/vm.c src/core/strategy.c src/core/item.c src/core/roster.c src/core/undo.c \
     src/gen/*.c > build/gb/lcc.log 2>&1 || { cat build/gb/lcc.log; echo "lcc FAILED"; exit 1; }
grep -v "warning 110\|^src/.*:$" build/gb/lcc.log || true
python3 -I tools/check_banks.py build/gb/sanguo_hjz.map
