#!/usr/bin/env python3
"""Fullscreen battle placeholder assets (design doc 12.4 layered composition).

绣像 woodcut style: flat team-colour / skin regions, one-pixel ink contours, ink hatching on armour, line
weapons; the plate is an ink-line landscape on paper. Same pipeline (plate dictionary + per-pose actor tiles
with transparency masks + runtime compositing).

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


# ---------------------------------------------------------------- actors (drawn facing LEFT), 绣像 woodcut style:
# flat team-colour / skin regions with a one-pixel ink contour, ink hatching on armour and robe, line weapons.
def poly(c, pts, fill):
    ys = [p[1] for p in pts]
    for y in range(max(0, int(min(ys))), min(c.h, int(max(ys)) + 1)):
        xs = []
        for (x0, y0), (x1, y1) in zip(pts, pts[1:] + pts[:1]):
            if (y0 <= y + 0.5 < y1) or (y1 <= y + 0.5 < y0):
                xs.append(x0 + (y + 0.5 - y0) * (x1 - x0) / (y1 - y0))
        xs.sort()
        for a, b in zip(xs[::2], xs[1::2]):
            for x in range(int(round(a)), int(round(b))):
                c.put(x, y, fill)


def outline(c):
    """ink every opaque pixel that touches transparency or a different colour region (woodcut contour)"""
    src = [r[:] for r in c.px]
    for y in range(c.h):
        for x in range(c.w):
            v = src[y][x]
            if v in (0, 3):
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                n = src[ny][nx] if 0 <= nx < c.w and 0 <= ny < c.h else 0
                if n == 0 or (n != v and n != 3 and v > n):
                    c.px[y][x] = 3
                    break


def hatch(c, x0, y0, x1, y1, colour=1, step=3, slope=1):
    for y in range(int(y0), int(y1) + 1):
        for x in range(int(x0), int(x1) + 1):
            if 0 <= x < c.w and 0 <= y < c.h and c.px[y][x] == colour and (x + slope * y) % step == 0:
                c.px[y][x] = 3


def warrior(c, bx, by, pose, helmet=True):
    """standing figure: plumed helmet, face, lamellar coat, war skirt, boots (facing left)"""
    poly(c, [(bx - 6, by + 30), (bx + 7, by + 30), (bx + 9, by + 40), (bx - 9, by + 40)], 1)   # war skirt
    poly(c, [(bx - 7, by + 12), (bx + 7, by + 12), (bx + 6, by + 31), (bx - 6, by + 31)], 1)   # coat
    poly(c, [(bx - 5, by + 40), (bx - 1, by + 40), (bx - 3, by + 52), (bx - 7, by + 52)], 1)   # legs
    poly(c, [(bx + 1, by + 40), (bx + 5, by + 40), (bx + 6, by + 52), (bx + 2, by + 52)], 1)
    c.rect(bx - 9, by + 51, bx - 3, by + 54, 3, 3)                                            # boots
    c.rect(bx + 2, by + 51, bx + 8, by + 54, 3, 3)
    c.ellipse(bx, by + 5, 5, 6, 2, 2)                                                          # face
    if helmet:
        poly(c, [(bx - 6, by + 3), (bx - 5, by - 3), (bx, by - 6), (bx + 5, by - 3), (bx + 6, by + 3)], 1)
        c.line(bx, by - 6, bx + 4, by - 12, 3, 1); c.line(bx + 1, by - 6, bx + 6, by - 11, 3, 1)  # plume
    outline(c)
    hatch(c, bx - 7, by + 13, bx + 7, by + 30, 1, 3, 1)                                         # lamellae
    hatch(c, bx - 9, by + 31, bx + 9, by + 40, 1, 2, 0)                                         # skirt folds
    c.line(bx - 7, by + 21, bx + 7, by + 21, 3, 1)                                              # belt
    c.put(bx - 3, by + 4, 3); c.line(bx - 4, by + 2, bx - 2, by + 2, 3, 1)                      # eye, brow
    c.line(bx - 4, by + 8, bx - 2, by + 8, 3, 1)                                                # mouth


def foot(c, pose, shield=True):
    lean = {"idle": 0, "windup": 2, "strike": -4, "hit": 5}[pose]
    drop = 2 if pose == "hit" else 0
    bx, by = 26 + lean, 8 + drop
    warrior(c, bx, by, pose)
    hx, hy = bx - 7, by + 18                                     # hand
    if pose == "windup":
        tip = (bx + 12, by - 8)
    elif pose == "strike":
        tip = (bx - 24, by + 14)
    elif pose == "hit":
        tip = (bx + 14, by + 34)
    else:
        tip = (bx - 12, by - 6)
    c.line(hx + (hx - tip[0]) * 0.4, hy + (hy - tip[1]) * 0.4, tip[0], tip[1], 3, 2)         # halberd shaft
    tx, ty = tip
    poly(c, [(tx - 2, ty - 3), (tx + 3, ty - 1), (tx + 1, ty + 4), (tx - 3, ty + 2)], 2)      # crescent blade
    outline(c)
    if shield and pose != "strike":
        c.ellipse(bx - 9, by + 22, 4, 7, 1)
        c.put(bx - 9, by + 22, 3)


def bow(c, pose):
    lean = {"idle": 0, "windup": 1, "strike": -2, "hit": 5}[pose]
    drop = 2 if pose == "hit" else 0
    bx, by = 28 + lean, 8 + drop
    warrior(c, bx, by, pose, helmet=False)
    poly(c, [(bx - 6, by + 2), (bx - 5, by - 3), (bx + 5, by - 3), (bx + 6, by + 2)], 3)       # head cloth
    c.rect(bx + 6, by + 12, bx + 9, by + 26, 2, 3)                                             # quiver
    for k in range(3):
        c.line(bx + 7 + k, by + 12, bx + 8 + k, by + 8, 3, 1)
    ax, ay = bx - 13, by + 20
    for i in range(-13, 14):                                                                    # recurved bow
        c.put(ax - 4 * math.cos(i / 13 * math.pi / 2) + (1 if abs(i) > 11 else 0), ay + i, 3)
    if pose == "windup":
        c.line(ax, ay - 13, bx - 2, ay, 3, 1); c.line(ax, ay + 13, bx - 2, ay, 3, 1)
        c.line(bx - 2, ay, ax - 10, ay, 3, 1)
    elif pose == "strike":
        c.line(ax, ay - 13, ax, ay + 13, 3, 1)
        c.line(ax - 8, ay, 1, ay, 3, 1); c.line(1, ay, 4, ay - 2, 3, 1); c.line(1, ay, 4, ay + 2, 3, 1)
    else:
        c.line(ax, ay - 13, ax, ay + 13, 3, 1)


def horse(c, pose):
    lean = {"idle": 0, "windup": 2, "strike": -5, "hit": 5}[pose]
    drop = 2 if pose == "hit" else 0
    hx = 26 + lean
    poly(c, [(hx - 15, 40 + drop), (hx + 14, 40 + drop), (hx + 16, 50 + drop), (hx - 13, 52 + drop)], 1)   # body
    poly(c, [(hx - 15, 42 + drop), (hx - 21, 30 + drop), (hx - 17, 28 + drop), (hx - 10, 40 + drop)], 1)   # neck
    poly(c, [(hx - 22, 27 + drop), (hx - 15, 26 + drop), (hx - 16, 33 + drop), (hx - 24, 32 + drop)], 1)   # head
    for lx, d in ((hx - 11, -2), (hx - 6, 1), (hx + 8, -1), (hx + 13, 2)):                                 # legs
        k = d if pose == "strike" else 0
        c.line(lx, 51 + drop, lx + k, 63, 3, 2)
    c.line(hx + 15, 42 + drop, hx + 21, 54 + drop, 3, 1); c.line(hx + 16, 42 + drop, hx + 22, 52 + drop, 3, 1)   # tail
    poly(c, [(hx - 4, 37 + drop), (hx + 8, 37 + drop), (hx + 9, 42 + drop), (hx - 5, 42 + drop)], 2)       # saddle cloth
    poly(c, [(hx - 4, 22 + drop), (hx + 5, 22 + drop), (hx + 5, 38 + drop), (hx - 4, 38 + drop)], 1)       # rider
    c.ellipse(hx, 16 + drop, 4, 5, 2, 2)
    poly(c, [(hx - 5, 14 + drop), (hx - 4, 9 + drop), (hx, 7 + drop), (hx + 4, 9 + drop), (hx + 5, 14 + drop)], 1)
    outline(c)
    hatch(c, hx - 4, 23 + drop, hx + 5, 37 + drop, 1, 3, 1)
    for k in range(-18, -12, 2):                                                                         # mane
        c.line(hx + k, 30 + drop + (k + 18) // 2, hx + k + 2, 27 + drop + (k + 18) // 2, 3, 1)
    c.put(hx - 21, 29 + drop, 3)                                                                         # horse eye
    c.put(hx - 2, 15 + drop, 3)
    if pose == "windup":
        c.line(hx + 3, 26, hx + 16, 2, 3, 2)
    elif pose == "strike":
        c.line(hx - 3, 28, hx - 26, 28, 3, 2); c.line(hx - 26, 28, hx - 22, 25, 3, 1)
    else:
        c.line(hx - 5, 36, hx - 13, 4, 3, 2); c.line(hx - 13, 4, hx - 11, 9, 3, 1)


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
         "#define FS_PLATE_BANK 15u",
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
    plate_c = ["#pragma bank 15  /* asset bank: read only through the rom.c gateway */", '#include "fs_data.h"']
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
