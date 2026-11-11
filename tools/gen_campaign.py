#!/usr/bin/env python3
"""Campaign data (design doc 2.1, 2.4, 9.4): the battle chain, the starting roster, and battles 2-3.

Battle 1 (BT_TECH01) and the shared tileset come from tools/gen_tech01.py; this script adds the two
original follow-up battles (maps, units, AI groups, treasures, events) as content/battles/BT_*.json and
emits src/gen/campaign_data.{h,c}: battle_defs[] (one row per battle), camp_defs[], roster_init[],
the ending page list. Terrain of battles 2-3 lives in asset bank 15 (copied to WRAM via the gateway).
Unit records: the first entries of every battle are the deployment slots of roster officers 0..3
(instances fixed per battle so events can name characters); enemies follow.
"""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FAC = {"PLAYER": 0, "ALLY": 1, "ENEMY": 2}
AI_MODES = {"HOLD": 0, "AGGRESSIVE": 1, "GUARD_POINT": 2, "ESCORT": 3, "ESCAPE": 4}
CONT = {"NORMAL": 0, "NO_REPLENISH": 1}
ROUND = {"RESET": 0, "CARRY": 1}
SPK = {"LIUBEI": 0, "GUANYU": 1, "ZHANGFEI": 2, "NARRATOR": 0xFF}


def die(m):
    sys.exit("gen_campaign: " + m)


# terrain digits: 0 plain 1 road 2 forest 3 village 4 fort 5 hill 6 shallow 7 deep 8 wall 9 bridge
TECH02_MAP = [
    "0000000000006760000000000000",
    "0000220000006760000000000000",
    "0002222000006760000000000000",
    "0002202200006760000888888800",
    "0000222000006760000800000800",
    "0000020000006760000804440800",
    "0000000000006760000800000800",
    "0000000000006760000800000800",
    "0000000000006760000800400800",
    "1111111111119991111100000801",
    "0000000000006760000800000800",
    "0000000000006760000800000800",
    "0000000000006760000804440800",
    "0000000000006760000800000800",
    "0003000000006760555888888800",
    "0000000000006760055500000000",
    "0000000000006760005550000000",
    "0000000000006666000555000000",
    "0000000000006760000000000000",
    "0000000000006760000000000000",
]
TECH03_MAP = [
    "55500000000000000555",
    "55000002220000000055",
    "50000022222000000005",
    "00000002220000000000",
    "00000000000000000000",
    "00000000000555000000",
    "11111111111111111111",
    "00000000000555000000",
    "00000000000000000000",
    "00000000002220000000",
    "50000000022222000005",
    "55000000002220000055",
    "55500000000000000555",
    "55550000000000005555",
]

