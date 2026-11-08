#!/bin/sh
# One-command build (design doc 11.1): generate data, host tests, ROM, emulator tests.
set -e
cd "$(dirname "$0")"
LCC=${GBDK_LCC:-tools/gbdk/bin/lcc}
python3 -I tools/gen_rules.py
python3 -I tools/gen_tech01.py
mkdir -p build/host build/gb
python3 -I tools/ref_model.py > build/host/ref_cases.txt
cc -std=c99 -Wall -Wextra -O1 -o build/host/test_core tests/host/test_core.c tests/host/host_grid.c \
   src/core/rng.c src/core/rules.c src/core/path.c src/gen/rules_data.c
build/host/test_core build/host/ref_cases.txt
$LCC -Wa-l -Wl-m -Wl-j -Wm-yC -Wm-yt0x1B -Wm-yo512 -Wm-ya16 -Wm-ynSANGUOHJZ \
     -Wl-g.STACK=0xD000 -Wl-b_DATA=0xC600 -o build/gb/sanguo_hjz.gbc \
     src/gb/main.c src/gb/wram.c src/gb/input.c src/gb/mapview.c \
     src/core/rng.c src/core/rules.c src/core/path.c src/gen/rules_data.c src/gen/tech01_data.c
shasum -a 256 build/gb/sanguo_hjz.gbc
python3 tests/emu/test_rom.py build/gb/sanguo_hjz.gbc
