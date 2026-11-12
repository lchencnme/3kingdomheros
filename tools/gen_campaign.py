#!/usr/bin/env python3
"""Campaign data (design doc 2.1, 2.4, 6.6, 9.4, 15.2): the battle chain, camps, roster, characters, and
the Chapter 1 battles BT_01..BT_05 (original maps and dialogue after the public-domain novel).

Battle 0 (BT_TECH01, the prologue drill) and the shared tileset come from tools/gen_tech01.py. This script
writes content/battles/BT_*.json for the other battles (compiled by gen_events.py) and emits
src/gen/campaign_data.{h,c} (battle_defs[], camp_defs[], roster_init[], char_name[], ending pages) plus
their terrain in asset bank 15. Unit records: the first PLAYER entries of a battle are the deployment slots
of roster officers 0..n (instances fixed per battle, so events can name characters); allies and enemies
follow in the listed order.
"""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
AI_MODES = {"HOLD": 0, "AGGRESSIVE": 1, "GUARD_POINT": 2, "ESCORT": 3, "ESCAPE": 4}
CONT = {"NORMAL": 0, "NO_REPLENISH": 1}
ROUND = {"RESET": 0, "CARRY": 1}
VIC = {"ALL": 0, "DEFEAT": 1, "SURVIVE": 2}
FAC = {"PLAYER": 0, "ALLY": 1, "ENEMY": 2}
SPK = {"LIUBEI": 0, "GUANYU": 1, "ZHANGFEI": 2, "NARRATOR": 0xFF}
OFFICER_TAGS = ["CH_LIUBEI", "CH_GUANYU", "CH_ZHANGFEI", "CH_JIANYONG"]


def die(m):
    sys.exit("gen_campaign: " + m)


def U(cls, x, y, ai=0, lv=1, ch=0, fac="ENEMY"):
    return {"cls": cls, "x": x, "y": y, "ai": ai, "lv": lv, "ch": ch, "fac": fac}


def G(i, mode, target=None, leash=3):
    return {"id": i, "mode": mode, "target": target, "leash": leash}


