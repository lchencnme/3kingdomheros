#!/bin/sh
# One-command build (design doc 11.1): generate data, host tests, ROM, emulator tests.
set -e
cd "$(dirname "$0")"
LCC=${GBDK_LCC:-tools/gbdk/bin/lcc}
python3 -I tools/gen_rules.py
python3 -I tools/gen_tech01.py
python3 -I tools/gen_campaign.py
python3 -I tools/gen_font.py
python3 -I tools/gen_music.py
python3 -I tools/gen_actors.py
python3 -I tools/gen_portraits.py
python3 -I tools/gen_events.py
mkdir -p build/host build/gb
python3 -I tools/ref_model.py > build/host/ref_cases.txt
cc -std=c99 -Wall -Wextra -O1 -o build/host/test_core tests/host/test_core.c tests/host/host_grid.c \
   src/core/rng.c src/core/rules.c src/core/path.c src/gen/rules_data.c
build/host/test_core build/host/ref_cases.txt
cc -std=c99 -Wall -Wextra -O1 -o build/host/test_battle tests/host/test_battle.c tests/host/host_units.c \
   tests/host/host_grid.c tests/host/host_sram.c src/core/battle.c src/core/ai.c src/core/strategy.c src/core/item.c \
   src/core/rng.c src/core/rules.c src/core/path.c src/gen/rules_data.c
build/host/test_battle
cc -std=c99 -Wall -Wextra -O2 -o build/host/test_save tests/host/test_save.c tests/host/host_sram.c \
   tests/host/host_units.c tests/host/host_grid.c src/core/save.c src/core/battle.c src/core/item.c src/core/strategy.c src/core/rng.c \
   src/core/rules.c src/core/path.c src/gen/rules_data.c
build/host/test_save
cc -std=c99 -Wall -Wextra -O1 -o build/host/test_vm tests/host/test_vm.c tests/host/host_units.c \
   tests/host/host_grid.c tests/host/host_sram.c src/core/item.c src/core/strategy.c src/core/ai.c src/core/vm.c src/core/battle.c src/core/rng.c src/core/rules.c src/core/path.c \
   src/gen/rules_data.c
build/host/test_vm
cc -std=c99 -Wall -Wextra -O1 -o build/host/test_strategy tests/host/test_strategy.c tests/host/host_units.c \
   tests/host/host_grid.c tests/host/host_sram.c src/core/item.c src/core/strategy.c src/core/battle.c src/core/rng.c src/core/rules.c src/core/path.c \
   src/gen/rules_data.c
build/host/test_strategy
cc -std=c99 -Wall -Wextra -O1 -o build/host/test_item tests/host/test_item.c tests/host/host_units.c \
   tests/host/host_grid.c tests/host/host_sram.c src/core/item.c src/core/ai.c src/core/strategy.c src/core/save.c \
   src/core/roster.c src/core/battle.c src/core/rng.c src/core/rules.c src/core/path.c src/gen/rules_data.c
build/host/test_item
tools/build_rom.sh                     # single source of truth for the ROM link line
shasum -a 256 build/gb/sanguo_hjz.gbc
python3 tests/emu/test_rom.py build/gb/sanguo_hjz.gbc