TECH02 = {
    "id": "BT_TECH02", "map": TECH02_MAP,
    "slots": [(2, 8), (3, 9), (2, 10), (3, 11)],                     # roster officers 0..3 -> instances 0..3
    "enemies": [  # (class, x, y, ai group, level)
        (0, 16, 9, 0, 2), (3, 17, 7, 0, 2), (6, 18, 11, 0, 2),
        (1, 21, 9, 1, 3),                                           # 长戟兵 guards the gate
        (9, 23, 10, 2, 3),                                          # 文士 holds and heals
        (13, 24, 12, 3, 2),                                         # 辎重队 escapes to the south-east exit
    ],
    "ai_groups": [{"id": 0, "mode": "AGGRESSIVE", "target": None, "leash": 3},
                  {"id": 1, "mode": "GUARD_POINT", "target": [20, 9], "leash": 2},
                  {"id": 2, "mode": "HOLD", "target": None, "leash": 3},
                  {"id": 3, "mode": "ESCAPE", "target": [27, 19], "leash": 0}],
    "enemy_items": [(4, 1), (6, 0)],                                 # (enemy index, item): wagon rations
    "treasures": [(6, 4, 0x63), (3, 16, 1)],                         # 驿马 in the woods, 军粮 at the village
    "turn_limit": 25, "continuity": "NORMAL", "round_mode": "RESET", "deploy_max": 4, "forced": [0], "seed": 7,
    "events": [
        {"id": 1, "trigger": "BATTLE_START", "param": None, "priority": 0, "once": True, "condition": None,
         "code": [["TEXT", "P0001", "TXD_T02_OPEN_1"], ["TEXT", "P0003", "TXD_T02_OPEN_2"]]},
        {"id": 2, "trigger": "PHASE_START", "param": "ENEMY", "priority": 0, "once": True,
         "condition": {"leaf": "turn_cmp", "op": "ge", "n": 3},
         "code": [["TEXT", "NARRATOR", "TXD_T02_ROUND3"]]},
        {"id": 3, "trigger": "BATTLE_END", "param": None, "priority": 0, "once": True,
         "condition": {"leaf": "unit_alive", "unit": 0},
         "code": [["TEXT", "P0001", "TXD_T02_WIN"], ["GIVE_GOLD", 300], ["GIVE_ITEM", 0x45], ["GIVE_ITEM", 1]]},
    ],
}
TECH03 = {
    "id": "BT_TECH03", "map": TECH03_MAP,
    "slots": [(1, 6), (2, 5), (2, 7), (1, 8)],
    "enemies": [(0, 13, 6, 0, 3), (3, 15, 4, 0, 3), (6, 16, 8, 0, 3), (0, 18, 6, 1, 3)],
    "ai_groups": [{"id": 0, "mode": "AGGRESSIVE", "target": None, "leash": 3},
                  {"id": 1, "mode": "HOLD", "target": None, "leash": 3}],
    "enemy_items": [],
    "treasures": [(10, 2, 0x52)],                                    # 六韬 on the northern rise
    "turn_limit": 45, "continuity": "NO_REPLENISH", "round_mode": "CARRY", "deploy_max": 4, "forced": [0], "seed": 11,
    "events": [
        {"id": 1, "trigger": "BATTLE_START", "param": None, "priority": 0, "once": True, "condition": None,
         "code": [["TEXT", "NARRATOR", "TXD_T03_OPEN"]]},
        {"id": 2, "trigger": "BATTLE_END", "param": None, "priority": 0, "once": True,
         "condition": {"leaf": "unit_alive", "unit": 0},
         "code": [["TEXT", "P0001", "TXD_T03_WIN"]]},
    ],
}
EXTRA = [TECH02, TECH03]


def battle_json(bt):
    h, w = len(bt["map"]), len(bt["map"][0])
    if any(len(r) != w for r in bt["map"]):
        die(f"{bt['id']}: ragged map")
    terrain = [int(c) for r in bt["map"] for c in r]
    for x, y in bt["slots"] + [(e[1], e[2]) for e in bt["enemies"]] + [(t[0], t[1]) for t in bt["treasures"]]:
        if not (0 <= x < w and 0 <= y < h) or terrain[y * w + x] in (7, 8):
            die(f"{bt['id']}: ({x},{y}) outside or impassable")
    n0 = len(bt["slots"])
    return {
        "schema_version": 2, "id": bt["id"], "profile": "DEFAULT_D1", "source_ref": "ORIGINAL",
        "map": {"width": w, "height": h, "tileset": "TS_TECH01", "metatiles": terrain, "terrain": terrain},
        "deploy": {"slots": [list(s) for s in bt["slots"]], "max_count": bt["deploy_max"],
                   "forced": [["CH_LIUBEI", "CH_GUANYU", "CH_ZHANGFEI", "CH_JIANYONG"][k] for k in bt["forced"]], "forbidden": []},
        "templates": [{"id": i, "character": "ANON", "class": e[0], "level": e[4], "str": 70, "int": 60, "cmd": 70,
                       "items": [None] * 8, "equipment": [None] * 3, "instance_id": n0 + i,
                       "blocks_victory": True, "appearance_variant": 0} for i, e in enumerate(bt["enemies"])],
        "units": [{"template": i, "x": e[1], "y": e[2], "faction": "ENEMY", "ai_group": e[3], "hp": "max", "status": []}
                  for i, e in enumerate(bt["enemies"])],
        "ai_groups": bt["ai_groups"], "reinforcements": [], "events": bt["events"],
        "victory": {"leaf": "all_enemy_removed"}, "defeat": {"leaf": "unit_removed", "unit": 0},
        "turn_limit": bt["turn_limit"], "round_mode": bt["round_mode"], "continuity": bt["continuity"],
        "seed": bt["seed"], "bgm": "SONG_BATTLE", "treasures": [list(t) for t in bt["treasures"]],
    }, terrain, w, h


