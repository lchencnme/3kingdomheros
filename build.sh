#!/bin/sh
# One-command build (design doc 11.1): generate data, host tests, ROM, emulator tests.
set -e
cd "$(dirname "$0")"
LCC=${GBDK_LCC:-tools/gbdk/bin/lcc}
python3 -I tools/gen_rules.py
python3 -I tools/gen_tech01.py
python3 -I tools/gen_font.py
mkdir -p build/host build/gb
python3 -I tools/ref_model.py > build/host/ref_cases.txt
cc -std=c99 -Wall -Wextra -O1 -o build/host/test_core tests/host/test_core.c tests/host/host_grid.c \
   src/core/rng.c src/core/rules.c src/core/path.c src/gen/rules_data.c
build/host/test_core build/host/ref_cases.txt
cc -std=c99 -Wall -Wextra -O1 -o build/host/test_battle tests/host/test_battle.c tests/host/host_units.c \
   tests/host/host_grid.c src/core/battle.c src/core/ai.c src/core/rng.c src/core/rules.c src/core/path.c \
   src/gen/rules_data.c
build/host/test_battle
cc -std=c99 -Wall -Wextra -O2 -o build/host/test_save tests/host/test_save.c tests/host/host_sram.c \
   tests/host/host_units.c tests/host/host_grid.c src/core/save.c src/core/battle.c src/core/rng.c \
   src/core/rules.c src/core/path.c src/gen/rules_data.c
build/host/test_save
$LCC -Wa-l -Wl-m -Wl-j -Wm-yC -Wm-yt0x1B -Wm-yo512 -Wm-ya16 -Wm-ynSANGUOHJZ \
     -Wl-g.STACK=0xD000 -Wl-b_DATA=0xC600 -o build/gb/sanguo_hjz.gbc \
     src/gb/main.c src/gb/wram.c src/gb/input.c src/gb/mapview.c src/gb/hud.c src/gb/units_gb.c src/gb/rom.c src/gb/sram_gb.c \
     src/core/rng.c src/core/rules.c src/core/path.c src/core/battle.c src/core/ai.c src/core/save.c \
     src/gen/rules_data.c src/gen/tech01_data.c src/gen/tech01_index.c \
     src/gen/text_data.c src/gen/text_index.c > build/gb/lcc.log 2>&1 || { cat build/gb/lcc.log; echo "lcc FAILED"; exit 1; }
grep -v "warning 110\|^src/.*:$" build/gb/lcc.log || true
python3 -I tools/check_banks.py build/gb/sanguo_hjz.map
shasum -a 256 build/gb/sanguo_hjz.gbc
python3 tests/emu/test_rom.py build/gb/sanguo_hjz.gbc
