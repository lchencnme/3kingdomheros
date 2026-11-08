#!/usr/bin/env python3
"""Fullscreen battle placeholder assets (design doc 12.4 layered composition).

Programmer art only: it exercises the real pipeline (background plate dictionary + per-pose actor
tiles with transparency masks + runtime compositing) until the 绣像 actors are drawn.

Outputs src/gen/fs_data.{h,c}:
  - plate: <=96 unique 8x8 tiles, 20x13 tile map (scene rows 3..15), drawn with paper(0)/ink(3) only
  - actors: 3 bodies (foot, bow, horse) x 4 poses (idle, windup, strike, hit) x 2 facings,
    each 6x8 tiles: 16B 2bpp + 8B mask per tile (mask bit = opaque pixel)
  - palettes for fullscreen mode
"""
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
AW, AH = 48, 64          # actor box: 6 x 8 tiles
PW, PH = 160, 104        # scene plate: 20 x 13 tiles

PAPER, INK = (232, 220, 184), (48, 40, 32)
PALS = [
    [PAPER, (200, 188, 156), (120, 104, 84), INK],     # 0 plate (only 0/3 used) and HUD text
    [PAPER, (48, 80, 160), (224, 176, 128), INK],      # 1 player actor: blue, skin
    [PAPER, (160, 48, 32), (224, 176, 128), INK],      # 2 enemy actor: red, skin
    [PAPER, (176, 64, 48), (138, 128, 112), INK],      # 3 spare
]


def rgb555(c):
    r, g, b = (v >> 3 for v in c)
    return r | (g << 5) | (b << 10)