def ai_recs(groups, w):
    recs = []
    for g in groups:
        t = g["target"]
        tgt = 0xFFFF if t is None else (t[1] * w + t[0] if isinstance(t, list) else t)
        recs += [g["id"], AI_MODES[g["mode"]], tgt & 0xFF, tgt >> 8, g["leash"]]
    return recs


def main():
    camp_list = json.loads((ROOT / "content/campaign.json").read_text(encoding="utf-8"))
    roster = json.loads((ROOT / "content/roster.json").read_text(encoding="utf-8"))["officers"]
    items = {it["id"] for it in json.loads((ROOT / "content/items.json").read_text(encoding="utf-8"))["items"]}
    ids = [b["id"] for b in camp_list["battles"]]
    if camp_list["start"] != ids[0]:
        die("the campaign starts with its first battle")
    c = ["/* GENERATED by tools/gen_campaign.py - do not edit */", '#include "campaign_data.h"', '#include "text_data.h"']
    asset = ["#pragma bank 15   /* asset bank: terrain of battles 2+ (read through the gateway) */", '#include "campaign_data.h"']
    rows = []
    for n, bt in enumerate(EXTRA, start=1):
        js, terrain, w, h = battle_json(bt)
        (ROOT / f"content/battles/{bt['id']}.json").write_text(json.dumps(js, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
        p = f"bt{n}"
        asset.append(f"const uint8_t {p}_terrain[{w * h}] = {{" + ", ".join(map(str, terrain)) + "};")
        units = [(r["class"], 0, x, y, 0) for r, (x, y) in zip(roster, bt["slots"])]
        units += [(e[0], 2, e[1], e[2], e[3]) for e in bt["enemies"]]
        c.append(f"extern const uint8_t {p}_terrain[];")
        c.append(f"static const Tech01Unit {p}_units[] = {{" + ", ".join("{%d, %d, %d, %d, %d}" % u for u in units) + "};")
        c.append(f"static const uint8_t {p}_levels[] = {{" + ", ".join(["1"] * len(bt["slots"]) + [str(e[4]) for e in bt["enemies"]]) + "};")
        ei = [(len(bt["slots"]) + k, it) for k, it in bt["enemy_items"]]
        for _, it in ei:
            if it not in items:
                die(f"unknown item {it}")
        c.append(f"static const uint8_t {p}_items[][2] = {{" + (", ".join(f"{{{u}, {i}}}" for u, i in ei) or "{0xFF, 0xFF}") + "};")
        c.append(f"static const uint8_t {p}_ai[] = {{" + ", ".join(map(str, ai_recs(bt["ai_groups"], w))) + "};")
        c.append(f"static const uint8_t {p}_tre[][3] = {{" + ", ".join(f"{{{x}, {y}, {i}}}" for x, y, i in bt["treasures"]) + "};")
        forced = sum(1 << k for k in bt["forced"])
        rows.append((p, w, h, 15, len(units), len(ei), len(bt["ai_groups"]), len(bt["treasures"]), bt["turn_limit"],
                     CONT[bt["continuity"]], ROUND[bt["round_mode"]], bt["deploy_max"], forced, bt["seed"]))
    # battle table: row 0 = TECH01 (tech01_* arrays from gen_tech01.py)
    defs = ["const BattleDef battle_defs[N_BATTLES] = {",
            "    {TECH01_W, TECH01_H, TECH01_BANK, tech01_terrain, TECH01_UNITS, tech01_units, tech01_levels, TECH01_START_ITEMS, "
            "tech01_start_items, TECH01_AI_GROUPS, tech01_ai_groups, TECH01_TREASURES, tech01_treasures, 30, 0, 0, "
            "TECH01_DEPLOY_MAX, 1, 1, %s, %s, %s},"]
    def node(i):
        b = camp_list["battles"][i]
        camp = "0" if b["camp"] == "CAMP_TECH01" else "0xFF"
        nxt = "0xFF" if b["next"] is None else str(ids.index(b["next"]))
        return camp, nxt, b["title"]
    defs[1] = defs[1] % node(0)
    for n, r in enumerate(rows, start=1):
        p = r[0]
        camp, nxt, title = node(n)
        defs.append(f"    {{{r[1]}, {r[2]}, {r[3]}, {p}_terrain, {r[4]}, {p}_units, {p}_levels, {r[5]}, {p}_items, {r[6]}, {p}_ai, "
                    f"{r[7]}, {p}_tre, {r[8]}, {r[9]}, {r[10]}, {r[11]}, {r[12]}, {r[13]}, {camp}, {nxt}, {title}}},")
    defs.append("};")
    c += defs
    c.append("const CampDef camp_defs[1] = {{TECH01_SHOP_N, tech01_shop, TECH01_COUNCIL_N, tech01_council}};")
    c.append("const RosterInit roster_init[N_ROSTER_INIT] = {")
    for o in roster:
        its = o["items"] + [0xFF] * (8 - len(o["items"]))
        c.append(f"    {{{o['character']}, {o['class']}, {o['level']}, {o['base'][0]}, {o['base'][1]}, {o['base'][2]}, {o['name']}, "
                 "{" + ", ".join(map(str, its)) + "}},")
    c.append("};")
    c.append("const uint16_t ending_pages[][2] = {" + ", ".join(f"{{{SPK[s]}, {t}}}" for s, t in camp_list["ending"]) + "};")
    h_ = ["/* GENERATED by tools/gen_campaign.py - do not edit */", "#ifndef CAMPAIGN_DATA_H", "#define CAMPAIGN_DATA_H",
          "#include <stdint.h>", '#include "tech01_data.h"',
          f"#define N_BATTLES {len(ids)}u", f"#define N_ROSTER_INIT {len(roster)}u", f"#define N_ENDING_PAGES {len(camp_list['ending'])}u",
          "#define CONT_NORMAL 0u\n#define CONT_NO_REPLENISH 1u\n#define ROUND_RESET 0u\n#define ROUND_CARRY 1u",
          "typedef struct {",
          "    uint8_t w, h, tbank;                 /* terrain in ROM bank tbank */",
          "    const uint8_t *terrain;",
          "    uint8_t n_units; const Tech01Unit *units; const uint8_t *levels;   /* player slots first */",
          "    uint8_t n_items; const uint8_t (*items)[2];                      /* start items (unit, item) */",
          "    uint8_t n_ai; const uint8_t *ai;                                 /* AI groups, 5 B each */",
          "    uint8_t n_tre; const uint8_t (*tre)[3];                          /* treasures (x, y, item) */",
          "    uint16_t turn_limit;",
          "    uint8_t continuity, round_mode, deploy_max, forced, seed;        /* forced: bit k = officer k */",
          "    uint8_t camp, next, title_tx;                                    /* 0xFF: continuous / ending */",
          "} BattleDef;",
          "typedef struct { uint8_t n_shop; const uint8_t *shop; uint8_t n_council; const uint16_t (*council)[2]; } CampDef;",
          "typedef struct { uint16_t character; uint8_t cls, level, str, intl, cmd, name_tx; uint8_t items[8]; } RosterInit;",
          "extern const BattleDef battle_defs[N_BATTLES];", "extern const CampDef camp_defs[1];",
          "extern const RosterInit roster_init[N_ROSTER_INIT];", "extern const uint16_t ending_pages[][2];",
          "extern const uint8_t tech01_levels[];", "#endif"]
    (ROOT / "src/gen/campaign_data.h").write_text("\n".join(h_) + "\n", encoding="utf-8")
    (ROOT / "src/gen/campaign_data.c").write_text("\n".join(c) + "\n", encoding="utf-8")
    (ROOT / "src/gen/campaign_assets.c").write_text("\n".join(asset) + "\n", encoding="utf-8")
    print(f"gen_campaign: {len(ids)} battles, {len(roster)} officers")


if __name__ == "__main__":
    main()
