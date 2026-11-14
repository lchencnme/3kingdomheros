#!/usr/bin/env python3
"""Dialogue scroll panel: an 80x80 青绿山水 vignette (hanging-scroll composition: sun and far range, a main
peak with a pavilion on its shoulder, mist, a pine and a fishing boat in the foreground) shown beside the
speaker's portrait, plus the 回纹 fret tile of the divider under it.
Output: src/gen/scroll_data.{h,c} (asset bank 27): 100 tiles (10 x 10, row-major) + the fret tile + a palette.
"""
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from png_shot import write_png  # noqa: E402

N, BANK = 80, 27
PAL = [(232, 220, 184), (176, 206, 176), (60, 116, 100), (32, 26, 20)]     # paper, 石绿 wash, 石绿, ink
FRET = ["########", ".......#", ".#####.#", ".#...#.#", ".#.#.#.#", ".#.###.#", ".#.....#", ".#######"]


def paint():
    g = [[0] * N for _ in range(N)]

    def put(x, y, v):
        x, y = int(round(x)), int(round(y))
        if 2 <= x < N - 2 and 2 <= y < N - 2:
            g[y][x] = v

    def line(x0, y0, x1, y1, v=3):
        n = int(max(abs(x1 - x0), abs(y1 - y0)) * 2) + 1
        for i in range(n + 1):
            t = i / n
            put(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, v)

    def peak(cx, top, half, base, shade=True, strokes=True):
        """a mountain: ink contour, deep pigment on the shaded (left) flank, wash on the lit one, the foot
        dissolving into mist; 皴 strokes fall from the ridge"""
        for y in range(top, base):
            f = (y - top) / max(1, base - top)
            w = half * (f ** 0.8) + 1.5 * math.sin(y * 0.9 + cx)
            for x in range(int(cx - w), int(cx + w) + 1):
                fade = f > 0.72 and ((x * 3 + y * 5) % 7) < (f - 0.72) * 26
                if fade:
                    continue
                left = x < cx - w * 0.15
                put(x, y, 2 if (left and shade and (x + y) % 2 == 0) else (1 if (x + y) % 2 == 0 or left else 0))
            if f < 0.8:
                put(cx - w, y, 3)
                put(cx + w, y, 3)
        if strokes:
            for k in range(-2, 3):
                x0 = cx + k * half * 0.22
                y0 = top + 4 + abs(k) * (base - top) * 0.12
                line(x0, y0, x0 + 1.5, y0 + (base - top) * 0.28)

    # far range and the sun
    for x in range(2, N - 2):
        y = 24 + 3 * math.sin(x / 9.0) + 2 * math.sin(x / 4.0 + 1)
        put(x, y, 3)
        for yy in range(int(y) + 1, 32):
            if (x + yy) % 2 == 0 and ((x * 3 + yy * 5) % 7) > (yy - y) * 0.9:
                put(x, yy, 1)
    for y in range(6, 18):
        for x in range(56, 68):
            d = math.hypot(x - 61.5, y - 11.5)
            if d < 4.3:
                put(x, y, 2)
            elif d < 5.2:
                put(x, y, 3)
    for cx, cy in ((22, 9), (40, 14)):                      # two birds
        line(cx - 3, cy, cx, cy + 1); line(cx, cy + 1, cx + 3, cy - 1)
    peak(54, 26, 17, 58, shade=False)                       # the right shoulder (behind)
    peak(27, 8, 21, 60)                                     # the main peak
    # pavilion on the right shoulder
    px, py = 55, 33
    line(px - 7, py, px, py - 5); line(px, py - 5, px + 7, py)          # roof
    line(px - 8, py, px + 8, py)
    line(px - 9, py + 1, px - 7, py - 1); line(px + 9, py + 1, px + 7, py - 1)   # upturned eaves
    for dx in (-5, 0, 5):
        line(px + dx, py + 1, px + dx, py + 7)
    line(px - 6, py + 7, px + 6, py + 7)
    for x in range(px - 4, px + 5):
        for y in range(py + 1, py + 7):
            if g[y][x] != 3:
                g[y][x] = 0
    # water: a few long strokes, the bank
    for y, x0, x1 in ((63, 30, 74), (67, 22, 60), (71, 36, 76), (75, 28, 58)):
        for x in range(x0, x1):
            if (x // 5 + y) % 3:
                put(x, y + (1 if (x // 7) % 2 else 0), 3 if (x + y) % 2 else 1)
    # fishing boat
    bx, by = 56, 68
    line(bx - 7, by, bx + 7, by); line(bx - 6, by + 1, bx + 6, by + 1, 2); line(bx - 4, by + 2, bx + 4, by + 2)
    line(bx - 1, by - 1, bx - 1, by - 4); line(bx - 3, by - 4, bx + 1, by - 4)       # fisherman: hat
    line(bx + 1, by - 3, bx + 9, by - 8)                                             # rod
    line(bx + 9, by - 8, bx + 9, by - 2, 1)
    # foreground rock and pine
    for y in range(60, 78):
        for x in range(2, 26):
            if y > 62 + (x - 2) * 0.5 + 2 * math.sin(x):
                put(x, y, 2 if (x + y) % 2 == 0 and x < 12 else (1 if (x + y) % 2 == 0 else 0))
        put(2 + (y - 62) * 1.9 if y > 62 else 2, y, 3)
    tx = 13
    line(tx, 66, tx - 1, 50); line(tx + 1, 66, tx, 50); line(tx - 1, 50, tx + 3, 40)
    line(tx + 3, 40, tx + 2, 34)
    for cx, cy, r in ((tx + 9, 46, 7), (tx - 6, 42, 6), (tx + 6, 36, 6), (tx - 2, 31, 5)):   # needle clusters
        line(tx + (1 if cx > tx else -1), cy + 3, cx, cy)
        for a in range(-r, r + 1):
            hgt = int(3 * (1 - (a / r) ** 2) + 1)
            for h in range(hgt):
                put(cx + a, cy - h, 2 if (a + h) % 2 == 0 else 1)
            put(cx + a, cy - hgt, 3)
            if a % 2 == 0:
                put(cx + a, cy + 1, 3)
    # scroll frame: the same double rule as the portrait frame
    for k in range(N):
        for a in (0, 2):
            g[a][k] = g[N - 1 - a][k] = g[k][a] = g[k][N - 1 - a] = 3
    for k in range(1, N - 1):
        g[1][k] = g[N - 2][k] = g[k][1] = g[k][N - 2] = 0
    return g


def tile(img, tx, ty):
    d = []
    for y in range(8):
        lo = hi = 0
        for x in range(8):
            v = img[ty * 8 + y][tx * 8 + x]
            lo |= (v & 1) << (7 - x)
            hi |= ((v >> 1) & 1) << (7 - x)
        d += [lo, hi]
    return d


def main():
    g = paint()
    tiles = [b for ty in range(10) for tx in range(10) for b in tile(g, tx, ty)]
    fret = [[2 if c == "#" else 0 for c in r] for r in FRET]
    ft = tile(fret, 0, 0)

    def rgb555(c):
        r, gg, b = (v >> 3 for v in c)
        return r | (gg << 5) | (b << 10)
    h = ["/* GENERATED by tools/gen_scroll.py - do not edit */", "#ifndef SCROLL_DATA_H", "#define SCROLL_DATA_H", "#include <stdint.h>",
         f"#define SCROLL_BANK {BANK}u", "#define SCROLL_TILES 100u   /* 10 x 10, row-major */",
         "extern const uint8_t scroll_tiles[];", "extern const uint8_t scroll_fret[16];", "extern const uint16_t scroll_pal[4];", "#endif"]
    c = [f"#pragma bank {BANK}   /* dialogue scroll panel: read only through the rom.c gateway */", '#include "scroll_data.h"',
         "const uint8_t scroll_tiles[] = {" + ", ".join(map(str, tiles)) + "};",
         "const uint8_t scroll_fret[16] = {" + ", ".join(map(str, ft)) + "};",
         "const uint16_t scroll_pal[4] = {" + ", ".join(str(rgb555(p)) for p in PAL) + "};"]
    (ROOT / "src/gen/scroll_data.h").write_text("\n".join(h) + "\n", encoding="utf-8")
    (ROOT / "src/gen/scroll_data.c").write_text("\n".join(c) + "\n", encoding="utf-8")
    S = 4
    rows = []
    for r in g:
        ln = bytearray()
        for v in r:
            ln += bytes(PAL[v]) * S
        rows += [ln] * S
    (ROOT / "build").mkdir(exist_ok=True)
    write_png(ROOT / "build/art_scroll.png", N * S, N * S, rows)
    print("gen_scroll: 100 tiles")


if __name__ == "__main__":
    main()
