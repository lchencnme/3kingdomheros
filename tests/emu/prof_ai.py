#!/usr/bin/env python3
"""Profile enemy-phase AI on the ROM: end turns until the outcome, print per-decision timings."""
import sys, warnings, logging
warnings.filterwarnings("ignore"); logging.disable(logging.WARNING)
sys.argv = ["x", sys.argv[1] if len(sys.argv) > 1 else "build/gb/sanguo_hjz.gbc"]
src = open("tests/emu/test_rom.py").read()
exec(src.split("# ------------------------------------------------------------------ basic flow")[0])
em = Emu(); em.wait_state([G_IDLE])
seen = set(); worst = 0
while em.state() != G_OUTCOME and em.fc < 30000:
    if em.state() == G_IDLE:
        em.press("start")
    em.frames(1)
    key = (em.r16("_dbg_ai_t_range"), em.r16("_dbg_ai_t_field"), em.r16("_dbg_ai_t_cand"), em.r16("_dbg_t_ai"))
    if key != (0, 0, 0, 0) and key not in seen:
        seen.add(key); worst = max(worst, key[3])
        print("range %3d field %3d cand %3d total %3d" % key)
print("worst", worst, "outcome", em.r8("_dbg_outcome"), "round", em.r16("_dbg_round"))
