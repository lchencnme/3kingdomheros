#!/usr/bin/env python3
"""Render a battle map view (terrain tiles + unit bodies + palettes from tools/gen_tech01.py) for art review.
Usage: art_preview_map.py BT_16 [cam_x cam_y] -> build/art_map_<id>.png (160x112 view, x3)"""
import json
import runpy
import sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from png_shot import write_png  # noqa: E402

g = runpy.run_path(str(ROOT / "tools/gen_tech01.py"), run_name="preview")
bid = sys.argv[1] if len(sys.argv) > 1 else "BT_16"
cx, cy = (int(sys.argv[2]), int(sys.argv[3])) if len(sys.argv) > 3 else (0, 0)
b = json.loads((ROOT / f"content/battles/{bid}.json").read_text(encoding="utf-8"))
w, h, terr = b["map"]["width"], b["map"]["height"], b["map"]["terrain"]
rules = json.loads((ROOT / "content/default_rules.json").read_text(encoding="utf-8"))
CH, PALS = g["CH"], g["BG_PALS"]
VW, VH = 160, 112
img = [[PALS[0][0]] * VW for _ in range(VH)]
for y in range(VH):
    for x in range(VW):
        wx, wy = x + cx * 16, y + cy * 16
        if wx >= w * 16 or wy >= h * 16:
            continue
        t = terr[(wy // 16) * w + wx // 16]
        if t == 0:
            v = ((wx // 16) * 7 + (wy // 16) * 13) & 7
            t = 10 if v == 0 else (11 if v == 3 else 0)
        pt = t
        img[y][x] = PALS[g["TERRAIN_PAL"][t]][CH[g["ART"][t][wy % 16][wx % 16]]]
units = [(s[0], s[1], 0, 0) for s in b["deploy"]["slots"][:b["deploy"]["max_count"]]]
units += [(u["x"], u["y"], u["class"], {"PLAYER": 0, "ALLY": 1, "ENEMY": 2}[u["faction"]]) for u in b["units"]]
roster_cls = [o["class"] for o in json.loads((ROOT / "content/roster.json").read_text(encoding="utf-8"))["officers"]]
units = [(x, y, roster_cls[k] if k < len(roster_cls) and f == 0 and k < b["deploy"]["max_count"] else c, f)
         for k, (x, y, c, f) in enumerate(units)]
for ux, uy, cls, fac in sorted(units, key=lambda u: u[1]):
    art = g["UNIT24"][g["CLASS_BODY"][cls]]
    pal = PALS[5 if fac == 2 else 4]
    for y in range(24):
        for x in range(16):
            ch = art[y][x]
            px, py = (ux - cx) * 16 + x, (uy - cy) * 16 - 8 + y
            if ch != "." and 0 <= px < VW and 0 <= py < VH:
                img[py][px] = pal[CH[ch]]
S = 3
rows = []
for r in img:
    line = bytearray(b for px in r for b in px * 1 for _ in [0])
    big = bytearray()
    for px in r:
        big += bytes(px) * S
    rows += [big] * S
out = ROOT / f"build/art_map_{bid}.png"
write_png(out, VW * S, VH * S, rows)
print(out)
