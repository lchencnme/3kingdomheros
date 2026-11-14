#!/usr/bin/env python3
"""Generate TECH01 placeholder assets + battle data (design doc 附录E.2 item 6, 12.2).

Outputs:
  src/gen/tech01_data.h / .c   tiles (2bpp), palettes (RGB555), metatile table, terrain map, units
  content/battles/BT_TECH01.json  battle file in schema v2 (附录C.2)

All art here is programmer placeholder art (not the final 绣像 assets).
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
W, H = 32, 24

# ---------------- palettes (RGB888 -> RGB555) ----------------
PAPER, INK = (232, 220, 184), (48, 40, 32)
# 设色 map: woodcut ink lines over classical mineral pigments, one pigment family per terrain group
# (赭石 ochre earth, 石绿 malachite hills and woods, 花青 indigo water, 青灰 grey brick and tile)
BG_PALS = [
    [PAPER, (218, 200, 152), (170, 128, 80), INK],    # 0 earth: plain/road        赭石
    [PAPER, (156, 192, 144), (62, 116, 96), INK],     # 1 hills and woods          石绿
    [PAPER, (176, 208, 212), (64, 108, 152), INK],    # 2 water: shallow/deep/bridge 花青
    [PAPER, (196, 186, 170), (112, 98, 92), INK],     # 3 buildings: village/fort/wall 青灰
]


def tint(c):  # move-range highlight: shift toward stone-blue
    return tuple((v * 6 + t * 2) // 8 for v, t in zip(c, (96, 150, 210)))


# design doc 12.2 (v2): 4 player unit, 5 enemy unit, 6 shared move-range tint, 7 acted unit (gray)
BG_PALS += [
    [PAPER, (160, 188, 228), (44, 84, 156), INK],                     # 4 player unit: 花青 indigo
    [PAPER, (240, 168, 144), (176, 52, 36), INK],                     # 5 enemy unit: 朱砂 vermilion
    [tint(PAPER), tint((206, 196, 160)), tint((120, 120, 110)), INK], # 6 highlight (any terrain)
    [PAPER, (186, 186, 186), (112, 112, 112), INK],                   # 7 acted unit: grey
]
OBJ_PALS = [
    [PAPER, (48, 80, 160), (224, 176, 128), INK],      # 0 player
    [PAPER, (160, 48, 32), (224, 176, 128), INK],      # 1 enemy
    [PAPER, (248, 248, 240), (200, 40, 32), INK],      # 2 cursor
    [PAPER, (120, 120, 128), (180, 170, 160), INK],    # 3 acted
]


def remap_table():
    """design doc 12.2: for every (terrain palette, unit palette) pair, map the terrain colours under a unit
    to the unit palette: paper -> paper, ink -> ink, others -> nearest RGB (ties: lower index). Packed
    per pair as one byte: bits 0-2 = low plane set for colours 1..3, bits 4-6 = high plane set."""
    out = []
    for src in range(8):
        for dst in range(4, 8):
            m = [0, None, None, 3]
            for c in (1, 2):
                col = BG_PALS[src][c]
                d = [sum((a - b) ** 2 for a, b in zip(col, BG_PALS[dst][k])) for k in range(4)]
                m[c] = min(range(4), key=lambda k: (d[k], k))
            lo = sum(((m[c] & 1) << (c - 1)) for c in (1, 2, 3))
            hi = sum((((m[c] >> 1) & 1) << (c - 1)) for c in (1, 2, 3))
            out.append(lo | (hi << 4))
    # unit art check: the unit tiles use colours 1..3 of their own palette directly, so the three
    # visible colours map one-to-one by construction; report the remap for review
    return out


def rgb555(c):
    r, g, b = (v >> 3 for v in c)
    return r | (g << 5) | (b << 10)


# ---------------- 16x16 metatile art ('.'=0 ':'=1 '+'=2 '#'=3) ----------------
def grid16(rows):
    assert len(rows) == 16 and all(len(r) == 16 for r in rows), rows
    return rows


ART = {
    0: grid16([  # plain
        "................", "................", "...:............", "..:.:.......:...",
        "...........:.:..", "................", "................", "......:.........",
        ".....:.:........", "................", ".............:..", "..:.........:.:.",
        ".:.:............", "................", "........:.......", ".......:.:......"]),
    1: grid16([  # road
        "....+......+....", "...+.........+..", "................", "....+.......+...",
        "...+.........+..", "................", "....+......+....", "...+........+...",
        "................", "....+.......+...", "...+.........+..", "................",
        "....+......+....", "...+.........+..", "................", "....+.......+..."]),
    2: grid16([  # forest
        "....##......##..", "...#::#....#::#.", "..#:::+#..#:::+#", "..#::++#..#::++#",
        "...#++#....#++#.", "....##......##..", ".....#.......#..", "................",
        "..##......##....", ".#::#....#::#...", "#:::+#..#:::+#..", "#::++#..#::++#..",
        ".#++#....#++#...", "..##......##....", "...#.......#....", "................"]),
    5: grid16([  # hill
        "................", "......##........", ".....#::#.......", "....#:::+#......",
        "...#::::++#.....", "..#:::+:+++#....", ".#::::++++++#...", "#############...",
        "................", "...........#....", "..........#:#...", ".........#::+#..",
        "........#::+++#.", ".......#########", "................", "................"]),
    6: grid16([  # shallow
        "::::::::::::::::", ":::++:::::::::::", "::+::+::::::++::", "::::::::::++::+:",
        "::::::::::::::::", "::::::::++::::::", ":::::::+::+:::::", "::::::::::::::::",
        "::++::::::::::::", ":+::+:::::::++::", "::::::::::++::+:", "::::::::::::::::",
        ":::::++:::::::::", "::::+::+::::::::", "::::::::::::::::", "::::::::::::::::"]),
    7: grid16([  # deep
        "++++++++++++++++", "+++##+++++++++++", "++#++#++++++##++", "++++++++++##++#+",
        "++++++++++++++++", "++++++++##++++++", "+++++++#++#+++++", "++++++++++++++++",
        "++##++++++++++++", "+#++#+++++++##++", "++++++++++##++#+", "++++++++++++++++",
        "+++++##+++++++++", "++++#++#++++++++", "++++++++++++++++", "++++++++++++++++"]),
    9: grid16([  # bridge
        "::::::::::::::::", "################", "#..#...#...#..#.", "#..#...#...#..#.",
        "################", "#...#...#...#...", "#...#...#...#...", "################",
        "#..#...#...#..#.", "#..#...#...#..#.", "################", "#...#...#...#...",
        "#...#...#...#...", "################", "::::::::::::::::", "::::::::::::::::"]),
    3: grid16([  # village
        "................", "......####......", ".....#++++#.....", "....#++++++#....",
        "...##########...", "....#......#....", "....#.##...#....", "....#.##...#....",
        "....########....", "................", "..:.......:.....", ".:.:.....:.:....",
        "................", "...........:....", "..:...:...:.:...", "................"]),
    4: grid16([  # fort
        "#.#.#.#.#.#.#.#.", "################", "#++#++#++#++#++#", "#++#++#++#++#++#",
        "################", "#..............#", "#..####..####..#", "#..#++#..#++#..#",
        "#..####..####..#", "#..............#", "#......##......#", "#.....#++#.....#",
        "#.....#++#.....#", "#.....#++#.....#", "################", "................"]),
    8: grid16([  # wall
        "################", "#+++#+++#+++#+++", "################", "++#+++#+++#+++#+",
        "################", "#+++#+++#+++#+++", "################", "++#+++#+++#+++#+",
        "################", "#+++#+++#+++#+++", "################", "++#+++#+++#+++#+",
        "################", "#+++#+++#+++#+++", "################", "++#+++#+++#+++#+"]),
}
TERRAIN_PAL = {0: 0, 1: 0, 2: 1, 3: 3, 4: 3, 5: 1, 6: 2, 7: 2, 8: 3, 9: 2}

SPR = {
    "player": grid16([
        "..###...........", "..#:::#.........", "..#:::#.........", "..#:::#..##.....",
        "..####..#++#....", "..#.....#++#....", "..#.....####....", "..#....#::::#...",
        "..#...#::::::#..", "..#...#:#::#:#..", "..#...##::::##..", "..#....#::::#...",
        "..#....#:##:#...", "..#....#:#.#:#..", "......##.#..##..", "................"]),
    "enemy": grid16([
        "..#.............", "..##............", "..#:#...........", "..#::#...##.....",
        "..#:::#.#++#....", "..####..#++#....", "..#.....####....", "..#....#::::#...",
        "..#...#::::::#..", "..#...#:#::#:#..", "..#...##::::##..", "..#....#::::#...",
        "..#....#:##:#...", "..#....#:#.#:#..", "......##.#..##..", "................"]),
    "cursor": grid16([
        "####........####", "#++#........#++#", "#+#..........#+#", "##............##",
        "................", "................", "................", "................",
        "................", "................", "................", "................",
        "##............##", "#+#..........#+#", "#++#........#++#", "####........####"]),
}
CH = {".": 0, ":": 1, "+": 2, "#": 3}

# 16x24 BG unit bodies (design doc 12.2, 13: 绣像 woodblock style - heavy ink outline, flat tones, a distinct
# silhouette per troop type so units read apart without colour). '.' = transparent.
# 绣像 woodblock style: '#' = ink outline (colour 3), '+' = mid tone (2), ':' = paper/light tone (1), '.' = clear.
# rows 0..17 figure, 18..21 the shared pedestal (carries the foot mark), 22..23 clear
PED = ["################", "#..............#", "#..............#", "################", "................", "................"]
UNIT24 = {}
UNIT24["sword"] = [   # 刀盾兵: round shield, raised saber
    "................", "......###.......", ".....#:::#...##.", ".....#+#+#..#:#.", ".....#+++#.#:#..",
    "......###.#:#...", "..###.####:#....", ".#+++#::::##....", "#+#+#+#::::#....", "#++#++#::::#....",
    "#+#+#+#::::#....", ".#+++#::::#.....", "..###.#####.....", "......#...#.....", ".....##...##....",
    ".....#.....#....", "....##.....##...", "................"]
UNIT24["spear"] = [   # 长戟兵: tall halberd with crescent blade
    "...........##...", "..........#:##..", "......###..##.#.", ".....#:::#.#....", ".....#+#+#.#....",
    ".....#+++#.#....", "......###..#....", ".....#####.#....", "....#:::::##....", "...#:#:::#:#....",
    "...#:#:::#.#....", "....#:::::##....", ".....#####.#....", ".....#...#.#....", "....##...###....",
    "....#.....#.....", "...##.....##....", "................"]
UNIT24["heavy"] = [   # 重甲步兵: plumed helmet, scale armour, spear
    ".......##.......", "......#::#...#..", ".....######.#:#.", "....#::::::##:#.", "....#+#++#+#.#..",
    ".....#++++#..#..", "...##########...", "..#:#:#:#:#:##..", "..#:#:#:#:#:#...", "..##:#:#:#:##...",
    "...#:#:#:#:#....", "...##########...", "....#:#..#:#....", "....#:#..#:#....", "...##:#..#:##...",
    "...#::#..#::#...", "...####..####...", "................"]
UNIT24["bow"] = [   # 弓弩手: drawn bow, quiver over the shoulder
    "......###.......", ".....#:::#.#....", ".....#+#+##:#...", ".#...#+++#.#:#..", "#.#...###...#...",
    "#..#.#####......", "#...#+++++#.....", "#....#::::#.....", "#....######.....", "#...#:::::#.....",
    "#..#.#:::#......", "#.#..#####......", ".#...#...#......", ".....#...#......", "....##...##.....",
    "....#.....#.....", "...##.....##....", "................"]
UNIT24["horse"] = [   # 轻骑: rider with lance on a running horse
    "#.......###.....", ".#.....#:::#....", "..#....#+#+#....", "...#...#+++#....", "....#...###.....",
    ".....#.#####....", "......#+++++#...", "...##..#:::#.##.", "..#:###########.", ".#:::::::::::#+#",
    "#:#::::::::::#+#", "##.#:::::::::##.", "...#########.#..", "...#.#....#.#...", "...#.#....#.#...",
    "..##.##..##.##..", "................", "................"]
UNIT24["armour"] = [   # 铁骑 / 骁骑: barded horse, helmeted rider, banner pole
    "...##...###.....", "...#+#.#+++#....", "...#+##:#:#:#...", "...##.#######...", "...#..#:#:#:#...",
    "...#..#:#:#:#...", "...#.#########..", "..###:#:#:#:#:#.", ".#:#:#:#:#:#:#:#", "#:#:#:#:#:#:#::#",
    "##:#:#:#:#:#:##.", ".#############..", "...#.#....#.#...", "...#.#....#.#...", "...#.#....#.#...",
    "..##.##..##.##..", "................", "................"]
UNIT24["robe"] = [   # 文士 / 参军 / 军师: scholar cap, wide robe, feather fan
    ".....#####......", ".....#:::#......", "......###.......", ".....#+#+#.###..", ".....#+++##:::#.",
    "......###.#:::#.", "....#######::#..", "...#:::::::##...", "..#:::+:::::#...", "..#::+::+:::#...",
    "..#:+::::+::#...", "..#::::+::::#...", "..#:::::::::#...", "..#::::+::::#...", "..#:::::::::#...",
    "..###########...", "...##.....##....", "................"]
UNIT24["mystic"] = [   # 方士: hooded robe, tall staff with rings
    "...........#....", "......##..#:#...", ".....#::#..#....", "....#:::##.#....", "....#+#+#..#....",
    "....#+++#..#....", ".....###..###...", "....#######+#...", "...#:::::::##...", "...#:+:::+:#....",
    "...#:::+:::#....", "...#:+:::+:#....", "...#:::+:::#....", "...#:+:::+:#....", "...#:::::::#....",
    "...#########....", "....##...##.....", "................"]
UNIT24["flag"] = [   # 鼓吹手 / 辎重队: big square banner on a pole, drum at the hip
    "...#########....", "...#::::::::#...", "...#:+++++:#....", "...#:+::+:#.....", "...#:::::#......",
    "...######.......", "...#..###.......", "...#.#:::#......", "...#.#+#+#......", "...#..###.......",
    "...#.#####......", "...##+++++#.##..", "...#.#++++##::#.", "...#.#####.#::#.", "...#.#...#..##..",
    "....##...##.....", "....#.....#.....", "................"]
UNIT24["ranger"] = [   # 山地兵 / 游侠: wide straw hat, short blade, light kit
    "....########....", "...#::::::::#...", "....###::###....", ".....#+#+#......", ".....#+++#......",
    "......###....#..", ".....#####..#...", "....#::::::##...", "...#:#::::##....", "...#:#::::#.....",
    "....#::::::#....", ".....######.....", ".....#....#.....", "....#......#....", "...##......##...",
    "..#..........#..", "................", "................"]
UNIT24["civilian"] = [   # 民众: head scarf, short jacket, bundle on a carrying pole
    "................", "......###.......", ".....#:::#......", ".....#+#+#......", "#....#+++#....#.",
    "##....###....##.", "#:#..#####...#:#", "################", "....#:::::#.....", "....#:::::#.....",
    "....#:::::#.....", ".....#####......", ".....#...#......", ".....#...#......", "....##...##.....",
    "................", "................", "................"]
for k, v in UNIT24.items():
    assert len(v) == 18 and all(len(r) == 16 for r in v), (k, [len(r) for r in v])
    UNIT24[k] = v + PED
ORDER = ["sword", "spear", "heavy", "bow", "horse", "armour", "robe", "mystic", "flag", "ranger", "civilian"]
CLASS_BODY = {0: "sword", 1: "spear", 2: "heavy", 3: "bow", 4: "bow", 5: "bow", 6: "horse", 7: "armour", 8: "armour",
              9: "robe", 10: "robe", 11: "robe", 12: "flag", 13: "flag", 14: "ranger", 15: "ranger", 16: "mystic", 17: "civilian"}


V05 = ROOT / "art_src" / "v05"
TONE = {(232, 220, 184): ".", (176, 164, 136): ":", (104, 92, 76): "+", (32, 26, 20): "#", (255, 0, 255): "."}


def master(name):
    """a 4-tone master from art_src/v05 (tools/import_art.py) as rows of '.:+#' ('.' also = transparent)"""
    import runpy
    w, h, rows = runpy.run_path(str(ROOT / "tools/gen_portraits.py"), run_name="gen_tech01")["read_png"](V05 / name)
    return ["".join(TONE[tuple(r[x * 3:x * 3 + 3])] for x in range(w)) for r in rows]


# terrain from the delivered art where a 16x16 cell works (textures and compact objects); road, village,
# fort floor and bridge stay hand-cut so they connect and repeat cleanly
for _tid, _name in ((2, "terrain_forest.png"),):
    if (V05 / _name).exists():
        ART[_tid] = grid16(master(_name))


def _mountain():
    """青绿山水 peak cut for a 16 px cell: two overlapping peaks, the shaded flank in the deep pigment, the lit
    flank in the pale one, ink contour and 皴 strokes down the ridge; the foot fades into the paper (mist)"""
    g = [["."] * 16 for _ in range(16)]
    for cx, top, half, base in ((11, 6, 5, 14), (6, 1, 6, 15)):       # back peak first, the main peak over it
        for y in range(top, base):
            w = (y - top) * half // (base - 1 - top) if base - 1 > top else 0
            for x in range(cx - w, cx + w + 1):
                if 0 <= x < 16:
                    fade = y >= base - 2 and (x + y) % 2
                    g[y][x] = "." if fade else ("+" if x < cx else ":")
            for x in (cx - w, cx + w):
                if 0 <= x < 16 and y < base - 2:
                    g[y][x] = "#"
            if y < base - 3 and (y - top) % 2 == 0 and y > top:
                g[y][cx] = "#"                                          # ridge
                if 0 <= cx + 2 < 16 and y > top + 2:
                    g[y][cx + 2 + (y % 3 == 0)] = "+"                   # 皴: texture strokes on the lit flank
        g[top][cx] = "#"
    return ["".join(r) for r in g]


def _road():
    """packed earth: direction-neutral so roads can run either way; pale ochre with darker pebbles"""
    g = []
    for y in range(16):
        r = ""
        for x in range(16):
            v = (x * 7 + y * 13 + (x * y) % 5) % 11
            r += "+" if v == 0 else (":" if (x + y) % 2 == 0 or v < 4 else ".")
        g.append(r)
    return g


ART[5] = grid16(_mountain())
ART[1] = grid16(_road())
ART[3] = grid16([  # village: a tiled-roof house with a door, a fence line in front
    "................", "......####......", ".....#++++#.....", "....#++#+++#....", "...#+++++#++#...",
    "..############..", "...#::::::::#...", "...#:##::##:#...", "...#:##::##:#...", "...#::::##::#...",
    "...#::::##::#...", "...##########...", "................", ".#.#.#....#.#.#.", ".######..######.",
    "................"])
# repeating ground and water are cut by hand in the same woodcut manner: they cover whole fields, so they
# must stay sparse and regular (the delivered textures turn to noise at 16 px)
ART[0] = grid16([  # plain: paper with two small grass tufts
    "................", "................", "...+.+..........", "....+...........", "...:+:..........",
    "................", "................", "................", "................", "...........+.+..",
    "............+...", "...........:+:..", "................", "................", "................",
    "................"])
ART[6] = grid16([  # shallow: light water with sparse ripple arcs
    "::::::::::::::::", "::::::::::::::::", "::++++::::::::::", ":+::::+:::::::::", "::::::::::::::::",
    "::::::::::::::::", "::::::::::++++::", ":::::::::+::::+:", "::::::::::::::::", "::::::::::::::::",
    "::::::::::::::::", ":::++++:::::::::", "::+::::+::::::::", "::::::::::::::::", "::::::::::::::::",
    "::::::::::::::::"])
ART[7] = grid16([  # deep: scalloped wave rows (woodcut 海水纹)
    "::####::::####::", ":#::::#::#::::#:", "#::::::##::::::#", "::::::::::::::::", "####::::####::::",
    "::::#::#::::#::#", ":::::##::::::##:", "::::::::::::::::", "::####::::####::", ":#::::#::#::::#:",
    "#::::::##::::::#", "::::::::::::::::", "####::::####::::", "::::#::#::::#::#", ":::::##::::::##:",
    "::::::::::::::::"])
ART[8] = grid16([  # wall: ink battlements on top, light coursed stone below (woodcut 城墙)
    "##..##..##..##..", "##..##..##..##..", "################", "+::+::::+::::+::", "++++++++++++++++",
    ":::+::::+::::+::", "++++++++++++++++", "+::+::::+::::+::", "++++++++++++++++", ":::+::::+::::+::",
    "++++++++++++++++", "+::+::::+::::+::", "++++++++++++++++", ":::+::::+::::+::", "++++++++++++++++",
    "+::+::::+::::+::"])
ART[4] = grid16([  # fort courtyard: paper with faint paving joints
    "................", "................", "......:.........", "................", "...:............",
    ":::::::.::::::::", "................", "..........:.....", "................", "................",
    "................", "::::.:::::::.:::", "................", "................", "......:.........",
    "................"])
# map units: the delivered chibi soldiers (16x21 + 3 clear rows for the foot mark)
_UNIT_FILES = {"sword": "unit_sword.png", "spear": "unit_spear.png", "heavy": "unit_heavy.png", "bow": "unit_bow.png",
               "horse": "unit_horse.png", "armour": "unit_armour.png", "robe": "unit_strategist.png",
               "mystic": "unit_mystic.png", "flag": "unit_drummer.png", "ranger": "unit_ranger.png",
               "civilian": "unit_civilian.png"}
for _k, _f in _UNIT_FILES.items():
    if (V05 / _f).exists():
        UNIT24[_k] = master(_f)


def tile_bytes(rows8):
    out = []
    for r in rows8:
        lo = hi = 0
        for i, ch in enumerate(r):
            v = CH[ch]
            lo |= (v & 1) << (7 - i)
            hi |= ((v >> 1) & 1) << (7 - i)
        out += [lo, hi]
    return out


def split16(art):
    """returns tiles in order TL, TR, BL, BR"""
    q = []
    for ty in (0, 8):
        for tx in (0, 8):
            q.append(tile_bytes([art[ty + y][tx:tx + 8] for y in range(8)]))
    return q


def split16_obj(art):
    """8x16 OBJ order: left column (top, bottom), right column (top, bottom)"""
    q = []
    for tx in (0, 8):
        for ty in (0, 8):
            q.append(tile_bytes([art[ty + y][tx:tx + 8] for y in range(8)]))
    return q


# ---------------- TECH01 map (original test range, not any historical battlefield) ----------------
def build_map():
    t = [0] * (W * H)

    def put(x, y, v):
        if 0 <= x < W and 0 <= y < H:
            t[y * W + x] = v

    for x in range(W):                       # east-west road
        put(x, 12, 1)
    for y in range(H):                       # river, deep with shallow banks
        put(13, y, 6); put(14, y, 7); put(15, y, 7); put(16, y, 6)
    for x in (13, 14, 15, 16):               # bridge on the road, ford in the south
        put(x, 12, 9)
    for x in (13, 14, 15, 16):
        put(x, 19, 6)
    for y in range(2, 9):                    # northern forest
        for x in range(3, 10):
            if (x * 7 + y * 3) % 5:
                put(x, y, 2)
    for y in range(15, 22):                  # southern hills
        for x in range(5, 11):
            if (x + y) % 3:
                put(x, y, 5)
    put(7, 10, 3); put(8, 14, 3)             # villages
    for x in range(22, 30):                  # walled fort in the east
        put(x, 6, 8); put(x, 18, 8)
    for y in range(6, 19):
        put(22, y, 8); put(29, y, 8)
    put(22, 12, 1)                           # gate on the road
    for y in range(7, 18):
        for x in range(23, 29):
            if t[y * W + x] == 0:
                put(x, y, 0)
    put(26, 9, 4); put(26, 15, 4)            # forts inside
    for y in range(2, 6):                    # north-east forest
        for x in range(18, 21):
            put(x, y, 2)
    return t


UNITS = [  # (name, char_tag, class, faction, x, y)
    ("刘备", "CH_LIUBEI", 0, "PLAYER", 4, 12),
    ("关羽", "CH_GUANYU", 6, "PLAYER", 5, 11),
    ("张飞", "CH_ZHANGFEI", 6, "PLAYER", 5, 13),
    ("刀盾兵", "ANON", 0, "ENEMY", 20, 12),
    ("弓手", "ANON", 3, "ENEMY", 23, 11),
    ("刀盾兵", "ANON", 0, "ENEMY", 24, 13),
    ("弓手", "ANON", 3, "ENEMY", 26, 12),
    ("简雍", "CH_JIANYONG", 9, "PLAYER", 3, 13),   # 文士: strategies (design doc 6.3)
]
CAMP = json.loads((Path(__file__).resolve().parent.parent / "content/camps/CAMP_TECH01.json").read_text(encoding="utf-8"))
# treasures (design doc 7.4): (x, y, item); taken flags are battle-scope VM flags 0x100 + i
TREASURES = [(10, 6, 0x60), (11, 19, 0x50)]         # 的卢 by the north woods, 孙子兵法 below the south hills
# starting inventory (design doc 7.1): (unit index, item id from content/items.json)
# AI groups (design doc 8.2): group 0 attacks; group 1 (the east archer) holds its post until threatened
AI_GROUPS = [{"id": 0, "mode": "AGGRESSIVE", "target": None, "leash": 3},
             {"id": 1, "mode": "HOLD", "target": None, "leash": 3}]
UNIT_AI = {6: 1}           # unit index -> group (default 0)
AI_MODES = {"HOLD": 0, "AGGRESSIVE": 1, "GUARD_POINT": 2, "ESCORT": 3, "ESCAPE": 4}
START_ITEMS = [(3, 0)]     # enemies only: officers bring theirs from the roster (content/roster.json)


def main():
    terrain = build_map()
    tiles = []
    meta = []                      # per terrain: 4 tile ids
    for tid in range(10):
        q = split16(ART[tid])
        base = len(tiles)
        tiles += q
        meta.append([base, base + 1, base + 2, base + 3])
    spr = []
    for k in ("player", "enemy", "cursor"):
        spr += split16_obj(SPR[k])
    unit_blob = []        # per body: 6 tiles (TL TR / ML MR / BL BR) x (16 B data + 8 B mask)
    for body in ORDER:
        art = UNIT24[body]
        assert len(art) == 24 and all(len(r) == 16 for r in art)
        for ty in range(3):
            for tx in range(2):
                rows8 = [art[ty * 8 + y][tx * 8:tx * 8 + 8] for y in range(8)]
                unit_blob += tile_bytes(rows8)
                unit_blob += [sum((1 << (7 - i)) for i, ch in enumerate(r) if ch != ".") for r in rows8]

    h = ["/* GENERATED by tools/gen_tech01.py - placeholder art, do not edit */",
         "#ifndef TECH01_DATA_H\n#define TECH01_DATA_H\n#include <stdint.h>",
         f"#define TECH01_W {W}u\n#define TECH01_H {H}u",
         f"#define TECH01_BG_TILES {len(tiles)}u\n#define TECH01_OBJ_TILES {len(spr)}u",
         f"#define TECH01_UNITS {len(UNITS)}u", "#define TECH01_BANK 11u",
         "#define OBJT_PLAYER 0u\n#define OBJT_ENEMY 4u\n#define OBJT_CURSOR 8u",
         f"#define UNIT_BODIES {len(ORDER)}u   /* " + " ".join(ORDER) + " */",
         "extern const uint8_t tech01_class_body[];   /* class id -> body (gen_tech01 CLASS_BODY) */",
         "#define UNIT_TILE_BYTES 24u   /* 16 B 2bpp + 8 B mask; 6 tiles per body (2 x 3) */",
         "extern const uint8_t tech01_unit_tiles[];",
         "typedef struct { uint8_t cls, faction, x, y, ai, ch; } Tech01Unit;   /* ch: character id, 0 = anonymous */",
         f"#define TECH01_AI_GROUPS {len(AI_GROUPS)}u",
         "extern const uint8_t tech01_shop[];", "extern const uint8_t tech01_remap[8][4];", f"#define TECH01_TREASURES {len(TREASURES)}u",
         "#define TECH01_TREASURE_FLAG0 0x100u", "extern const uint8_t tech01_treasures[][3];", "extern const uint16_t tech01_council[][2];",
         "extern const uint8_t tech01_deploy_forced;   /* bit k = player officer k must deploy */",
         f"#define TECH01_SHOP_N {len(CAMP['shop'])}u", f"#define TECH01_COUNCIL_N {len(CAMP['council'])}u",
         f"#define TECH01_DEPLOY_MAX {CAMP['deploy']['max_count']}u", "extern const uint8_t tech01_ai_groups[];   /* {id, mode, target lo, hi, leash} */",
         "extern const uint8_t tech01_bg_tiles[];", "extern const uint8_t tech01_obj_tiles[];",
         "extern const uint16_t tech01_bg_pals[32];", "extern const uint16_t tech01_obj_pals[16];",
         "extern const uint8_t tech01_meta[10][4];", "extern const uint8_t tech01_terrain_pal[10];",
         "extern const uint8_t tech01_terrain[];", "extern const Tech01Unit tech01_units[];",
         f"#define TECH01_START_ITEMS {len(START_ITEMS)}u", "extern const uint8_t tech01_start_items[][2];",
         "#endif"]
    c = ["#pragma bank 11", "#include \"tech01_data.h\""]   # same bank as mapview (direct reads); others use the gateway
    ci = ["#include \"tech01_data.h\""]                       # unbanked index data

    def arr(name, typ, vals, per=16):
        lines = [f"const {typ} {name}[] = {{"]
        for i in range(0, len(vals), per):
            lines.append("    " + ", ".join(str(v) for v in vals[i:i + per]) + ",")
        lines.append("};")
        return lines

    c += arr("tech01_bg_tiles", "uint8_t", [b for t in tiles for b in t])
    c += arr("tech01_obj_tiles", "uint8_t", [b for t in spr for b in t])
    c += arr("tech01_terrain", "uint8_t", terrain, 32)
    c += arr("tech01_unit_tiles", "uint8_t", unit_blob)
    c += arr("tech01_class_body", "uint8_t", [ORDER.index(CLASS_BODY[k]) for k in range(18)])
    ci += arr("tech01_bg_pals", "uint16_t", [rgb555(col) for p in BG_PALS for col in p], 4)
    ci += arr("tech01_obj_pals", "uint16_t", [rgb555(col) for p in OBJ_PALS for col in p], 4)
    ci.append("const uint8_t tech01_meta[10][4] = {" + ", ".join("{" + ", ".join(map(str, m)) + "}" for m in meta) + "};")
    rt = remap_table()
    ci.append("const uint8_t tech01_remap[8][4] = {" + ", ".join("{" + ", ".join(map(str, rt[i:i + 4])) + "}" for i in range(0, 32, 4))
              + "};   /* [src pal][unit pal - 4] */")
    ci.append("const uint8_t tech01_terrain_pal[10] = {" + ", ".join(str(TERRAIN_PAL[i]) for i in range(10)) + "};")
    fac = {"PLAYER": 0, "ALLY": 1, "ENEMY": 2}
    ci.append("const uint8_t tech01_start_items[][2] = {" + ", ".join(f"{{{u}, {i}}}" for u, i in START_ITEMS) + "};")
    recs = []
    for g in AI_GROUPS:
        tgt = 0xFFFF if g["target"] is None else g["target"]
        recs += [g["id"], AI_MODES[g["mode"]], tgt & 0xFF, tgt >> 8, g["leash"]]
    ci.append("const uint8_t tech01_ai_groups[] = {" + ", ".join(map(str, recs)) + "};")
    # camp node (design doc 2.2, C.4): shop list, council lines, deployment rules
    camp = json.loads((ROOT / "content/camps/CAMP_TECH01.json").read_text(encoding="utf-8"))
    items = {it["id"]: it for it in json.loads((ROOT / "content/items.json").read_text(encoding="utf-8"))["items"]}
    for i in camp["shop"]:
        if i not in items or items[i]["price"] <= 0:
            raise SystemExit(f"gen_tech01: shop item {i} unknown or not for sale")
    spk = {"LIUBEI": 0, "GUANYU": 1, "ZHANGFEI": 2, "NARRATOR": 0xFF}
    players = [u for u in UNITS if u[3] == "PLAYER"]
    forced = 0
    for tag in camp["deploy"]["forced"]:
        forced |= 1 << [u[1] for u in players].index(tag)
    ci.insert(1, '#include "text_data.h"')
    for x, y, it in TREASURES:
        if not (0 <= x < W and 0 <= y < H) or it not in items:
            raise SystemExit(f"gen_tech01: bad treasure {(x, y, it)}")
    ci.append("const uint8_t tech01_treasures[][3] = {" + ", ".join(f"{{{x}, {y}, {it}}}" for x, y, it in TREASURES) + "};")
    ci.append("const uint8_t tech01_shop[] = {" + ", ".join(map(str, camp["shop"])) + "};")
    ci.append("const uint16_t tech01_council[][2] = {" + ", ".join(f"{{{spk[s]}, {tx}}}" for s, tx in camp["council"]) + "};")
    ci.append(f"const uint8_t tech01_deploy_forced = {forced};")
    ci.append("const uint8_t tech01_levels[] = {" + ", ".join("1" for _ in UNITS) + "};")
    ci.append("const Tech01Unit tech01_units[] = {")
    for k, (n, _, cl, f, x, y) in enumerate(UNITS):
        ci.append(f"    {{{cl}, {fac[f]}, {x}, {y}, {UNIT_AI.get(k, 0)}, 0}}, /* {n} */")
    ci.append("};")
    (ROOT / "src/gen/tech01_data.h").write_text("\n".join(h) + "\n", encoding="utf-8")
    (ROOT / "src/gen/tech01_data.c").write_text("\n".join(c) + "\n", encoding="utf-8")
    (ROOT / "src/gen/tech01_index.c").write_text("\n".join(ci) + "\n", encoding="utf-8")

    # battle JSON (schema v2, 附录C.2); player units come from the roster, not this file
    battle = {
        "schema_version": 2, "id": "BT_TECH01", "profile": "DEFAULT_D1", "source_ref": "ORIGINAL",
        "map": {"width": W, "height": H, "tileset": "TS_TECH01",
                "metatiles": terrain, "terrain": terrain},
        "deploy": {"slots": [[x, y] for n, tag, cl, f, x, y in UNITS if f == "PLAYER"], "max_count": CAMP["deploy"]["max_count"],
                   "forced": CAMP["deploy"]["forced"], "forbidden": []},
        "templates": [{"id": i, "character": "ANON", "class": cl, "level": 1, "str": 70, "int": 60, "cmd": 70,
                       "items": [None] * 8, "equipment": [None] * 3, "instance_id": 3 + i,
                       "blocks_victory": True, "appearance_variant": 0}
                      for i, (n, tag, cl, f, x, y) in enumerate(u for u in UNITS if u[3] == "ENEMY")],
        "units": [{"template": i, "x": x, "y": y, "faction": "ENEMY", "ai_group": UNIT_AI.get(3 + i, 0), "hp": "max", "status": []}
                  for i, (n, tag, cl, f, x, y) in enumerate(u for u in UNITS if u[3] == "ENEMY")],
        "ai_groups": AI_GROUPS,
        "treasures": [], "zones": [],
        "reinforcements": [   # SPAWN templates: compile-time reserved instance slots (design doc 9.3)
            {"template": 0, "character": "ANON", "class": 6, "level": 2, "faction": "ENEMY", "instance_id": 8},
            {"template": 1, "character": "ANON", "class": 3, "level": 2, "faction": "ENEMY", "instance_id": 9},
        ],
        "events": [
            {"id": 1, "trigger": "BATTLE_START", "param": None, "priority": 0, "once": True, "condition": None,
             "code": [["TEXT", "P0001", "TXD_T01_OPEN_LIUBEI"], ["TEXT", "P0002", "TXD_T01_OPEN_GUANYU"],
                      ["TEXT", "P0003", "TXD_T01_OPEN_ZHANGFEI"], ["TEXT", "NARRATOR", "TXD_T01_GOAL"]]},
            {"id": 2, "trigger": "PHASE_START", "param": "ENEMY", "priority": 0, "once": True,
             "condition": {"leaf": "turn_cmp", "op": "eq", "n": 3},
             "code": [["TEXT", "NARRATOR", "TXD_T01_REINF"], ["SPAWN", 0, 30, 10], ["SPAWN", 1, 30, 14],
                      ["SET_AI", 8, "AGGRESSIVE", 0, 3]]},   # the cavalry hunts 刘备 (designated target)
            {"id": 3, "trigger": "UNIT_RETREAT", "param": None, "priority": 0, "once": True,
             "condition": {"any": [{"leaf": "target_eq", "unit": u} for u in (3, 4, 5, 6, 8, 9)]},
             "code": [["TEXT", "P0003", "TXD_T01_FIRST_KILL"]]},
            {"id": 4, "trigger": "POST_MOVE", "param": 0, "priority": 0, "once": True,
             "condition": {"leaf": "unit_in_rect", "unit": 0, "rect": [7, 10, 7, 10]},
             "code": [["TEXT", "NARRATOR", "TXD_T01_VILLAGE"], ["HP_SET", 0, 9999], ["HP_SET", 1, 9999], ["HP_SET", 2, 9999]]},
            {"id": 6, "trigger": "TALK", "param": 7, "priority": 0, "once": True,
             "condition": {"leaf": "target_eq", "unit": 4},
             "code": [["TEXT", "NARRATOR", "TXD_T01_TALK_JY"], ["RECRUIT", 4], ["TEXT", "NARRATOR", "TXD_T01_TALK_DONE"]]},
            {"id": 7, "trigger": "PRE_ACTION", "param": 1, "priority": 0, "once": True,
             "condition": {"leaf": "target_eq", "unit": 3},
             "code": [["TEXT", "P0002", "TXD_T01_DUEL_GUANYU"], ["DUEL", 1, 3, 0], ["TEXT", "NARRATOR", "TXD_T01_DUEL_DONE"]]},
            {"id": 5, "trigger": "BATTLE_END", "param": None, "priority": 0, "once": True,
             "condition": {"leaf": "unit_alive", "unit": 0},
             "code": [["TEXT", "P0001", "TXD_T01_WIN"], ["TEXT", "NARRATOR", "TXD_T01_PRIZE"],
                      ["GIVE_ITEM", 0x70], ["GIVE_ITEM", 4]]},          # 传国玉玺 (plot) + 美酒 (7.4)
        ],
        "victory": {"leaf": "all_enemy_removed"},
        "defeat": {"leaf": "unit_removed", "unit": 0},
        "turn_limit": 30, "round_mode": "RESET", "continuity": "NORMAL", "seed": 1, "bgm": "SONG_BATTLE",
        "next": [{"target": "END", "condition": {"all": []}, "priority": 0}],
    }
    out = ROOT / "content/battles/BT_TECH01.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(battle, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"gen_tech01: {len(tiles)} bg tiles, {len(spr)} obj tiles, {len(UNITS)} units")


if __name__ == "__main__":
    main()
