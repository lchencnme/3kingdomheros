#!/usr/bin/env python3
"""Contact sheet of the 16x24 map bodies (tools/gen_tech01.py UNIT24) in the player / enemy palettes,
for reviewing silhouettes without the emulator. Output: build/art_bodies.png"""
import runpy
import sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from png_shot import write_png  # noqa: E402

g = runpy.run_path(str(ROOT / "tools/gen_tech01.py"), run_name="art_preview")
PAPER = g["PAPER"]
pals = [g["BG_PALS"][4], g["BG_PALS"][5]]
ground = g["BG_PALS"][0][1]
S, PAD = 6, 4
names = g["ORDER"]
cols = len(names)
W, H = cols * (16 + PAD) * S, 2 * (24 + PAD) * S
img = [[PAPER] * W for _ in range(H)]
for row, pal in enumerate(pals):
    for k, nm in enumerate(names):
        art = g["UNIT24"][nm]
        ox, oy = k * (16 + PAD) * S, row * (24 + PAD) * S
        for y in range(24):
            for x in range(16):
                ch = art[y][x]
                c = ground if ch == "." else pal[g["CH"][ch]]
                for dy in range(S):
                    img[oy + y * S + dy][ox + x * S:ox + (x + 1) * S] = [c] * S
rows = [bytearray(b for px in r for b in px) for r in img]
out = ROOT / "build/art_bodies.png"
write_png(out, W, H, rows)
print(out)
