#!/usr/bin/env python3
"""Contact sheet of the fullscreen actors (tools/gen_actors.py): 3 bodies x 4 poses, player palette -> build/art_actors.png"""
import runpy
import sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from png_shot import write_png  # noqa: E402
g = runpy.run_path(str(ROOT / "tools/gen_actors.py"), run_name="preview")
S = 4
pal = g["PALS"][1]
cells = [(n, p) for n, _ in g["BODIES"] for p in g["POSES"]]
W, H = 4 * (g["AW"] + 4) * S, 3 * (g["AH"] + 4) * S
img = [[g["PAPER"]] * W for _ in range(H)]
for k, (name, fn) in enumerate(g["BODIES"]):
    for j, pose in enumerate(g["POSES"]):
        cv = g["Canvas"](g["AW"], g["AH"])
        fn(cv, pose)
        ox, oy = j * (g["AW"] + 4) * S, k * (g["AH"] + 4) * S
        for y in range(g["AH"]):
            for x in range(g["AW"]):
                v = cv.px[y][x]
                col = (200, 188, 156) if v == 0 else pal[v]
                for dy in range(S):
                    img[oy + y * S + dy][ox + x * S:ox + (x + 1) * S] = [col] * S
write_png(ROOT / "build/art_actors.png", W, H, [bytearray(b for px in r for b in px) for r in img])
print("build/art_actors.png")
