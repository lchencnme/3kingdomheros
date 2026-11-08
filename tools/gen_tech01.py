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
BG_PALS = [
    [PAPER, (168, 192, 128), (192, 144, 80), INK],    # 0 ground: plain/road
    [PAPER, (112, 160, 96), (138, 106, 64), INK],     # 1 nature: forest/hill
    [PAPER, (144, 184, 208), (72, 120, 168), INK],    # 2 water: shallow/deep/bridge
    [PAPER, (176, 64, 48), (138, 128, 112), INK],     # 3 buildings: village/fort/wall
]


def tint(c):  # move-range highlight: shift toward stone-blue
    return tuple((v * 5 + t * 3) // 8 for v, t in zip(c, (96, 140, 220)))


BG_PALS += [[tint(c) for c in p] for p in BG_PALS]   # 4..7 highlighted versions
OBJ_PALS = [
    [PAPER, (48, 80, 160), (224, 176, 128), INK],      # 0 player
    [PAPER, (160, 48, 32), (224, 176, 128), INK],      # 1 enemy
    [PAPER, (248, 248, 240), (200, 40, 32), INK],      # 2 cursor
    [PAPER, (120, 120, 128), (180, 170, 160), INK],    # 3 acted
]


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

    h = ["/* GENERATED by tools/gen_tech01.py - placeholder art, do not edit */",
         "#ifndef TECH01_DATA_H\n#define TECH01_DATA_H\n#include <stdint.h>",
         f"#define TECH01_W {W}u\n#define TECH01_H {H}u",
         f"#define TECH01_BG_TILES {len(tiles)}u\n#define TECH01_OBJ_TILES {len(spr)}u",
         f"#define TECH01_UNITS {len(UNITS)}u", "#define TECH01_BANK 2u",
         "#define OBJT_PLAYER 0u\n#define OBJT_ENEMY 4u\n#define OBJT_CURSOR 8u",
         "typedef struct { uint8_t cls, faction, x, y; } Tech01Unit;",
         "extern const uint8_t tech01_bg_tiles[];", "extern const uint8_t tech01_obj_tiles[];",
         "extern const uint16_t tech01_bg_pals[32];", "extern const uint16_t tech01_obj_pals[16];",
         "extern const uint8_t tech01_meta[10][4];", "extern const uint8_t tech01_terrain_pal[10];",
         "extern const uint8_t tech01_terrain[];", "extern const Tech01Unit tech01_units[];",
         "#endif"]
    c = ["#pragma bank 2", "#include \"tech01_data.h\""]   # asset bank: read only through rom.c gateway
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
    ci += arr("tech01_bg_pals", "uint16_t", [rgb555(col) for p in BG_PALS for col in p], 4)
    ci += arr("tech01_obj_pals", "uint16_t", [rgb555(col) for p in OBJ_PALS for col in p], 4)
    ci.append("const uint8_t tech01_meta[10][4] = {" + ", ".join("{" + ", ".join(map(str, m)) + "}" for m in meta) + "};")
    ci.append("const uint8_t tech01_terrain_pal[10] = {" + ", ".join(str(TERRAIN_PAL[i]) for i in range(10)) + "};")
    fac = {"PLAYER": 0, "ALLY": 1, "ENEMY": 2}
    ci.append("const Tech01Unit tech01_units[] = {")
    for n, _, cl, f, x, y in UNITS:
        ci.append(f"    {{{cl}, {fac[f]}, {x}, {y}}}, /* {n} */")
    ci.append("};")
    (ROOT / "src/gen/tech01_data.h").write_text("\n".join(h) + "\n", encoding="utf-8")
    (ROOT / "src/gen/tech01_data.c").write_text("\n".join(c) + "\n", encoding="utf-8")
    (ROOT / "src/gen/tech01_index.c").write_text("\n".join(ci) + "\n", encoding="utf-8")

    # battle JSON (schema v2, 附录C.2); player units come from the roster, not this file
    battle = {
        "schema_version": 2, "id": "BT_TECH01", "profile": "DEFAULT_D1", "source_ref": "ORIGINAL",
        "map": {"width": W, "height": H, "tileset": "TS_TECH01",
                "metatiles": terrain, "terrain": terrain},
        "deploy": {"slots": [[x, y] for n, tag, cl, f, x, y in UNITS if f == "PLAYER"], "max_count": 4,
                   "forced": ["CH_LIUBEI", "CH_GUANYU", "CH_ZHANGFEI", "CH_JIANYONG"], "forbidden": []},
        "templates": [{"id": i, "character": "ANON", "class": cl, "level": 1, "str": 70, "int": 60, "cmd": 70,
                       "items": [None] * 8, "equipment": [None] * 3, "instance_id": 3 + i,
                       "blocks_victory": True, "appearance_variant": 0}
                      for i, (n, tag, cl, f, x, y) in enumerate(u for u in UNITS if u[3] == "ENEMY")],
        "units": [{"template": i, "x": x, "y": y, "faction": "ENEMY", "ai_group": 0, "hp": "max", "status": []}
                  for i, (n, tag, cl, f, x, y) in enumerate(u for u in UNITS if u[3] == "ENEMY")],
        "ai_groups": [{"id": 0, "mode": "AGGRESSIVE", "target": None, "leash": 3}],
        "treasures": [], "zones": [],
        "reinforcements": [   # SPAWN templates: compile-time reserved instance slots (design doc 9.3)
            {"template": 0, "character": "ANON", "class": 6, "level": 2, "faction": "ENEMY", "instance_id": 8},
            {"template": 1, "character": "ANON", "class": 3, "level": 2, "faction": "ENEMY", "instance_id": 9},
        ],
        "events": [
            {"id": 1, "trigger": "BATTLE_START", "param": None, "priority": 0, "once": True, "condition": None,
             "code": [["TEXT", "P0001", "TXD_T01_OPEN_LIUBEI"], ["TEXT", "P0002", "TXD_T01_OPEN_GUANYU"],
                      ["TEXT", "P0003", "TXD_T01_OPEN_ZHANGFEI"]]},
            {"id": 2, "trigger": "PHASE_START", "param": "ENEMY", "priority": 0, "once": True,
             "condition": {"leaf": "turn_cmp", "op": "eq", "n": 3},
             "code": [["TEXT", "NARRATOR", "TXD_T01_REINF"], ["SPAWN", 0, 30, 10], ["SPAWN", 1, 30, 14]]},
            {"id": 3, "trigger": "UNIT_RETREAT", "param": None, "priority": 0, "once": True,
             "condition": {"any": [{"leaf": "target_eq", "unit": u} for u in (3, 4, 5, 6, 8, 9)]},
             "code": [["TEXT", "P0003", "TXD_T01_FIRST_KILL"]]},
            {"id": 4, "trigger": "POST_MOVE", "param": 0, "priority": 0, "once": True,
             "condition": {"leaf": "unit_in_rect", "unit": 0, "rect": [7, 10, 7, 10]},
             "code": [["TEXT", "NARRATOR", "TXD_T01_VILLAGE"], ["HP_SET", 0, 9999], ["HP_SET", 1, 9999], ["HP_SET", 2, 9999]]},
            {"id": 5, "trigger": "BATTLE_END", "param": None, "priority": 0, "once": True,
             "condition": {"leaf": "unit_alive", "unit": 0},
             "code": [["TEXT", "P0001", "TXD_T01_WIN"]]},
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