# terrain digits: 0 plain 1 road 2 forest 3 village 4 fort 5 hill 6 shallow 7 deep 8 wall 9 bridge
# ---------------------------------------------------------------- BT_01 涿郡破黄巾 (回目 1, tutorial)
BT01 = {
    "id": "BT_01", "map": [
        "000000222000000000055500",
        "000002222200000000555550",
        "000000222000000000055500",
        "000000000000676000000000",
        "003000000000676000004440",
        "000000000000676000004440",
        "111111111111999111111440",
        "000000000000676000004440",
        "000000000000676000000000",
        "002200000000676000000000",
        "022220000000676000022200",
        "002200000000676000222220",
        "000000000000666000022200",
        "000005550000676000000000",
        "000055555000676000000000",
        "000005550000676000000000"],
    "slots": [(2, 5), (2, 7), (3, 6), (1, 6)],
    "others": [U(0, 21, 5, ai=1, lv=4, ch=10),                      # 4 程远志 holds the camp
               U(0, 17, 6, ai=0, lv=3, ch=11),                      # 5 邓茂 leads the charge
               U(0, 15, 4), U(0, 16, 8), U(0, 19, 10), U(3, 20, 7), U(3, 22, 8)],
    "ai_groups": [G(0, "AGGRESSIVE"), G(1, "HOLD")],
    "items": [(5, 0)], "treasures": [(20, 11, 1)],
    "victory": ("DEFEAT", 4, 0), "protect": None,
    "turn_limit": 20, "continuity": "NORMAL", "round_mode": "RESET", "deploy_max": 3, "forced": [0, 1, 2], "seed": 3,
    "events": [
        {"id": 1, "trigger": "BATTLE_START", "param": None, "priority": 0, "once": True, "condition": None,
         "code": [["TEXT", "NARRATOR", "TXD_B01_OPEN_0"], ["TEXT", "P0001", "TXD_B01_OPEN_1"],
                  ["TEXT", "P0003", "TXD_B01_OPEN_2"], ["TEXT", "P0002", "TXD_B01_OPEN_3"], ["TEXT", "NARRATOR", "TXD_B01_GOAL"]]},
        {"id": 2, "trigger": "UNIT_RETREAT", "param": 5, "priority": 0, "once": True, "condition": None,
         "code": [["TEXT", "P0003", "TXD_B01_DENGMAO"]]},
        {"id": 3, "trigger": "BATTLE_END", "param": None, "priority": 0, "once": True, "condition": {"leaf": "unit_alive", "unit": 0},
         "code": [["TEXT", "P0002", "TXD_B01_WIN_1"], ["TEXT", "P0001", "TXD_B01_WIN_2"], ["GIVE_GOLD", 200], ["GIVE_ITEM", 1]]},
    ],
}
# ---------------------------------------------------------------- BT_02 青州解围 (回目 1, ally / facility)
BT02 = {
    "id": "BT_02", "map": [
        "00000000000000000000000000",
        "00222000000000000000022200",
        "02222200000000000000222220",
        "00222000088888888800022200",
        "00000000084444444800000000",
        "00000000084400044800000000",
        "00000000084000004800000000",
        "11111111111000001111111111",
        "00000000084000004800000000",
        "00000000084400044800000000",
        "00000000084444444800000000",
        "00000000088888888800000000",
        "00000000000000000000000000",
        "00555000000000000000005550",
        "05555500000003000000055555",
        "00555000000000000000005550",
        "00000000000000000000000000",
        "00000000000000000000000000"],
    "slots": [(1, 7), (2, 6), (2, 8), (0, 6)],
    "others": [U(9, 13, 6, ai=2, lv=3, ch=12, fac="ALLY"),       # 4 龚景 (must survive), stays in the keep
               U(0, 12, 8, ai=3, lv=2, fac="ALLY"), U(0, 14, 8, ai=3, lv=2, fac="ALLY"),
               U(0, 6, 4), U(0, 6, 10), U(3, 20, 4), U(3, 20, 10), U(0, 22, 7), U(0, 19, 7),
               U(6, 24, 6, lv=2), U(6, 24, 8, lv=2)],
    "ai_groups": [G(0, "AGGRESSIVE"), G(2, "HOLD"), G(3, "GUARD_POINT", [13, 7], 2)],
    "items": [], "treasures": [(13, 14, 3)],
    "victory": ("ALL", None, 0), "protect": 4,
    "turn_limit": 25, "continuity": "NORMAL", "round_mode": "RESET", "deploy_max": 4, "forced": [0], "seed": 5,
    "events": [
        {"id": 1, "trigger": "BATTLE_START", "param": None, "priority": 0, "once": True, "condition": None,
         "code": [["TEXT", "NARRATOR", "TXD_B02_OPEN_0"], ["TEXT", "P0001", "TXD_B02_OPEN_1"], ["TEXT", "NARRATOR", "TXD_B02_GOAL"]]},
        {"id": 2, "trigger": "PHASE_START", "param": "PLAYER", "priority": 0, "once": True,
         "condition": {"leaf": "turn_cmp", "op": "ge", "n": 3},
         "code": [["TEXT", "NARRATOR", "TXD_B02_ROUND3"]]},
        {"id": 3, "trigger": "BATTLE_END", "param": None, "priority": 0, "once": True, "condition": {"leaf": "unit_alive", "unit": 0},
         "code": [["TEXT", "NARRATOR", "TXD_B02_WIN"], ["GIVE_GOLD", 300], ["GIVE_ITEM", 4]]},
    ],
}
# ---------------------------------------------------------------- BT_03 破张宝妖术 (回目 2, continuous)
BT03 = {
    "id": "BT_03", "map": [
        "555550000000000000222000",
        "555500000002200002222200",
        "550000000022220000222000",
        "500000000002200000000000",
        "000000000000000000000555",
        "000000222000000000005555",
        "111111111111111111115555",
        "000000222000000000005555",
        "000000000000003000000555",
        "000000000000000000000000",
        "002220000000000000222000",
        "022222000005550002222200",
        "002220000055555000222000",
        "000000000005550000000000"],
    "slots": [(1, 6), (2, 5), (2, 7), (0, 7)],
    "others": [U(16, 21, 6, ai=1, lv=6, ch=13),                    # 4 张宝 (方士) at the shrine on the hill
               U(16, 18, 4, lv=3), U(16, 18, 9, lv=3),
               U(0, 12, 5, lv=2), U(0, 12, 7, lv=2), U(3, 15, 3, lv=2), U(3, 15, 10, lv=2)],
    "ai_groups": [G(0, "AGGRESSIVE"), G(1, "HOLD")],
    "items": [(5, 6), (6, 7)], "treasures": [(11, 12, 0x15)],     # 激流书 (strategy book) on the southern hill
    "victory": ("DEFEAT", 4, 0), "protect": None,
    "turn_limit": 25, "continuity": "NO_REPLENISH", "round_mode": "RESET", "deploy_max": 4, "forced": [0], "seed": 9,
    "events": [
        {"id": 1, "trigger": "BATTLE_START", "param": None, "priority": 0, "once": True, "condition": None,
         "code": [["TEXT", "NARRATOR", "TXD_B03_OPEN_0"], ["TEXT", "P0002", "TXD_B03_OPEN_1"], ["TEXT", "NARRATOR", "TXD_B03_GOAL"]]},
        {"id": 2, "trigger": "PHASE_START", "param": "ENEMY", "priority": 0, "once": True,
         "condition": {"leaf": "turn_cmp", "op": "ge", "n": 2},
         "code": [["TEXT", "NARRATOR", "TXD_B03_STORM"], ["WEATHER", "RAIN"]]},
        {"id": 3, "trigger": "UNIT_RETREAT", "param": 4, "priority": 0, "once": True, "condition": None,
         "code": [["WEATHER", "CLEAR"], ["TEXT", "NARRATOR", "TXD_B03_CLEAR"]]},
        {"id": 4, "trigger": "BATTLE_END", "param": None, "priority": 0, "once": True, "condition": {"leaf": "unit_alive", "unit": 0},
         "code": [["TEXT", "P0001", "TXD_B03_WIN"], ["GIVE_GOLD", 300]]},
    ],
}
# ---------------------------------------------------------------- BT_04 汜水关 (回目 5, duel)
BT04 = {
    "id": "BT_04", "map": [
        "55555555555555555588888888",
        "55500000000000005588444488",
        "50000000000000000088444488",
        "00002200000000000088000088",
        "00022220000000000000000088",
        "00002200000000000000000008",
        "11111111111111111111111118",
        "00000000000000000000000008",
        "00002220000000000000000088",
        "00022222000000000088000088",
        "00002220000000000088444488",
        "50000000000000000088444488",
        "55500000000000005588888888",
        "55555555555555555588888888"],
    "slots": [(1, 6), (2, 5), (2, 7), (0, 5)],
    "others": [U(7, 20, 6, ai=0, lv=8, ch=14),                     # 4 华雄 rides out of the pass
               U(1, 19, 4, lv=4), U(1, 19, 8, lv=4), U(4, 22, 2, ai=1, lv=4), U(4, 22, 10, ai=1, lv=4),
               U(0, 14, 5, lv=3), U(0, 14, 7, lv=3)],
    "ai_groups": [G(0, "AGGRESSIVE"), G(1, "HOLD")],
    "items": [(4, 1)], "treasures": [(4, 9, 0x63)],              # 驿马 in the southern grove
    "victory": ("DEFEAT", 4, 0), "protect": None,
    "turn_limit": 25, "continuity": "NORMAL", "round_mode": "RESET", "deploy_max": 4, "forced": [0, 1], "seed": 13,
    "events": [
        {"id": 1, "trigger": "BATTLE_START", "param": None, "priority": 0, "once": True, "condition": None,
         "code": [["TEXT", "NARRATOR", "TXD_B04_OPEN_0"], ["TEXT", "P0002", "TXD_B04_OPEN_1"], ["TEXT", "NARRATOR", "TXD_B04_GOAL"]]},
        {"id": 2, "trigger": "PRE_ACTION", "param": 1, "priority": 0, "once": True, "condition": {"leaf": "target_eq", "unit": 4},
         "code": [["TEXT", "P0002", "TXD_B04_DUEL_1"], ["DUEL", 1, 4, 0], ["TEXT", "NARRATOR", "TXD_B04_DUEL_2"]]},
        {"id": 3, "trigger": "BATTLE_END", "param": None, "priority": 0, "once": True, "condition": {"leaf": "unit_alive", "unit": 0},
         "code": [["TEXT", "P0001", "TXD_B04_WIN"], ["GIVE_GOLD", 400], ["GIVE_ITEM", 0x41]]},   # 青龙偃月刀
    ],
}
# ---------------------------------------------------------------- BT_05 虎牢关 (回目 5, hold out / 三英战吕布)
BT05 = {
    "id": "BT_05", "map": [
        "55555555558888888888555555",
        "55000000008444444448000055",
        "50000000008444444448000005",
        "00000000008440000448000000",
        "00022200000000000000000000",
        "00222220000000000000002220",
        "11111111111111111111111111",
        "00222220000000000000002220",
        "00022200000000000000000000",
        "00000000008440000448000000",
        "50000000008444444448000005",
        "55000000008444444448000055",
        "55555555558888888888555555"],
    "slots": [(2, 6), (3, 5), (3, 7), (1, 5)],
    "others": [U(1, 12, 3, ai=1, lv=5), U(1, 12, 9, ai=1, lv=5), U(4, 16, 2, ai=1, lv=5), U(4, 16, 10, ai=1, lv=5),
               U(7, 18, 6, lv=6), U(0, 14, 6, lv=5), U(6, 22, 4, lv=5), U(6, 22, 8, lv=5)],
    "templates": [{"template": 0, "character": 15, "class": 8, "level": 15, "faction": "ENEMY"}],   # 吕布 (instance 12)
    "ai_groups": [G(0, "AGGRESSIVE"), G(1, "HOLD")],
    "items": [], "treasures": [(4, 4, 0x07)],
    "victory": ("SURVIVE", 12, 12), "protect": None,                # hold to round 12, or drive off 吕布
    "turn_limit": 30, "continuity": "NORMAL", "round_mode": "RESET", "deploy_max": 4, "forced": [0, 1, 2], "seed": 17,
    "events": [
        {"id": 1, "trigger": "BATTLE_START", "param": None, "priority": 0, "once": True, "condition": None,
         "code": [["TEXT", "NARRATOR", "TXD_B05_OPEN_0"], ["TEXT", "P0003", "TXD_B05_OPEN_1"], ["TEXT", "NARRATOR", "TXD_B05_GOAL"]]},
        {"id": 2, "trigger": "PHASE_START", "param": "ENEMY", "priority": 0, "once": True,
         "condition": {"leaf": "turn_cmp", "op": "ge", "n": 4},
         "code": [["TEXT", "NARRATOR", "TXD_B05_LVBU_1"], ["SPAWN", 0, 24, 6], ["SET_AI", 12, "AGGRESSIVE", 0, 3],
                  ["TEXT", "NARRATOR", "TXD_B05_LVBU_2"]]},
        {"id": 3, "trigger": "PRE_ACTION", "param": 2, "priority": 0, "once": True, "condition": {"leaf": "target_eq", "unit": 12},
         "code": [["TEXT", "P0003", "TXD_B05_SANYING_1"], ["TEXT", "P0002", "TXD_B05_SANYING_2"], ["TEXT", "P0001", "TXD_B05_SANYING_3"],
                  ["DUEL", 2, 12, 0], ["TEXT", "NARRATOR", "TXD_B05_SANYING_4"]]},
        {"id": 4, "trigger": "BATTLE_END", "param": None, "priority": 0, "once": True, "condition": {"leaf": "unit_alive", "unit": 0},
         "code": [["TEXT", "P0001", "TXD_B05_WIN"], ["GIVE_GOLD", 500]]},
    ],
}
EXTRA = [BT01, BT02, BT03, BT04, BT05]


