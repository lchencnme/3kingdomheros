#!/usr/bin/env python3
"""Campaign data (design doc 2.1, 2.4, 6.6, 9.4, 15.2): the battle chain, camps, roster, characters, and
the Chapter 1-3 battles BT_01..BT_20 (original maps and dialogue after the public-domain novel).

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
VIC = {"ALL": 0, "DEFEAT": 1, "SURVIVE": 2, "REACH": 3}
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
# ================================================================ Chapter 2 群雄（徐州） BT_06..BT_12
# ---------------------------------------------------------------- BT_06 磐河之战 (回目 7, escort 公孙瓒 / 赵云)
BT06 = {
    "id": "BT_06", "map": [
        "55500000000000676000000555",
        "55000022000000676000000055",
        "50000222200000676000220005",
        "00000022000000676002222000",
        "00000000000000676000220000",
        "00000000000000676000000000",
        "11111111111111999111111111",
        "00000000000000676000000000",
        "00000000000000676000222000",
        "00300000000000666002222200",
        "00000002220000676000222000",
        "50000022222000676000000005",
        "55000002220000676000000055",
        "55500000000000676000000555"],
    "slots": [(3, 5), (3, 7), (4, 6), (2, 6)],
    "others": [U(6, 18, 6, ai=2, lv=6, ch=16, fac="ALLY"),          # 4 公孙瓒 flees west over the bridge
               U(6, 19, 5, ai=2, lv=4, fac="ALLY"),
               U(7, 23, 6, ai=4, lv=7, ch=30),                      # 6 文丑 hunts 公孙瓒
               U(6, 22, 4, ai=4, lv=5), U(6, 22, 8, ai=4, lv=5),
               U(0, 24, 5, lv=5), U(0, 24, 7, lv=5), U(3, 20, 3, lv=5), U(3, 21, 9, lv=5),
               U(1, 7, 6, ai=1, lv=5), U(6, 9, 3, ai=4, lv=5), U(6, 9, 9, ai=4, lv=5)],   # 袁绍's ambush on the west bank
    "templates": [{"template": 0, "character": 17, "class": 15, "level": 8, "faction": "ALLY"}],   # 赵云 (instance 16)
    "ai_groups": [G(0, "AGGRESSIVE"), G(1, "HOLD"), G(2, "ESCAPE", [0, 6], 0), G(3, "ESCORT", 4, 3), G(4, "AGGRESSIVE", 4)],
    "items": [(6, 1)], "treasures": [(2, 9, 0x06)],
    "victory": ("REACH", 4, 0), "area": (0, 4, 1, 8), "protect": 4,
    "turn_limit": 20, "continuity": "NORMAL", "round_mode": "RESET", "deploy_max": 4, "forced": [0], "seed": 19,
    "events": [
        {"id": 1, "trigger": "BATTLE_START", "param": None, "priority": 0, "once": True, "condition": None,
         "code": [["TEXT", "NARRATOR", "TXD_B06_OPEN_0"], ["TEXT", "NARRATOR", "TXD_B06_AMBUSH"], ["TEXT", "P0001", "TXD_B06_OPEN_1"],
                  ["TEXT", "NARRATOR", "TXD_B06_GOAL"]]},
        {"id": 2, "trigger": "PHASE_START", "param": "PLAYER", "priority": 0, "once": True,
         "condition": {"leaf": "turn_cmp", "op": "ge", "n": 2},
         "code": [["TEXT", "NARRATOR", "TXD_B06_ZHAOYUN_1"], ["SPAWN", 0, 17, 9], ["SET_AI", 16, "ESCORT", 4, 3],
                  ["TEXT", "P0005", "TXD_B06_ZHAOYUN_2"], ["SET_FLAG", "GLOBAL", 16, 1]]},
        {"id": 3, "trigger": "UNIT_RETREAT", "param": 6, "priority": 0, "once": True, "condition": None,
         "code": [["TEXT", "NARRATOR", "TXD_B06_WENCHOU"]]},
        {"id": 4, "trigger": "BATTLE_END", "param": None, "priority": 0, "once": True, "condition": {"leaf": "unit_alive", "unit": 0},
         "code": [["TEXT", "NARRATOR", "TXD_B06_WIN_1"], ["TEXT", "P0001", "TXD_B06_WIN_2"], ["GIVE_GOLD", 400], ["GIVE_ITEM", 0x47]]},
    ],
}
# ---------------------------------------------------------------- BT_07 北海救孔融 (回目 11, 太史慈 ally)
BT07 = {
    "id": "BT_07", "map": [
        "00000000000000000000000000",
        "00222000000000000000000000",
        "02222200000088888888800000",
        "00222000000084444444800000",
        "00000000000084400044800000",
        "00000000000084000004800000",
        "11111111111111000004800000",
        "00000000000084000004800000",
        "00000000000084400044800000",
        "00555000000084444444800000",
        "05555500000088888888800000",
        "00555000000000000000000000",
        "00000000222000000000005550",
        "00000002222200000000055555",
        "00000000222000003000005550",
        "00000000000000000000000000"],
    "slots": [(1, 6), (2, 5), (2, 7), (0, 7)],
    "others": [U(1, 10, 6, ai=3, lv=7, ch=19),                      # 4 管亥 besieges the west gate
               U(9, 16, 6, ai=1, lv=4, ch=18, fac="ALLY"),          # 5 孔融 (must survive) in the keep
               U(6, 14, 5, ai=0, lv=7, ch=20, fac="ALLY"),          # 6 太史慈 breaks out to meet us
               U(0, 17, 4, ai=1, lv=3, fac="ALLY"),
               U(0, 9, 4, lv=5), U(0, 9, 8, lv=5), U(3, 6, 3, lv=4), U(3, 6, 9, lv=4),
               U(0, 22, 6, lv=5), U(1, 23, 4, lv=5), U(1, 23, 8, lv=5)],
    "ai_groups": [G(0, "AGGRESSIVE"), G(1, "HOLD"), G(3, "GUARD_POINT", [10, 6], 3)],
    "items": [(4, 0)], "treasures": [(16, 14, 0x04)],
    "victory": ("DEFEAT", 4, 0), "protect": 5,
    "turn_limit": 25, "continuity": "NORMAL", "round_mode": "RESET", "deploy_max": 4, "forced": [0], "seed": 23,
    "events": [
        {"id": 1, "trigger": "BATTLE_START", "param": None, "priority": 0, "once": True, "condition": None,
         "code": [["TEXT", "NARRATOR", "TXD_B07_OPEN_0"], ["TEXT", "NARRATOR", "TXD_B07_OPEN_1"], ["TEXT", "P0001", "TXD_B07_OPEN_2"],
                  ["TEXT", "NARRATOR", "TXD_B07_GOAL"]]},
        {"id": 2, "trigger": "PRE_ACTION", "param": 1, "priority": 0, "once": True, "condition": {"leaf": "target_eq", "unit": 4},
         "code": [["TEXT", "P0002", "TXD_B07_DUEL_1"], ["DUEL", 1, 4, 0], ["TEXT", "NARRATOR", "TXD_B07_DUEL_2"]]},
        {"id": 3, "trigger": "BATTLE_END", "param": None, "priority": 0, "once": True, "condition": {"leaf": "unit_alive", "unit": 0},
         "code": [["TEXT", "NARRATOR", "TXD_B07_WIN_1"], ["TEXT", "P0001", "TXD_B07_WIN_2"], ["GIVE_GOLD", 400], ["GIVE_ITEM", 0x53]]},
    ],
}
# ---------------------------------------------------------------- BT_08 徐州解围 (回目 11, all enemies, 陶谦 protected)
BT08 = {
    "id": "BT_08", "map": [
        "55500000000000000000002220",
        "55000000000000000000022222",
        "50888888880000000000002220",
        "00844444480000000000000000",
        "00844000480000000000000000",
        "00840000080000022200000000",
        "00840000011111122211111111",
        "00840000080000022200000000",
        "00844000480000000000000000",
        "00844444480000000000005550",
        "00888888880000000000055555",
        "00000000000000000000005550",
        "00222000000000000000000000",
        "02222200000300000000000000",
        "00222000000000000000000000",
        "00000000000000000000000000"],
    "slots": [(5, 12), (6, 13), (7, 12), (5, 14)],
    "others": [U(9, 5, 6, ai=1, lv=5, ch=21, fac="ALLY"),           # 4 陶谦 (must survive)
               U(0, 7, 6, ai=2, lv=4, fac="ALLY"), U(0, 7, 4, ai=2, lv=4, fac="ALLY"),
               U(7, 20, 6, lv=8, ch=32),                            # 7 于禁 leads 曹操's vanguard
               U(1, 14, 4, lv=6), U(1, 14, 8, lv=6), U(3, 17, 3, lv=6), U(3, 17, 9, lv=6),
               U(6, 22, 5, lv=6), U(6, 22, 7, lv=6), U(0, 12, 6, lv=6)],
    "ai_groups": [G(0, "AGGRESSIVE"), G(1, "HOLD"), G(2, "GUARD_POINT", [5, 6], 2)],
    "items": [(7, 1)], "treasures": [(3, 4, 0x52)],
    "victory": ("ALL", None, 0), "protect": 4,
    "turn_limit": 25, "continuity": "NORMAL", "round_mode": "RESET", "deploy_max": 4, "forced": [0], "seed": 29,
    "events": [
        {"id": 1, "trigger": "BATTLE_START", "param": None, "priority": 0, "once": True, "condition": None,
         "code": [["TEXT", "NARRATOR", "TXD_B08_OPEN_0"], ["TEXT", "P0003", "TXD_B08_OPEN_1"], ["TEXT", "NARRATOR", "TXD_B08_GOAL"]]},
        {"id": 2, "trigger": "PHASE_START", "param": "ENEMY", "priority": 0, "once": True,
         "condition": {"leaf": "turn_cmp", "op": "ge", "n": 10},
         "code": [["TEXT", "NARRATOR", "TXD_B08_YANZHOU"], ["OUTCOME", "WIN"]]},
        {"id": 3, "trigger": "BATTLE_END", "param": None, "priority": 0, "once": True, "condition": {"leaf": "unit_alive", "unit": 0},
         "code": [["TEXT", "NARRATOR", "TXD_B08_WIN_1"], ["TEXT", "P0001", "TXD_B08_WIN_2"], ["GIVE_GOLD", 500], ["GIVE_ITEM", 2]]},
    ],
}
# ---------------------------------------------------------------- BT_09 盱眙拒袁术 (回目 14, drive off 纪灵)
BT09 = {
    "id": "BT_09", "map": [
        "00000000000000000005555555",
        "00002220000000000005500005",
        "00022222000000000000004440",
        "00002220000000000000004440",
        "00000000006660000000004440",
        "00000000066666000000000000",
        "11111111119999111111111111",
        "00000000066666000000000000",
        "00000000006660000000000000",
        "00222000000000000000222000",
        "02222200000000000002222200",
        "00222000000000000000222000",
        "00000000000000000000000000",
        "00000000000000000000000000"],
    "slots": [(1, 6), (2, 5), (2, 7), (0, 5)],
    "others": [U(2, 23, 3, ai=2, lv=10, ch=22),                     # 4 纪灵 holds the camp
               U(6, 18, 6, ai=0, lv=8, ch=23),                      # 5 荀正 rides ahead
               U(0, 15, 5, lv=7), U(0, 15, 7, lv=7), U(3, 20, 2, lv=7), U(3, 20, 10, lv=7),
               U(6, 22, 8, lv=7), U(6, 22, 5, lv=7), U(1, 24, 4, ai=1, lv=7)],
    "ai_groups": [G(0, "AGGRESSIVE"), G(1, "HOLD"), G(2, "GUARD_POINT", [23, 3], 3)],
    "items": [(4, 2), (5, 0)], "treasures": [(3, 10, 0x1C)],
    "victory": ("DEFEAT", 4, 0), "protect": None,
    "turn_limit": 25, "continuity": "NORMAL", "round_mode": "RESET", "deploy_max": 4, "forced": [0], "seed": 31,
    "events": [
        {"id": 1, "trigger": "BATTLE_START", "param": None, "priority": 0, "once": True, "condition": None,
         "code": [["TEXT", "NARRATOR", "TXD_B09_OPEN_0"], ["TEXT", "P0002", "TXD_B09_OPEN_1"], ["TEXT", "NARRATOR", "TXD_B09_GOAL"]]},
        {"id": 2, "trigger": "PRE_ACTION", "param": 1, "priority": 0, "once": True, "condition": {"leaf": "target_eq", "unit": 5},
         "code": [["TEXT", "P0002", "TXD_B09_DUEL_1"], ["DUEL", 1, 5, 0], ["TEXT", "NARRATOR", "TXD_B09_DUEL_2"]]},
        {"id": 3, "trigger": "BATTLE_END", "param": None, "priority": 0, "once": True, "condition": {"leaf": "unit_alive", "unit": 0},
         "code": [["TEXT", "P0001", "TXD_B09_WIN"], ["GIVE_GOLD", 500]]},
    ],
}
# ---------------------------------------------------------------- BT_10 徐州夜袭 (回目 14, escape with the two ladies)
BT10 = {
    "id": "BT_10", "map": [
        "00000000000000000088888888",
        "00222000000000000084444448",
        "02222200000000000084000048",
        "00222000000000000084000048",
        "00000000000000000084000048",
        "00000000000000000080000008",
        "11111111111111111111000008",
        "00000000000000000080000008",
        "00000000005550000084000048",
        "00000000055555000084000048",
        "00222000005550000084000048",
        "02222200000000000084444448",
        "00222000000000000088888888",
        "00000000000000000000000000"],
    "slots": [(21, 5), (21, 7), (22, 6), (20, 6)],
    "others": [U(17, 23, 5, ai=2, lv=3, ch=28, fac="ALLY"),         # 4 甘夫人: reaching the west exit wins
               U(17, 23, 7, ai=2, lv=3, ch=29, fac="ALLY"),         # 5 糜夫人 (must survive)
               U(0, 24, 3, ai=0, lv=8, ch=31),                      # 6 曹豹 turns traitor inside the city
               U(2, 8, 6, ai=1, lv=10, ch=25),                      # 7 高顺 blocks the west road
               U(6, 10, 5, lv=8), U(6, 10, 7, lv=8), U(0, 14, 4, lv=7), U(0, 14, 8, lv=7), U(3, 6, 3, ai=1, lv=7)],
    "templates": [{"template": 0, "character": 15, "class": 8, "level": 16, "faction": "ENEMY"}],   # 吕布 (instance 13)
    "ai_groups": [G(0, "AGGRESSIVE"), G(1, "HOLD"), G(2, "ESCAPE", [0, 6], 0)],
    "items": [], "treasures": [(19, 2, 0x07)],
    "victory": ("REACH", 4, 0), "area": (0, 5, 1, 7), "protect": 5,
    "turn_limit": 20, "continuity": "NORMAL", "round_mode": "RESET", "deploy_max": 4, "forced": [0, 2], "seed": 37,
    "events": [
        {"id": 1, "trigger": "BATTLE_START", "param": None, "priority": 0, "once": True, "condition": None,
         "code": [["TEXT", "NARRATOR", "TXD_B10_OPEN_0"], ["TEXT", "P0003", "TXD_B10_OPEN_1"], ["TEXT", "P0001", "TXD_B10_OPEN_2"],
                  ["TEXT", "NARRATOR", "TXD_B10_GOAL"]]},
        {"id": 2, "trigger": "PHASE_START", "param": "ENEMY", "priority": 0, "once": True,
         "condition": {"leaf": "turn_cmp", "op": "ge", "n": 3},
         "code": [["TEXT", "NARRATOR", "TXD_B10_LVBU"], ["SPAWN", 0, 25, 13], ["SET_AI", 13, "AGGRESSIVE", 4, 3]]},
        {"id": 3, "trigger": "BATTLE_END", "param": None, "priority": 0, "once": True, "condition": {"leaf": "unit_alive", "unit": 0},
         "code": [["TEXT", "NARRATOR", "TXD_B10_WIN_1"], ["TEXT", "P0003", "TXD_B10_WIN_2"]]},
    ],
}
# ---------------------------------------------------------------- BT_11 辕门射戟 (回目 16, hold 小沛 until 吕布 steps in)
BT11 = {
    "id": "BT_11", "map": [
        "55550000000000000000000000",
        "55000000000000000002220000",
        "50888880000000000022222000",
        "00844480000000000002220000",
        "00840000000000000000000000",
        "00840000000000000000000000",
        "00800001111111111111111111",
        "00840000000000000000000000",
        "00840000000000000000000000",
        "00844480000000000005550000",
        "50888880000000000055555000",
        "55000000000000000005550000",
        "55550000000000000000000000"],
    "slots": [(4, 5), (4, 7), (5, 6), (3, 6)],
    "others": [U(2, 22, 6, ai=1, lv=11, ch=22),                     # 4 纪灵 waits for the vanguard
               U(1, 16, 4, lv=8), U(1, 16, 8, lv=8), U(6, 18, 5, lv=8), U(6, 18, 7, lv=8),
               U(3, 20, 2, lv=8), U(3, 20, 10, lv=8), U(0, 24, 4, lv=8), U(0, 24, 8, lv=8), U(7, 14, 6, lv=9)],
    "ai_groups": [G(0, "AGGRESSIVE"), G(1, "HOLD")],
    "items": [(4, 1)], "treasures": [(3, 3, 0x02)],
    "victory": ("SURVIVE", None, 8), "protect": None,
    "turn_limit": 20, "continuity": "NO_REPLENISH", "round_mode": "RESET", "deploy_max": 4, "forced": [0], "seed": 41,
    "events": [
        {"id": 1, "trigger": "BATTLE_START", "param": None, "priority": 0, "once": True, "condition": None,
         "code": [["TEXT", "NARRATOR", "TXD_B11_OPEN_0"], ["TEXT", "P0002", "TXD_B11_OPEN_1"], ["TEXT", "NARRATOR", "TXD_B11_GOAL"]]},
        {"id": 2, "trigger": "PHASE_START", "param": "ENEMY", "priority": 0, "once": True,
         "condition": {"leaf": "turn_cmp", "op": "ge", "n": 8},
         "code": [["TEXT", "NARRATOR", "TXD_B11_JI_1"], ["TEXT", "NARRATOR", "TXD_B11_JI_2"], ["TEXT", "NARRATOR", "TXD_B11_JI_3"],
                  ["OUTCOME", "WIN"]]},
        {"id": 3, "trigger": "BATTLE_END", "param": None, "priority": 0, "once": True, "condition": {"leaf": "unit_alive", "unit": 0},
         "code": [["TEXT", "P0003", "TXD_B11_WIN_1"], ["TEXT", "P0001", "TXD_B11_WIN_2"], ["GIVE_GOLD", 600]]},
    ],
}
# ---------------------------------------------------------------- BT_12 下邳擒吕布 (回目 19, 曹军 allies, flood)
BT12 = {
    "id": "BT_12", "map": [
        "0000000000000000000000000000",
        "0022200000000000000000000000",
        "0222220000000000888888888800",
        "0022200000000000844444444800",
        "0000000000000000844000044800",
        "0000000006660000840000004800",
        "1111111119991111110000004800",
        "0000000006660000840000004800",
        "0000000000000000844000044800",
        "0055500000000000844444444800",
        "0555550000000000888888888800",
        "0055500000000000000000000000",
        "0000000000002220000000000000",
        "0000000000022222000000000000",
        "0000000000002220000000000000",
        "0000000000000000000000000000"],
    "slots": [(2, 6), (3, 5), (3, 7), (1, 5)],
    "others": [U(8, 21, 6, ai=3, lv=15, ch=15),                     # 4 吕布 in 下邳
               U(2, 18, 6, ai=1, lv=12, ch=25),                     # 5 高顺 holds the gate
               U(7, 20, 4, ai=0, lv=11, ch=26),                     # 6 张辽
               U(10, 23, 7, ai=1, lv=11, ch=24),                    # 7 陈宫
               U(3, 20, 8, ai=1, lv=9), U(0, 14, 4, lv=9), U(0, 14, 8, lv=9), U(6, 11, 3, lv=9),
               U(7, 4, 3, ai=0, lv=12, ch=27, fac="ALLY"),          # 12 曹操
               U(3, 5, 10, ai=0, lv=9, fac="ALLY"), U(1, 6, 12, ai=0, lv=9, fac="ALLY")],
    "ai_groups": [G(0, "AGGRESSIVE"), G(1, "HOLD"), G(3, "GUARD_POINT", [21, 6], 4)],
    "items": [(7, 6)], "treasures": [(13, 13, 0x05)],
    "victory": ("DEFEAT", 4, 0), "protect": None,
    "turn_limit": 30, "continuity": "NORMAL", "round_mode": "RESET", "deploy_max": 4, "forced": [0], "seed": 43,
    "events": [
        {"id": 1, "trigger": "BATTLE_START", "param": None, "priority": 0, "once": True, "condition": None,
         "code": [["TEXT", "NARRATOR", "TXD_B12_OPEN_0"], ["TEXT", "P0006", "TXD_B12_OPEN_1"], ["TEXT", "P0001", "TXD_B12_OPEN_2"],
                  ["TEXT", "NARRATOR", "TXD_B12_GOAL"]]},
        {"id": 2, "trigger": "PHASE_START", "param": "PLAYER", "priority": 0, "once": True,
         "condition": {"leaf": "turn_cmp", "op": "ge", "n": 5},
         "code": [["TEXT", "P0006", "TXD_B12_FLOOD_1"], ["WEATHER", "RAIN"], ["TEXT", "NARRATOR", "TXD_B12_FLOOD_2"]]},
        {"id": 3, "trigger": "UNIT_RETREAT", "param": 6, "priority": 0, "once": True, "condition": None,
         "code": [["TEXT", "P0002", "TXD_B12_ZHANGLIAO"]]},
        {"id": 4, "trigger": "BATTLE_END", "param": None, "priority": 0, "once": True, "condition": {"leaf": "unit_alive", "unit": 0},
         "code": [["TEXT", "NARRATOR", "TXD_B12_WIN_1"], ["TEXT", "P0001", "TXD_B12_WIN_2"], ["GIVE_GOLD", 800], ["GIVE_ITEM", 0x61]]},
    ],
}
# ================================================================ Chapter 3 流离（依袁、依刘） BT_13..BT_20
def EV(i, trig, param, code, cond=None):
    return {"id": i, "trigger": trig, "param": param, "priority": 0, "once": True, "condition": cond, "code": code}
WIN0 = {"leaf": "unit_alive", "unit": 0}
# ---------------------------------------------------------------- BT_13 徐州斩车胄 (回目 21)
BT13 = {
    "id": "BT_13", "map": [
        "000000000000000088888888",
        "002220000000000084444448",
        "022222000000000084000048",
        "002220000000000084000048",
        "000000000000000084000048",
        "000000000000000084000048",
        "111111111111111111000048",
        "000000000000000084000048",
        "000005550000000084000048",
        "000055555000000084000048",
        "000005550000000084444448",
        "000000000000000088888888",
        "000000000000000000000000",
        "000000000000000000000000"],
    "slots": [(1, 6), (2, 5), (2, 7), (0, 5)],
    "others": [U(1, 20, 6, ai=1, lv=12, ch=33),                     # 4 车胄 in the 徐州 keep
               U(0, 18, 4, ai=1, lv=10), U(0, 18, 8, ai=1, lv=10), U(3, 21, 3, ai=1, lv=10), U(3, 21, 9, ai=1, lv=10),
               U(6, 12, 5, lv=10), U(6, 12, 7, lv=10), U(0, 9, 3, lv=10), U(0, 9, 9, lv=10)],
    "ai_groups": [G(0, "AGGRESSIVE"), G(1, "HOLD")],
    "items": [(4, 1)], "treasures": [(5, 9, 0x07)],
    "victory": ("DEFEAT", 4, 0), "protect": None,
    "turn_limit": 25, "continuity": "NORMAL", "round_mode": "RESET", "deploy_max": 4, "forced": [0], "seed": 47,
    "events": [
        EV(1, "BATTLE_START", None, [["TEXT", "NARRATOR", "TXD_B13_OPEN_0"], ["TEXT", "P0001", "TXD_B13_OPEN_1"], ["TEXT", "NARRATOR", "TXD_B13_GOAL"]]),
        {"id": 2, "trigger": "PRE_ACTION", "param": 1, "priority": 0, "once": True, "condition": {"leaf": "target_eq", "unit": 4},
         "code": [["TEXT", "P0002", "TXD_B13_DUEL_1"], ["DUEL", 1, 4, 0], ["TEXT", "NARRATOR", "TXD_B13_DUEL_2"]]},
        EV(3, "BATTLE_END", None, [["TEXT", "P0001", "TXD_B13_WIN"], ["GIVE_GOLD", 600]], WIN0),
    ],
}
# ---------------------------------------------------------------- BT_14 拒刘岱王忠 (回目 22)
BT14 = {
    "id": "BT_14", "map": [
        "00000000000000000000055500",
        "00222000000000000000555550",
        "02222200000000000000055500",
        "00222000000000000000000000",
        "00000000000000022200000000",
        "00000000000000222220000000",
        "11111111111111111111111111",
        "00000000000000222220000000",
        "00000000000000022200000000",
        "00000555000000000000000000",
        "00005555500000000000222000",
        "00000555000000000002222200",
        "00000000000000000000222000",
        "00000000000000000000000000"],
    "slots": [(1, 6), (2, 5), (2, 7), (0, 7)],
    "others": [U(7, 22, 5, lv=12, ch=34), U(6, 22, 8, lv=12, ch=35),    # 4 刘岱, 5 王忠
               U(1, 18, 4, lv=11), U(1, 18, 8, lv=11), U(3, 24, 3, lv=11), U(3, 24, 9, lv=11),
               U(0, 12, 5, lv=11), U(0, 12, 7, lv=11), U(7, 20, 6, lv=11)],
    "ai_groups": [G(0, "AGGRESSIVE"), G(1, "HOLD")],
    "items": [(4, 1), (5, 4)], "treasures": [(6, 10, 0x1E)],
    "victory": ("ALL", None, 0), "protect": None,
    "turn_limit": 25, "continuity": "NORMAL", "round_mode": "RESET", "deploy_max": 4, "forced": [0], "seed": 53,
    "events": [
        EV(1, "BATTLE_START", None, [["TEXT", "NARRATOR", "TXD_B14_OPEN_0"], ["TEXT", "P0003", "TXD_B14_OPEN_1"], ["TEXT", "NARRATOR", "TXD_B14_GOAL"]]),
        EV(2, "UNIT_RETREAT", 4, [["TEXT", "P0003", "TXD_B14_LIUDAI"]]),
        EV(3, "UNIT_RETREAT", 5, [["TEXT", "P0002", "TXD_B14_WANGZHONG"]]),
        EV(4, "BATTLE_END", None, [["TEXT", "P0001", "TXD_B14_WIN_1"], ["TEXT", "NARRATOR", "TXD_B14_WIN_2"], ["GIVE_GOLD", 500]], WIN0),
    ],
}
# ---------------------------------------------------------------- BT_15 小沛之败 (回目 24, escape; 关羽 leaves)
BT15 = {
    "id": "BT_15", "map": [
        "00000000002220000000000000",
        "00000000022222000000000000",
        "00000000002220000000000000",
        "00000000000000000888880000",
        "00022200000000000844480000",
        "00222220000000000840000000",
        "11111111111111111110000000",
        "00222220000000000840000000",
        "00022200000000000844480000",
        "00000000000555000888880000",
        "00000000005555500000000000",
        "00000000000555000000000000",
        "00000000000000000000000000",
        "00000000000000000000000000"],
    "slots": [(19, 5), (19, 7), (20, 6), (18, 6)],
    "others": [U(7, 25, 6, lv=15, ch=27),                           # 4 曹操 attacks 小沛 by night
               U(7, 24, 4, lv=13, ch=26), U(8, 24, 8, lv=14, ch=48),  # 5 张辽, 6 夏侯惇
               U(6, 13, 3, lv=12), U(6, 15, 11, lv=12), U(1, 9, 6, ai=1, lv=12),
               U(3, 6, 3, ai=1, lv=11), U(3, 6, 9, ai=1, lv=11), U(0, 22, 12, lv=11), U(0, 22, 1, lv=11)],
    "ai_groups": [G(0, "AGGRESSIVE"), G(1, "HOLD")],
    "items": [], "treasures": [(12, 10, 0x01)],
    "victory": ("REACH", 0, 0), "area": (0, 0, 1, 2), "protect": None,
    "turn_limit": 20, "continuity": "NO_REPLENISH", "round_mode": "RESET", "deploy_max": 4, "forced": [0], "seed": 59,
    "events": [
        EV(1, "BATTLE_START", None, [["TEXT", "NARRATOR", "TXD_B15_OPEN_0"], ["TEXT", "P0002", "TXD_B15_OPEN_1"], ["TEXT", "P0001", "TXD_B15_OPEN_2"],
                                     ["TEXT", "NARRATOR", "TXD_B15_GOAL"]]),
        EV(2, "BATTLE_END", None, [["TEXT", "NARRATOR", "TXD_B15_WIN_1"], ["TEXT", "NARRATOR", "TXD_B15_WIN_2"], ["LEAVE", 2]], WIN0),
    ],
}
# ---------------------------------------------------------------- BT_16 古城会 (回目 28; 关羽 returns, 赵云 joins)
BT16 = {
    "id": "BT_16", "map": [
        "88888880000000000000000000",
        "84444480000000002220000000",
        "84000480000000022222000000",
        "84000000000000002220000000",
        "84000000000000000000000000",
        "84000000000000000000000555",
        "80001111111111111111111555",
        "84000000000000000000000555",
        "84000000000000000000000000",
        "84000000000002220000000000",
        "84000480000022222000000000",
        "84444480000002220000000000",
        "88888880000000000000000000",
        "00000000000000000000000000"],
    "slots": [(3, 5), (3, 7), (4, 6), (2, 6)],
    "others": [U(6, 14, 6, ai=4, lv=12, ch=2, fac="ALLY"),          # 4 关羽 rides to 古城 (must survive)
               U(7, 22, 6, ai=5, lv=14, ch=36),                     # 5 蔡阳 pursues him
               U(6, 20, 4, ai=5, lv=12), U(6, 20, 8, ai=5, lv=12), U(0, 24, 3, lv=12), U(0, 24, 9, lv=12),
               U(3, 18, 2, ai=1, lv=12), U(3, 18, 10, ai=1, lv=12)],
    "ai_groups": [G(0, "AGGRESSIVE"), G(1, "HOLD"), G(4, "ESCAPE", [4, 6], 0), G(5, "AGGRESSIVE", 4)],
    "items": [(5, 1)], "treasures": [(14, 10, 0x53)],
    "victory": ("DEFEAT", 5, 0), "protect": 4,
    "turn_limit": 25, "continuity": "NORMAL", "round_mode": "RESET", "deploy_max": 4, "forced": [0], "seed": 61,
    "events": [
        EV(1, "BATTLE_START", None, [["TEXT", "NARRATOR", "TXD_B16_OPEN_0"], ["TEXT", "P0003", "TXD_B16_OPEN_1"], ["TEXT", "P0002", "TXD_B16_OPEN_2"],
                                     ["TEXT", "P0003", "TXD_B16_OPEN_3"], ["TEXT", "NARRATOR", "TXD_B16_GOAL"]]),
        {"id": 2, "trigger": "PRE_ACTION", "param": 2, "priority": 0, "once": True, "condition": {"leaf": "target_eq", "unit": 5},
         "code": [["TEXT", "P0002", "TXD_B16_DUEL_1"], ["DUEL", 4, 5, 0], ["TEXT", "NARRATOR", "TXD_B16_DUEL_2"]]},
        EV(3, "BATTLE_END", None, [["TEXT", "P0003", "TXD_B16_WIN_1"], ["TEXT", "P0001", "TXD_B16_WIN_2"], ["RETURN", 2],
                                   ["IF", {"leaf": "flag_eq", "scope": "GLOBAL", "id": 16, "val": 1},
                                    [["TEXT", "P0005", "TXD_B16_ZHAOYUN_1"], ["TEXT", "P0001", "TXD_B16_ZHAOYUN_2"], ["JOIN", 17]]],
                                   ["GIVE_GOLD", 600]], WIN0),
    ],
}
# ---------------------------------------------------------------- BT_17 汝南之战 (回目 31, hold then fall back)
BT17 = {
    "id": "BT_17", "map": [
        "55555000000000000000000000",
        "55500000000000000002220000",
        "50088888000000000022222000",
        "00084448000000000002220000",
        "00084000000000000000000000",
        "00084000000000000000000000",
        "00080001111111111111111111",
        "00084000000000000000000000",
        "00084000000000000000000000",
        "00084448000000000005550000",
        "50088888000000000055555000",
        "55500000000000000005550000",
        "55555000000000000000000000",
        "00000000000000000000000000"],
    "slots": [(5, 4), (5, 8), (6, 5), (6, 7), (4, 6), (5, 6)],
    "others": [U(1, 8, 5, ai=2, lv=10, ch=39, fac="ALLY"), U(0, 8, 7, ai=2, lv=10, ch=40, fac="ALLY"),   # 6 刘辟, 7 龚都
               U(8, 21, 6, lv=15, ch=48), U(7, 20, 4, lv=13, ch=32),                                  # 8 夏侯惇, 9 于禁
               U(1, 16, 3, lv=12), U(1, 16, 9, lv=12), U(3, 18, 2, lv=12), U(3, 18, 10, lv=12),
               U(6, 23, 5, lv=12), U(6, 23, 7, lv=12), U(0, 14, 6, lv=12)],
    "ai_groups": [G(0, "AGGRESSIVE"), G(1, "HOLD"), G(2, "GUARD_POINT", [6, 6], 2)],
    "items": [(8, 2)], "treasures": [(4, 3, 0x05)],
    "victory": ("SURVIVE", None, 6), "protect": None,
    "turn_limit": 20, "continuity": "NORMAL", "round_mode": "RESET", "deploy_max": 5, "forced": [0], "seed": 67,
    "events": [
        EV(1, "BATTLE_START", None, [["TEXT", "NARRATOR", "TXD_B17_OPEN_0"], ["TEXT", "P0001", "TXD_B17_OPEN_1"], ["TEXT", "NARRATOR", "TXD_B17_GOAL"]]),
        EV(2, "PHASE_START", "PLAYER", [["TEXT", "NARRATOR", "TXD_B17_ROUND4"]], {"leaf": "turn_cmp", "op": "ge", "n": 4}),
        EV(3, "BATTLE_END", None, [["TEXT", "P0001", "TXD_B17_WIN_1"], ["TEXT", "NARRATOR", "TXD_B17_WIN_2"], ["GIVE_GOLD", 500]], WIN0),
    ],
}
# ---------------------------------------------------------------- BT_18 江夏讨张武 (回目 34, 的卢)
BT18 = {
    "id": "BT_18", "map": [
        "00000000000000000000000000",
        "00222000000066600000222000",
        "02222200000066600002222200",
        "00222000000066600000222000",
        "00000000000066600000000000",
        "00000000000066600000004440",
        "11111111111199911111114440",
        "00000000000066600000004440",
        "00000000000066600000000000",
        "00055500000066600000000000",
        "00555550000066600002220000",
        "00055500000066600022222000",
        "00000000000066600002220000",
        "00000000000000000000000000"],
    "slots": [(1, 6), (2, 5), (2, 7), (0, 5), (0, 7), (1, 4)],
    "others": [U(7, 23, 6, ai=2, lv=14, ch=41), U(6, 20, 6, lv=13, ch=42),   # 6 张武 (rides 的卢), 7 陈孙
               U(0, 17, 4, lv=12), U(0, 17, 8, lv=12), U(3, 20, 2, lv=12), U(3, 20, 10, lv=12),
               U(6, 9, 3, lv=12), U(6, 9, 9, lv=12), U(1, 24, 8, ai=1, lv=12)],
    "ai_groups": [G(0, "AGGRESSIVE"), G(1, "HOLD"), G(2, "GUARD_POINT", [23, 6], 4)],
    "items": [(7, 1)], "treasures": [(3, 10, 0x52)],
    "victory": ("DEFEAT", 6, 0), "protect": None,
    "turn_limit": 25, "continuity": "NORMAL", "round_mode": "RESET", "deploy_max": 5, "forced": [0], "seed": 71,
    "events": [
        EV(1, "BATTLE_START", None, [["TEXT", "NARRATOR", "TXD_B18_OPEN_0"], ["TEXT", "P0001", "TXD_B18_OPEN_1"], ["TEXT", "NARRATOR", "TXD_B18_GOAL"]]),
        EV(2, "UNIT_RETREAT", 7, [["TEXT", "P0003", "TXD_B18_CHENSUN"]]),
        EV(3, "BATTLE_END", None, [["TEXT", "NARRATOR", "TXD_B18_WIN_1"], ["TEXT", "P0001", "TXD_B18_WIN_2"], ["GIVE_GOLD", 600], ["GIVE_ITEM", 0x60]], WIN0),
    ],
}
# ---------------------------------------------------------------- BT_19 跃马檀溪 (回目 34, 刘备 alone; 徐庶 joins)
BT19 = {
    "id": "BT_19", "map": [
        "000000077700000000005555",
        "000000077700000000055555",
        "000000077700000222000555",
        "000000077700002222200000",
        "000000077700000222000000",
        "000000077700000000000000",
        "000000066611111111111111",
        "000000077700000000000000",
        "000000077700000222000000",
        "000000077700002222200000",
        "000000077700000222000555",
        "000000077700000000055555",
        "000000077700000000005555",
        "000000077700000000000000"],
    "slots": [(22, 6), (21, 5), (21, 7), (23, 5), (23, 7), (20, 6)],
    "others": [U(7, 19, 3, ai=2, lv=13, ch=43),                     # 6 蔡瑁 springs the trap
               U(6, 19, 9, ai=2, lv=12), U(1, 14, 5, ai=2, lv=12), U(1, 14, 7, ai=2, lv=12),
               U(3, 12, 2, lv=11), U(3, 12, 10, lv=11), U(3, 4, 11, ai=1, lv=11)],
    "ai_groups": [G(0, "AGGRESSIVE"), G(1, "HOLD"), G(2, "AGGRESSIVE", 0)],
    "items": [], "treasures": [],
    "victory": ("REACH", 0, 0), "area": (0, 0, 2, 13), "protect": None,
    "turn_limit": 15, "continuity": "NORMAL", "round_mode": "RESET", "deploy_max": 1, "forced": [0], "seed": 73,
    "events": [
        EV(1, "BATTLE_START", None, [["TEXT", "NARRATOR", "TXD_B19_OPEN_0"], ["TEXT", "P0001", "TXD_B19_OPEN_1"], ["TEXT", "NARRATOR", "TXD_B19_GOAL"]]),
        EV(2, "PHASE_START", "PLAYER", [["TEXT", "NARRATOR", "TXD_B19_RIVER"]], {"leaf": "turn_cmp", "op": "ge", "n": 2}),
        EV(3, "BATTLE_END", None, [["TEXT", "NARRATOR", "TXD_B19_WIN_1"], ["TEXT", "NARRATOR", "TXD_B19_WIN_2"], ["TEXT", "P0001", "TXD_B19_WIN_3"],
                                   ["JOIN", 44]], WIN0),
    ],
}
# ---------------------------------------------------------------- BT_20 新野破曹仁 (回目 36, 八门金锁阵; 徐庶 leaves)
BT20 = {
    "id": "BT_20", "map": [
        "00000000000000000000000000",
        "00222000000000000000022200",
        "02222200000000000000222220",
        "00222000000000000000022200",
        "00000000000000000000000000",
        "00000000000000000000000000",
        "00000000000000000000000000",
        "11111111111111111111111111",
        "00000000000000000000000000",
        "00000000000000000000000000",
        "00000000000000000000000000",
        "00555000000000000000005550",
        "05555500000000000000055555",
        "00555000000000000000005550",
        "00000000000000000000000000",
        "00000000000000000000000000"],
    "slots": [(2, 7), (3, 6), (3, 8), (1, 6), (1, 8), (2, 5)],
    "others": [U(12, 17, 7, ai=1, lv=14),                           # 6 阵眼: the formation's flag drummer
               U(0, 15, 5, ai=1, lv=12), U(1, 17, 4, ai=1, lv=12), U(0, 19, 5, ai=1, lv=12), U(1, 20, 7, ai=1, lv=12),
               U(0, 19, 9, ai=1, lv=12), U(1, 17, 10, ai=1, lv=12), U(0, 15, 9, ai=1, lv=12), U(3, 14, 7, ai=1, lv=12),
               U(7, 18, 7, ai=2, lv=15, ch=45), U(2, 23, 7, lv=14, ch=46)],   # 15 曹仁, 16 李典
    "ai_groups": [G(0, "AGGRESSIVE"), G(1, "HOLD"), G(2, "GUARD_POINT", [17, 7], 3)],
    "items": [(15, 2)], "treasures": [(2, 12, 0x1D)],
    "victory": ("DEFEAT", 6, 0), "protect": None,
    "turn_limit": 25, "continuity": "NORMAL", "round_mode": "RESET", "deploy_max": 6, "forced": [0], "seed": 79,
    "events": [
        EV(1, "BATTLE_START", None, [["TEXT", "NARRATOR", "TXD_B20_OPEN_0"], ["TEXT", "NARRATOR", "TXD_B20_OPEN_1"], ["TEXT", "P0001", "TXD_B20_OPEN_2"],
                                     ["TEXT", "NARRATOR", "TXD_B20_GOAL"]]),
        EV(2, "UNIT_RETREAT", 15, [["TEXT", "NARRATOR", "TXD_B20_CAOREN"]]),
        EV(3, "BATTLE_END", None, [["TEXT", "NARRATOR", "TXD_B20_WIN_1"], ["TEXT", "NARRATOR", "TXD_B20_WIN_2"], ["TEXT", "P0001", "TXD_B20_WIN_3"],
                                   ["LEAVE", 44], ["GIVE_GOLD", 800]], WIN0),
    ],
}
EXTRA = [BT01, BT02, BT03, BT04, BT05, BT06, BT07, BT08, BT09, BT10, BT11, BT12, BT13, BT14, BT15, BT16, BT17, BT18, BT19, BT20]


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
    bt_c = ["#pragma bank 14   /* battle tables: read only by flow.c (same bank); others use its BANKED accessors */",
            "/* GENERATED by tools/gen_campaign.py - do not edit */", '#include "campaign_data.h"', '#include "text_data.h"']
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
        units = [(roster[k]["class"] if k < len(roster) else 0, 0, x, y, 0, 0) for k, (x, y) in enumerate(bt["slots"])]
        if not 1 <= bt["deploy_max"] <= min(8, len(bt["slots"])):
            die(f"{bid}: deploy_max must be 1..min(8, slots)")
        if len(bt["slots"]) < len(roster):
            die(f"{bid}: fewer player slots than starting officers")
        for o in bt["others"]:
            if o["ch"] and o["ch"] not in char_ids:
                die(f"{bid}: character {o['ch']} not registered in content/characters.json")
            units.append((o["cls"], FAC[o["fac"]], o["x"], o["y"], o["ai"], o["ch"]))
        for t in bt.get("templates", []):
            if t["character"] not in char_ids:
                die(f"{bid}: template character {t['character']} not registered")
        bt_c.append(f"extern const uint8_t {p}_terrain[];")
        bt_c.append(f"static const Tech01Unit {p}_units[] = {{" + ", ".join("{%d, %d, %d, %d, %d, %d}" % u for u in units) + "};")
        bt_c.append(f"static const uint8_t {p}_levels[] = {{" + ", ".join(["1"] * len(bt["slots"]) + [str(o["lv"]) for o in bt["others"]]) + "};")
        ei = [(len(bt["slots"]) + k, it) for k, it in bt["items"]]
        for _, it in ei:
            if it not in items:
                die(f"unknown item {it}")
        for t in bt["treasures"]:
            if t[2] not in items:
                die(f"{bid}: unknown treasure item {t[2]}")
        bt_c.append(f"static const uint8_t {p}_items[][2] = {{" + (", ".join(f"{{{u}, {i}}}" for u, i in ei) or "{0xFF, 0xFF}") + "};")
        bt_c.append(f"static const uint8_t {p}_ai[] = {{" + ", ".join(map(str, ai_recs(bt["ai_groups"], w))) + "};")
        bt_c.append(f"static const uint8_t {p}_tre[][3] = {{" + (", ".join(f"{{{x}, {y}, {i}}}" for x, y, i in bt["treasures"]) or "{0, 0, 0}") + "};")
        vk, vu, vr = bt["victory"]
        rect = bt.get("area", (0, 0, 0, 0))
        if vk == "REACH" and not (0 <= rect[0] <= rect[2] < w and 0 <= rect[1] <= rect[3] < h):
            die(f"{bid}: bad REACH area {rect}")
        rows[n] = (p, w, h, 15, len(units), len(ei), len(bt["ai_groups"]), len(bt["treasures"]), bt["turn_limit"],
                   CONT[bt["continuity"]], ROUND[bt["round_mode"]], bt["deploy_max"], sum(1 << k for k in bt["forced"]),
                   bt["seed"], VIC[vk], 0xFF if vu is None else vu, vr, 0xFF if bt["protect"] is None else bt["protect"]) + tuple(rect)

    def node(i):
        b = camp_list["battles"][i]
        camp = "0xFF" if b["camp"] is None else str(camp_ids.index(b["camp"]))
        nxt = "0xFF" if b["next"] is None else str(ids.index(b["next"]))
        return camp, nxt, b["title"]
    defs = ["const BattleDef battle_defs[N_BATTLES] = {"]
    cp, nx, ti = node(0)
    defs.append("    {TECH01_W, TECH01_H, TECH01_BANK, tech01_terrain, TECH01_UNITS, tech01_units, tech01_levels, TECH01_START_ITEMS, "
                "tech01_start_items, TECH01_AI_GROUPS, tech01_ai_groups, TECH01_TREASURES, tech01_treasures, 30, 0, 0, "
                f"TECH01_DEPLOY_MAX, 1, 1, {cp}, {nx}, {ti}, 0, 0xFF, 0, 0xFF, 0, 0, 0, 0}},")
    for n in range(1, len(ids)):
        r = rows[n]
        cp, nx, ti = node(n)
        defs.append(f"    {{{r[1]}, {r[2]}, {r[3]}, {r[0]}_terrain, {r[4]}, {r[0]}_units, {r[0]}_levels, {r[5]}, {r[0]}_items, {r[6]}, "
                    f"{r[0]}_ai, {r[7]}, {r[0]}_tre, {r[8]}, {r[9]}, {r[10]}, {r[11]}, {r[12]}, {r[13]}, {cp}, {nx}, {ti}, "
                    f"{r[14]}, {r[15]}, {r[16]}, {r[17]}, {r[18]}, {r[19]}, {r[20]}, {r[21]}}},")
    defs.append("};")
    bt_c += defs
    # camps (2.2, C.4): bank 10 with camp.c, their only reader
    camp_c = ["#pragma bank 10   /* camp tables: read only by camp.c (same bank) */",
              "/* GENERATED by tools/gen_campaign.py - do not edit */", '#include "campaign_data.h"', '#include "text_data.h"']
    cd = []
    for k, cid in enumerate(camp_ids):
        cj = json.loads((ROOT / f"content/camps/{cid}.json").read_text(encoding="utf-8"))
        for i in cj["shop"]:
            if i not in items or items[i]["price"] <= 0:
                die(f"{cid}: shop item {i} unknown or not for sale")
        camp_c.append(f"static const uint8_t camp{k}_shop[] = {{" + ", ".join(map(str, cj["shop"])) + "};")
        camp_c.append(f"static const uint16_t camp{k}_council[][2] = {{" + ", ".join(f"{{{SPK[s]}, {tx}}}" for s, tx in cj["council"]) + "};")
        jn = cj.get("join")
        js = [j["character"] for j in json.loads((ROOT / "content/roster.json").read_text(encoding="utf-8")).get("joiners", [])]
        if jn is not None and jn not in js:
            die(f"{cid}: join {jn} is not a joiner")
        cd.append(f"{{{len(cj['shop'])}, camp{k}_shop, {len(cj['council'])}, camp{k}_council, {0xFF if jn is None else js.index(jn)}}}")
    camp_c.append(f"const CampDef camp_defs[N_CAMPS] = {{" + ", ".join(cd) + "};")
    c.append("const RosterInit roster_init[N_ROSTER_INIT] = {")
    for o in roster:
        its = o["items"] + [0xFF] * (8 - len(o["items"]))
        c.append(f"    {{{o['character']}, {o['class']}, {o['level']}, {o['base'][0]}, {o['base'][1]}, {o['base'][2]}, {o['name']}, "
                 "{" + ", ".join(map(str, its)) + "}},")
    c.append("};")
    joiners = json.loads((ROOT / "content/roster.json").read_text(encoding="utf-8")).get("joiners", [])
    for o in joiners:
        if o["character"] not in char_ids:
            die(f"joiner {o['character']} not registered")
    c.append("const RosterInit join_defs[N_JOINERS + 1] = {")      # +1: never an empty array
    for o in joiners + [{"character": 0, "class": 0, "level": 1, "base": [0, 0, 0], "name": "0", "items": []}]:
        its = o["items"] + [0xFF] * (8 - len(o["items"]))
        c.append(f"    {{{o['character']}, {o['class']}, {o['level']}, {o['base'][0]}, {o['base'][1]}, {o['base'][2]}, {o['name']}, "
                 "{" + ", ".join(map(str, its)) + "}},")
    c.append("};")
    c.append("const uint16_t char_name[N_CHARS][2] = {" + ", ".join(f"{{{ch['id']}, {ch['name']}}}" for ch in chars) + "};")
    c.append("const uint16_t ending_pages[][2] = {" + ", ".join(f"{{{SPK[s]}, {t}}}" for s, t in camp_list["ending"]) + "};")
    h_ = ["/* GENERATED by tools/gen_campaign.py - do not edit */", "#ifndef CAMPAIGN_DATA_H", "#define CAMPAIGN_DATA_H",
          "#include <stdint.h>", '#include "tech01_data.h"',
          f"#define N_BATTLES {len(ids)}u", f"#define N_CAMPS {len(camp_ids)}u", f"#define N_ROSTER_INIT {len(roster)}u",
          f"#define N_CHARS {len(chars)}u",
          f"#define N_JOINERS {len(json.loads((ROOT / 'content/roster.json').read_text(encoding='utf-8')).get('joiners', []))}u", f"#define N_ENDING_PAGES {len(camp_list['ending'])}u",
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
          "    uint8_t vic_x0, vic_y0, vic_x1, vic_y1;                          /* VIC_REACH area */",
          "} BattleDef;",
          "typedef struct { uint8_t n_shop; const uint8_t *shop; uint8_t n_council; const uint16_t (*council)[2]; uint8_t join; } CampDef;   /* join: joiner index or FF */",
          "typedef struct { uint16_t character; uint8_t cls, level, str, intl, cmd; uint16_t name_tx; uint8_t items[8]; } RosterInit;",
          "extern const BattleDef battle_defs[N_BATTLES];   /* ROM bank 14 (flow.c only) */", "extern const CampDef camp_defs[N_CAMPS];          /* ROM bank 10 (camp.c only) */",
          "extern const RosterInit roster_init[N_ROSTER_INIT];",
          "extern const RosterInit join_defs[];             /* OP_JOIN table (content/roster.json joiners) */", "extern const uint16_t ending_pages[][2];",
          "extern const uint16_t char_name[N_CHARS][2];   /* character id -> TX name */",
          "extern const uint8_t tech01_levels[];",
          "#ifdef __SDCC",
          "#include <gb/gb.h>",
          "/* flow.c (bank 14) accessors for code in other banks */",
          "uint8_t bd_treasure(uint8_t i, uint8_t *xyi) BANKED;            /* current battle: x, y, item */",
          "void bd_deploy_rule(uint8_t bt, uint8_t *max, uint8_t *forced) BANKED;",
          "#endif", "#endif"]
    (ROOT / "src/gen/campaign_data.h").write_text("\n".join(h_) + "\n", encoding="utf-8")
    (ROOT / "src/gen/campaign_data.c").write_text("\n".join(c) + "\n", encoding="utf-8")
    (ROOT / "src/gen/campaign_camps.c").write_text("\n".join(camp_c) + "\n", encoding="utf-8")
    (ROOT / "src/gen/campaign_battles.c").write_text("\n".join(bt_c) + "\n", encoding="utf-8")
    (ROOT / "src/gen/campaign_assets.c").write_text("\n".join(asset) + "\n", encoding="utf-8")
    print(f"gen_campaign: {len(ids)} battles, {len(camp_ids)} camps, {len(roster)} officers, {len(chars)} characters")


if __name__ == "__main__":
    main()
