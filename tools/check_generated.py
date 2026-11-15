#!/usr/bin/env python3
"""Fail the ROM build when generated sources disagree with the text table.

Text ids (TX_* / TXD_*) are positions in src/gen/text_data.h: adding a string renumbers the ones after it.
Every generator that writes text ids as numbers (portraits, camps, battles, events) must run after
gen_font.py; a stale file shows the wrong names and lines (e.g. a portrait labelled with another
character's name). This checks the portrait names exactly and that no file holding text ids is older
than text_data.h.
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GEN = ROOT / "src/gen"
th = (GEN / "text_data.h").read_text(encoding="utf-8")
tx = {m.group(1): int(m.group(2)) for m in re.finditer(r"#define (TXD?_\w+) (\d+)u", th)}
bad = []
reg = json.loads((ROOT / "content/portraits.json").read_text(encoding="utf-8"))["portraits"]
refs = (GEN / "portrait_b19.c").read_text(encoding="utf-8")
m = re.search(r"portrait_refs\[PORTRAIT_COUNT\] = \{(.*?)\};", refs, re.S)
rows = re.findall(r"\{(\d+), portrait_(\d+)_tiles, (\d+), (\d+)\}", m.group(1)) if m else []
if len(rows) != len(reg):
    bad.append(f"portrait_refs has {len(rows)} entries, content/portraits.json {len(reg)}")
for (bank, i, name, tint), e in zip(rows, reg):
    want = tx.get("TX_NAME_" + e["pinyin"].upper())
    if int(name) != want:
        bad.append(f"portrait {i} ({e['name']}): name id {name}, text_data.h says {want}")
t0 = (GEN / "text_data.h").stat().st_mtime
for f in ("portrait_b19.c", "campaign_camps.c", "campaign_battles.c", "campaign_names.c", "tech01_events.c",
          "tech01_index.c", "title_data.c"):
    p = GEN / f
    if p.exists() and p.stat().st_mtime < t0 and "TX" in p.read_text(encoding="utf-8")[:0] + "TX":
        if re.search(r"\bTXD?_\w+\b|\d", p.read_text(encoding="utf-8")) and p.stat().st_mtime + 1 < t0:
            bad.append(f"{f} is older than text_data.h: re-run its generator after gen_font.py")
if bad:
    sys.exit("check_generated: FAIL\n  " + "\n  ".join(bad))
print(f"check_generated: {len(rows)} portrait names match the text table")