def battle_json(bt):
    h, w = len(bt["map"]), len(bt["map"][0])
    if any(len(r) != w for r in bt["map"]):
        die(f"{bt['id']}: ragged map")
    terrain = [int(c) for r in bt["map"] for c in r]
    cells = bt["slots"] + [(o["x"], o["y"]) for o in bt["others"]] + [(t[0], t[1]) for t in bt["treasures"]]
    for x, y in cells:
        if not (0 <= x < w and 0 <= y < h) or terrain[y * w + x] in (7, 8):
            die(f"{bt['id']}: ({x},{y}) outside or impassable")
    if len(set(cells[:len(bt["slots"]) + len(bt["others"])])) != len(bt["slots"]) + len(bt["others"]):
        die(f"{bt['id']}: two units on one cell")
    n0 = len(bt["slots"])
    n_units = n0 + len(bt["others"])
    tpls = [dict(t, instance_id=n_units + k) for k, t in enumerate(bt.get("templates", []))]
    return {
        "schema_version": 2, "id": bt["id"], "profile": "DEFAULT_D1", "source_ref": "ORIGINAL",
        "map": {"width": w, "height": h, "tileset": "TS_TECH01", "metatiles": terrain, "terrain": terrain},
        "deploy": {"slots": [list(s) for s in bt["slots"]], "max_count": bt["deploy_max"],
                   "forced": [OFFICER_TAGS[k] for k in bt["forced"]], "forbidden": []},
        "templates": [], "units": [{"template": None, "x": o["x"], "y": o["y"], "faction": o["fac"], "class": o["cls"],
                                    "character": o["ch"] or "ANON", "ai_group": o["ai"], "level": o["lv"], "hp": "max", "status": []}
                                   for o in bt["others"]],
        "ai_groups": bt["ai_groups"], "reinforcements": tpls, "events": bt["events"],
        "victory": {"kind": bt["victory"][0], "unit": bt["victory"][1], "round": bt["victory"][2]},
        "protect": bt["protect"],
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
    chars = json.loads((ROOT / "content/characters.json").read_text(encoding="utf-8"))["characters"]
    items = {it["id"]: it for it in json.loads((ROOT / "content/items.json").read_text(encoding="utf-8"))["items"]}
    ids = [b["id"] for b in camp_list["battles"]]
    if camp_list["start"] != ids[0] or ids[0] != "BT_TECH01":
        die("the campaign starts with BT_TECH01 (generated by gen_tech01.py)")
    extra = {bt["id"]: bt for bt in EXTRA}
    camp_ids = []
    for b in camp_list["battles"]:
        if b["camp"] and b["camp"] not in camp_ids:
            camp_ids.append(b["camp"])
    char_ids = {c["id"] for c in chars}
    c = ["/* GENERATED by tools/gen_campaign.py - do not edit */", '#include "campaign_data.h"', '#include "text_data.h"']
    asset = ["#pragma bank 15   /* asset bank: terrain of battles 2+ (read through the gateway) */", '#include "campaign_data.h"']
    rows = {}
    for n, bid in enumerate(ids):
        if n == 0:
            continue
        if bid not in extra:
            die(f"battle {bid} has no definition")
        bt = extra[bid]
        js, terrain, w, h = battle_json(bt)
        (ROOT / f"content/battles/{bid}.json").write_text(json.dumps(js, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
        p = f"bt{n}"
        asset.append(f"const uint8_t {p}_terrain[{w * h}] = {{" + ", ".join(map(str, terrain)) + "};")
        units = [(r["class"], 0, x, y, 0, 0) for r, (x, y) in zip(roster, bt["slots"])]
        for o in bt["others"]:
            if o["ch"] and o["ch"] not in char_ids:
                die(f"{bid}: character {o['ch']} not registered in content/characters.json")
            units.append((o["cls"], FAC[o["fac"]], o["x"], o["y"], o["ai"], o["ch"]))
        for t in bt.get("templates", []):
            if t["character"] not in char_ids:
                die(f"{bid}: template character {t['character']} not registered")
        c.append(f"extern const uint8_t {p}_terrain[];")
        c.append(f"static const Tech01Unit {p}_units[] = {{" + ", ".join("{%d, %d, %d, %d, %d, %d}" % u for u in units) + "};")
        c.append(f"static const uint8_t {p}_levels[] = {{" + ", ".join(["1"] * len(bt["slots"]) + [str(o["lv"]) for o in bt["others"]]) + "};")
        ei = [(len(bt["slots"]) + k, it) for k, it in bt["items"]]
        for _, it in ei:
            if it not in items:
                die(f"unknown item {it}")
        for t in bt["treasures"]:
            if t[2] not in items:
                die(f"{bid}: unknown treasure item {t[2]}")
        c.append(f"static const uint8_t {p}_items[][2] = {{" + (", ".join(f"{{{u}, {i}}}" for u, i in ei) or "{0xFF, 0xFF}") + "};")
        c.append(f"static const uint8_t {p}_ai[] = {{" + ", ".join(map(str, ai_recs(bt["ai_groups"], w))) + "};")
        c.append(f"static const uint8_t {p}_tre[][3] = {{" + (", ".join(f"{{{x}, {y}, {i}}}" for x, y, i in bt["treasures"]) or "{0, 0, 0}") + "};")
        vk, vu, vr = bt["victory"]
        rows[n] = (p, w, h, 15, len(units), len(ei), len(bt["ai_groups"]), len(bt["treasures"]), bt["turn_limit"],
                   CONT[bt["continuity"]], ROUND[bt["round_mode"]], bt["deploy_max"], sum(1 << k for k in bt["forced"]),
                   bt["seed"], VIC[vk], 0xFF if vu is None else vu, vr, 0xFF if bt["protect"] is None else bt["protect"])

    def node(i):
        b = camp_list["battles"][i]
        camp = "0xFF" if b["camp"] is None else str(camp_ids.index(b["camp"]))
        nxt = "0xFF" if b["next"] is None else str(ids.index(b["next"]))
        return camp, nxt, b["title"]
    defs = ["const BattleDef battle_defs[N_BATTLES] = {"]
    cp, nx, ti = node(0)
    defs.append("    {TECH01_W, TECH01_H, TECH01_BANK, tech01_terrain, TECH01_UNITS, tech01_units, tech01_levels, TECH01_START_ITEMS, "
                "tech01_start_items, TECH01_AI_GROUPS, tech01_ai_groups, TECH01_TREASURES, tech01_treasures, 30, 0, 0, "
                f"TECH01_DEPLOY_MAX, 1, 1, {cp}, {nx}, {ti}, 0, 0xFF, 0, 0xFF}},")
    for n in range(1, len(ids)):
        r = rows[n]
        cp, nx, ti = node(n)
        defs.append(f"    {{{r[1]}, {r[2]}, {r[3]}, {r[0]}_terrain, {r[4]}, {r[0]}_units, {r[0]}_levels, {r[5]}, {r[0]}_items, {r[6]}, "
                    f"{r[0]}_ai, {r[7]}, {r[0]}_tre, {r[8]}, {r[9]}, {r[10]}, {r[11]}, {r[12]}, {r[13]}, {cp}, {nx}, {ti}, "
                    f"{r[14]}, {r[15]}, {r[16]}, {r[17]}}},")
    defs.append("};")
    c += defs
    # camps (2.2, C.4)
    cd = []
    for k, cid in enumerate(camp_ids):
        cj = json.loads((ROOT / f"content/camps/{cid}.json").read_text(encoding="utf-8"))
        for i in cj["shop"]:
            if i not in items or items[i]["price"] <= 0:
                die(f"{cid}: shop item {i} unknown or not for sale")
        c.append(f"static const uint8_t camp{k}_shop[] = {{" + ", ".join(map(str, cj["shop"])) + "};")
        c.append(f"static const uint16_t camp{k}_council[][2] = {{" + ", ".join(f"{{{SPK[s]}, {tx}}}" for s, tx in cj["council"]) + "};")
        cd.append(f"{{{len(cj['shop'])}, camp{k}_shop, {len(cj['council'])}, camp{k}_council}}")
    c.append(f"const CampDef camp_defs[N_CAMPS] = {{" + ", ".join(cd) + "};")
    c.append("const RosterInit roster_init[N_ROSTER_INIT] = {")
    for o in roster:
        its = o["items"] + [0xFF] * (8 - len(o["items"]))
        c.append(f"    {{{o['character']}, {o['class']}, {o['level']}, {o['base'][0]}, {o['base'][1]}, {o['base'][2]}, {o['name']}, "
                 "{" + ", ".join(map(str, its)) + "}},")
    c.append("};")
    c.append("const uint8_t char_name[N_CHARS][2] = {" + ", ".join(f"{{{ch['id']}, {ch['name']}}}" for ch in chars) + "};")
    c.append("const uint16_t ending_pages[][2] = {" + ", ".join(f"{{{SPK[s]}, {t}}}" for s, t in camp_list["ending"]) + "};")
    h_ = ["/* GENERATED by tools/gen_campaign.py - do not edit */", "#ifndef CAMPAIGN_DATA_H", "#define CAMPAIGN_DATA_H",
          "#include <stdint.h>", '#include "tech01_data.h"',
          f"#define N_BATTLES {len(ids)}u", f"#define N_CAMPS {len(camp_ids)}u", f"#define N_ROSTER_INIT {len(roster)}u",
          f"#define N_CHARS {len(chars)}u", f"#define N_ENDING_PAGES {len(camp_list['ending'])}u",
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
          "    uint8_t vic_kind, vic_unit, vic_round, protect;                  /* 6.6 objectives (VIC_*) */",
          "} BattleDef;",
          "typedef struct { uint8_t n_shop; const uint8_t *shop; uint8_t n_council; const uint16_t (*council)[2]; } CampDef;",
          "typedef struct { uint16_t character; uint8_t cls, level, str, intl, cmd, name_tx; uint8_t items[8]; } RosterInit;",
          "extern const BattleDef battle_defs[N_BATTLES];", "extern const CampDef camp_defs[N_CAMPS];",
          "extern const RosterInit roster_init[N_ROSTER_INIT];", "extern const uint16_t ending_pages[][2];",
          "extern const uint8_t char_name[N_CHARS][2];   /* character id -> TX name */",
          "extern const uint8_t tech01_levels[];", "#endif"]
    (ROOT / "src/gen/campaign_data.h").write_text("\n".join(h_) + "\n", encoding="utf-8")
    (ROOT / "src/gen/campaign_data.c").write_text("\n".join(c) + "\n", encoding="utf-8")
    (ROOT / "src/gen/campaign_assets.c").write_text("\n".join(asset) + "\n", encoding="utf-8")
    print(f"gen_campaign: {len(ids)} battles, {len(camp_ids)} camps, {len(roster)} officers, {len(chars)} characters")


if __name__ == "__main__":
    main()