class Canvas:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = [[0] * w for _ in range(h)]

    def put(self, x, y, c):
        x, y = int(round(x)), int(round(y))
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y][x] = c

    def line(self, x0, y0, x1, y1, c, width=1):
        n = int(max(abs(x1 - x0), abs(y1 - y0)) * 2) + 1
        for i in range(n + 1):
            t = i / n
            x, y = x0 + (x1 - x0) * t, y0 + (y1 - y0) * t
            for dx in range(width):
                for dy in range(width):
                    self.put(x + dx - width // 2, y + dy - width // 2, c)

    def disc(self, cx, cy, r, fill, outline=3):
        for y in range(int(cy - r - 1), int(cy + r + 2)):
            for x in range(int(cx - r - 1), int(cx + r + 2)):
                d = math.hypot(x - cx, y - cy)
                if d <= r - 0.5:
                    self.put(x, y, fill)
                elif d <= r + 0.5:
                    self.put(x, y, outline)

    def rect(self, x0, y0, x1, y1, fill, outline=3):
        for y in range(int(y0), int(y1) + 1):
            for x in range(int(x0), int(x1) + 1):
                edge = x in (int(x0), int(x1)) or y in (int(y0), int(y1))
                self.put(x, y, outline if edge else fill)

    def ellipse(self, cx, cy, rx, ry, fill, outline=3):
        for y in range(int(cy - ry - 1), int(cy + ry + 2)):
            for x in range(int(cx - rx - 1), int(cx + rx + 2)):
                d = ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2
                if d <= 0.85:
                    self.put(x, y, fill)
                elif d <= 1.15:
                    self.put(x, y, outline)

    def mirrored(self):
        m = Canvas(self.w, self.h)
        m.px = [list(reversed(r)) for r in self.px]
        return m


# ---------------------------------------------------------------- actors (drawn facing LEFT)
def foot(c, pose, shield=True):
    lean = {"idle": 0, "windup": 2, "strike": -4, "hit": 5}[pose]
    drop = 2 if pose == "hit" else 0
    bx = 24 + lean
    c.rect(bx - 7, 26 + drop, bx + 7, 44 + drop, 1)                 # torso (team colour)
    c.line(bx - 4, 45 + drop, bx - 6, 60, 3, 2)                      # legs
    c.line(bx + 4, 45 + drop, bx + 6, 60, 3, 2)
    c.disc(bx, 18 + drop, 6, 2)                                      # head (skin)
    c.line(bx - 7, 12 + drop, bx + 7, 12 + drop, 3, 2)               # helmet brim
    c.put(bx - 3, 18 + drop, 3)                                      # eye (facing left)
    if pose == "windup":
        c.line(bx + 6, 28, bx + 14, 6, 3, 2)                         # weapon raised behind
    elif pose == "strike":
        c.line(bx - 6, 30, bx - 22, 26, 3, 2)                        # weapon thrust forward
    elif pose == "hit":
        c.line(bx + 4, 30, bx + 14, 40, 3, 2)
    else:
        c.line(bx - 8, 36, bx - 12, 14, 3, 2)                        # weapon upright
    if shield:
        sx = bx - 9 if pose != "strike" else bx - 4
        c.rect(sx - 4, 28 + drop, sx + 3, 42 + drop, 1)


def bow(c, pose):
    lean = {"idle": 0, "windup": 1, "strike": -2, "hit": 5}[pose]
    drop = 2 if pose == "hit" else 0
    bx = 26 + lean
    c.rect(bx - 6, 26 + drop, bx + 6, 44 + drop, 1)
    c.line(bx - 3, 45 + drop, bx - 5, 60, 3, 2)
    c.line(bx + 3, 45 + drop, bx + 5, 60, 3, 2)
    c.disc(bx, 18 + drop, 6, 2)
    c.put(bx - 3, 18 + drop, 3)
    c.line(bx - 6, 10 + drop, bx + 6, 10 + drop, 3, 1)
    # bow arc in front
    ax = bx - 12
    for i in range(-12, 13):
        c.put(ax - 4 * math.cos(i / 12 * math.pi / 2), 32 + drop + i, 3)
    if pose == "windup":
        c.line(ax, 20, bx - 2, 32, 3, 1); c.line(ax, 44, bx - 2, 32, 3, 1)   # string drawn
        c.line(bx - 2, 32, ax - 10, 32, 3, 1)                                 # arrow nocked
    elif pose == "strike":
        c.line(ax, 20, ax, 44, 3, 1)
        c.line(ax - 8, 32, 2, 32, 3, 1)                                       # arrow flying
    else:
        c.line(ax, 20, ax, 44, 3, 1)


def horse(c, pose):
    lean = {"idle": 0, "windup": 2, "strike": -5, "hit": 5}[pose]
    drop = 2 if pose == "hit" else 0
    hx = 26 + lean
    c.ellipse(hx, 46 + drop, 15, 7, 1)                               # horse body (team colour)
    c.line(hx - 13, 44 + drop, hx - 19, 34 + drop, 3, 3)             # neck
    c.ellipse(hx - 20, 32 + drop, 4, 3, 1)                           # head
    for lx in (hx - 10, hx - 4, hx + 6, hx + 11):                    # legs
        c.line(lx, 52 + drop, lx + (2 if pose == "strike" else 0), 63, 3, 1)
    c.rect(hx - 4, 26 + drop, hx + 5, 40 + drop, 1)                  # rider torso
    c.disc(hx, 19 + drop, 5, 2)
    c.put(hx - 2, 19 + drop, 3)
    if pose == "windup":
        c.line(hx + 4, 28, hx + 16, 4, 3, 2)
    elif pose == "strike":
        c.line(hx - 4, 30, hx - 24, 30, 3, 2)
    else:
        c.line(hx - 6, 40, hx - 14, 6, 3, 2)


BODIES = [("foot", lambda c, p: foot(c, p)), ("bow", bow), ("horse", horse)]
POSES = ["idle", "windup", "strike", "hit"]


def tiles_of(cv, tw, th):
    """returns list of (2bpp 16 bytes, mask 8 bytes) row-major"""
    out = []
    for ty in range(th):
        for tx in range(tw):
            data, mask = [], []
            for y in range(8):
                lo = hi = m = 0
                for x in range(8):
                    v = cv.px[ty * 8 + y][tx * 8 + x]
                    lo |= (v & 1) << (7 - x)
                    hi |= ((v >> 1) & 1) << (7 - x)
                    m |= (1 if v else 0) << (7 - x)
                data += [lo, hi]
                mask.append(m)
            out.append((data, mask))
    return out


# ---------------------------------------------------------------- plate: ink-line landscape on paper
def plate():
    c = Canvas(PW, PH)
    for x in range(PW):                                    # distant mountains
        y = 22 + 8 * math.sin(x / 17.0) + 4 * math.sin(x / 7.3)
        c.put(x, y, 3)
        if x % 9 == 0:
            c.line(x, y, x + 3, y + 5, 3)
    for x in range(0, PW, 2):                              # ground line + hatching
        c.put(x, 84, 3)
        if x % 12 == 0:
            c.line(x, 88, x + 5, 92, 3)
    for x in (12, 140):                                    # two pines
        c.line(x, 84, x, 52, 3)
        for k in range(5):
            c.line(x - 7 + k, 56 + k * 5, x + 7 - k, 56 + k * 5, 3)
    tiles, index, tmap = [[0] * 16], {tuple([0] * 16): 0}, []   # tile 0 = blank paper (HUD margins)
    for ty in range(PH // 8):
        for tx in range(PW // 8):
            data, _ = tiles_of_region(c, tx, ty)
            key = tuple(data)
            if key not in index:
                index[key] = len(tiles)
                tiles.append(data)
            tmap.append(index[key])
    if len(tiles) > 96:
        raise SystemExit(f"plate uses {len(tiles)} unique tiles > 96 (design doc 12.4 budget)")
    return tiles, tmap


def tiles_of_region(c, tx, ty):
    data = []
    for y in range(8):
        lo = hi = 0
        for x in range(8):
            v = c.px[ty * 8 + y][tx * 8 + x]
            lo |= (v & 1) << (7 - x)
            hi |= ((v >> 1) & 1) << (7 - x)
        data += [lo, hi]
    return data, None


def main():
    ptiles, pmap = plate()
    poses = []                       # list of 1152-byte blobs: 48 x 16B tiles then 48 x 8B masks
    for name, fn in BODIES:
        for facing in ("L", "R"):
            for pose in POSES:
                cv = Canvas(AW, AH)
                fn(cv, pose)
                if facing == "R":
                    cv = cv.mirrored()
                tl = tiles_of(cv, AW // 8, AH // 8)
                poses.append([b for d, _ in tl for b in d] + [b for _, m in tl for b in m])
    POSE_BYTES = 48 * 24
    banks, cur, used = [], 6, 0
    for _ in poses:                  # pack poses into asset banks 6.. without crossing 16 KiB
        if used + POSE_BYTES > 16384 - 64:
            cur += 1
            used = 0
        banks.append(cur)
        used += POSE_BYTES

    def arr(name, vals, typ="uint8_t", per=24, static=False):
        lines = [f"{'static ' if static else ''}const {typ} {name}[] = {{"]
        for i in range(0, len(vals), per):
            lines.append("    " + ", ".join(map(str, vals[i:i + per])) + ",")
        lines.append("};")
        return lines

    h = ["/* GENERATED by tools/gen_actors.py - placeholder fullscreen art, do not edit */",
         "#ifndef FS_DATA_H", "#define FS_DATA_H", "#include <stdint.h>",
         "#define FS_PLATE_BANK 2u",
         f"#define FS_PLATE_TILES {len(ptiles)}u", "#define FS_PLATE_W 20u", "#define FS_PLATE_H 13u",
         "#define FS_ACTOR_W 6u", "#define FS_ACTOR_H 8u", "#define FS_ACTOR_TILES 48u",
         "#define FS_POSE_BYTES 1152u   /* 48 x 16B tiles, then 48 x 8B masks */",
         "#define FS_BODY_FOOT 0u", "#define FS_BODY_BOW 1u", "#define FS_BODY_HORSE 2u",
         "#define FS_POSE_IDLE 0u", "#define FS_POSE_WINDUP 1u", "#define FS_POSE_STRIKE 2u", "#define FS_POSE_HIT 3u",
         f"#define FS_POSES {len(poses)}u",
         "/* pose index = (body * 2 + facing) * 4 + pose; facing 0 = faces left, 1 = faces right */",
         "extern const uint8_t fs_plate_tiles[];", "extern const uint8_t fs_plate_map[];",
         "extern const uint8_t fs_pose_bank[FS_POSES];", "extern const uint8_t *const fs_pose_ptr[FS_POSES];",
         "extern const uint16_t fs_pals[16];", "#endif"]
    out = {}
    for i, blob in enumerate(poses):
        out.setdefault(banks[i], []).extend(arr(f"fs_pose_{i}", blob))
    for b, lines in out.items():
        (ROOT / f"src/gen/fs_poses_b{b}.c").write_text("\n".join([f"#pragma bank {b}", '#include "fs_data.h"'] + lines) + "\n", encoding="utf-8")
    plate_c = ["#pragma bank 2   /* asset bank: read only through the rom.c gateway */", '#include "fs_data.h"']
    plate_c += arr("fs_plate_tiles", [b for t_ in ptiles for b in t_])
    plate_c += arr("fs_plate_map", pmap, per=20)
    (ROOT / "src/gen/fs_plate.c").write_text("\n".join(plate_c) + "\n", encoding="utf-8")
    ci = ['#include "fs_data.h"']
    ci += [f"extern const uint8_t fs_pose_{i}[];" for i in range(len(poses))]
    ci.append("const uint8_t fs_pose_bank[FS_POSES] = {" + ", ".join(map(str, banks)) + "};")
    ci.append("const uint8_t *const fs_pose_ptr[FS_POSES] = {" + ", ".join(f"fs_pose_{i}" for i in range(len(poses))) + "};")
    ci += arr("fs_pals", [rgb555(col) for p_ in PALS for col in p_], "uint16_t", 4)
    (ROOT / "src/gen/fs_data.h").write_text("\n".join(h) + "\n", encoding="utf-8")
    (ROOT / "src/gen/fs_index.c").write_text("\n".join(ci) + "\n", encoding="utf-8")
    old = ROOT / "src/gen/fs_data.c"
    if old.exists():
        old.unlink()
    print(f"gen_actors: plate {len(ptiles)} tiles, {len(poses)} actor poses in banks {sorted(set(banks))}")


if __name__ == "__main__":
    main()
