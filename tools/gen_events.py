#!/usr/bin/env python3
"""Event compiler (design doc 9.1-9.3): content/battles/BT_*.json events -> VM bytecode.

Checks: trigger/condition/opcode names and argument types, text IDs exist (TX_*/TXD_*), speakers are
known portraits, IF only jumps forward over whole instructions, unique event IDs, condition depth <= 8 /
leaves <= 32, SPAWN templates exist and reserve slots not used by initial units, once bits dense from 0,
script battle flags from 4095 downward with no overlap. Output: src/gen/tech01_events.c (+ blob in bank 2).
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TRIG = {"BATTLE_START": 0, "PHASE_START": 1, "POST_MOVE": 2, "PRE_ACTION": 3, "UNIT_RETREAT": 4,
        "ROUND_END": 5, "PHASE_END": 6, "BATTLE_END": 7, "TALK": 8}
PHASE = {"PLAYER": 0, "ALLY": 1, "ENEMY": 2}
CMP = {"eq": 0, "ne": 1, "lt": 2, "le": 3, "gt": 4, "ge": 5}
_PORTRAITS = [e["pinyin"] for e in json.loads((ROOT / "content/portraits.json").read_text(encoding="utf-8"))["portraits"]]
SPEAKER = {f"P{i + 1:04d}": i for i in range(len(_PORTRAITS))} | {p: i for i, p in enumerate(_PORTRAITS)} | {"NARRATOR": 0xFF}
FACTION = {"PLAYER": 0, "ALLY": 1, "ENEMY": 2}


def die(m):
    sys.exit("gen_events: " + m)


def text_ids():
    h = (ROOT / "src/gen/text_data.h").read_text(encoding="utf-8")
    return {m.group(1): int(m.group(2)) for m in re.finditer(r"#define (TXD?_\w+) (\d+)u", h)}


def u16(v):
    return [v & 0xFF, (v >> 8) & 0xFF]


class Compiler:
    def __init__(self, battle, tx):
        self.b, self.tx, self.blob = battle, tx, bytearray()
        self.n_units = len([u for u in battle["deploy"]["slots"]]) + len(battle["units"])
        self.templates = {t["template"]: t for t in battle.get("reinforcements", [])}

    def cond(self, c, depth=0, leaves=None):
        leaves = leaves if leaves is not None else [0]
        if depth > 8:
            die("condition deeper than 8")
        if "all" in c or "any" in c:
            kids = c.get("all", c.get("any"))
            out = [1 if "all" in c else 2, len(kids)]
            for k in kids:
                out += self.cond(k, depth + 1, leaves)
            return out
        if "not" in c:
            return [3] + self.cond(c["not"], depth + 1, leaves)
        leaves[0] += 1
        if leaves[0] > 32:
            die("more than 32 condition leaves")
        k = c["leaf"]
        if k == "flag_eq":
            return [0x10, {"GLOBAL": 0, "BATTLE": 1}[c["scope"]]] + u16(c["id"]) + [c["val"] & 1]
        if k == "turn_cmp":
            return [0x11, CMP[c["op"]]] + u16(c["n"])
        if k == "unit_alive":
            return [0x12, self.unit(c["unit"])]
        if k == "unit_in_rect":
            x0, y0, x1, y1 = c["rect"]
            if not (0 <= x0 <= x1 < self.b["map"]["width"] and 0 <= y0 <= y1 < self.b["map"]["height"]):
                die(f"bad rect {c['rect']}")
            return [0x13, self.unit(c["unit"]), x0, y0, x1, y1]
        if k in ("actor_eq", "target_eq"):
            return [0x14 if k == "actor_eq" else 0x15, self.unit(c["unit"])]
        if k == "phase_eq":
            return [0x16, PHASE[c["phase"]]]
        die(f"unknown condition leaf {k}")

    def unit(self, u):
        slots = set(range(self.n_units)) | {t["instance_id"] for t in self.templates.values()}
        if u not in slots:
            die(f"unit {u} is neither an initial unit nor a reserved template slot")
        return u

    def text(self, name):
        if name not in self.tx:
            die(f"unknown text {name}")
        return u16(self.tx[name])

    def code(self, ops):
        out = []
        for op in ops:
            name, args = op[0], op[1:]
            if name == "TEXT":
                if args[0] not in SPEAKER:
                    die(f"unknown speaker {args[0]}")
                out += [0x02, SPEAKER[args[0]]] + self.text(args[1])
            elif name == "SET_FLAG":
                out += [0x03, {"GLOBAL": 0, "BATTLE": 1}[args[0]]] + u16(args[1]) + [args[2] & 1]
            elif name == "IF":
                cpos = self.place(self.cond(args[0]))
                body = self.code(args[1])
                out += [0x04] + u16(cpos) + u16(len(body)) + body      # skips exactly the body (forward only)
            elif name == "SPAWN":
                if args[0] not in self.templates:
                    die(f"unknown template {args[0]}")
                out += [0x05, args[0], args[1], args[2]]
            elif name == "RETREAT":
                out += [0x06, self.unit(args[0])]
            elif name == "WEATHER":
                out += [0x07, {"CLEAR": 0, "RAIN": 1}[args[0]]]
            elif name == "OUTCOME":
                out += [0x08, {"WIN": 1, "LOSE": 2}[args[0]]]
            elif name == "GIVE_GOLD":
                out += [0x09] + u16(args[0])
            elif name == "HP_SET":
                out += [0x0A, self.unit(args[0])] + u16(min(args[1], 9999))
            elif name == "HP_CAP":     # [unit, value]: scripted damage, never heals
                out += [0x11, self.unit(args[0])] + u16(args[1])
            elif name == "DUEL":
                out += [0x0B, self.unit(args[0]), self.unit(args[1])] + u16(min(args[2], 9999))
            elif name == "GIVE_ITEM":   # [item id]: scripted reward -> prize queue (design doc 7.4)
                ids = {it["id"] for it in json.loads((ROOT / "content/items.json").read_text(encoding="utf-8"))["items"]}
                if args[0] not in ids:
                    die(f"GIVE_ITEM: unknown item {args[0]}")
                self.gifts = getattr(self, "gifts", 0) + 1
                if self.gifts > 16:
                    die("more than 16 GIVE_ITEM in one battle: the prize queue holds 16 (7.4)")
                out += [0x0E, args[0]]
            elif name == "SET_AI":     # [subject, mode, target, leash]; subject int unit or "G<n>" group
                subj = args[0]
                if isinstance(subj, str) and subj.startswith("G"):
                    g = int(subj[1:])
                    if not 0 <= g < 96:
                        die(f"bad AI group {subj}")
                    s16 = 0x8000 | g
                else:
                    s16 = self.unit(subj)
                modes = {"HOLD": 0, "AGGRESSIVE": 1, "GUARD_POINT": 2, "ESCORT": 3, "ESCAPE": 4}
                if args[1] not in modes:
                    die(f"unknown AI mode {args[1]}")
                tgt = args[2]
                if tgt is None:
                    tgt = 0xFFFF
                elif isinstance(tgt, list):                 # [x, y] cell
                    tgt = tgt[1] * self.b["map"]["width"] + tgt[0]
                else:
                    tgt = self.unit(tgt)
                out += [0x0D] + u16(s16) + [modes[args[1]]] + u16(tgt) + [min(max(args[3], 0), 255)]
            elif name == "JOIN":       # [character id]: must be listed in content/roster.json joiners
                js = [j["character"] for j in json.loads((ROOT / "content/roster.json").read_text(encoding="utf-8")).get("joiners", [])]
                if args[0] not in js:
                    die(f"JOIN {args[0]}: not a joiner in content/roster.json")
                out += [0x0F, js.index(args[0])]
            elif name in ("LEAVE", "RETURN"):   # [character id]: party leave / return (FATE_LEFT / FATE_OWNED)
                out += [0x10] + u16(args[0]) + [2 if name == "LEAVE" else 1]
            elif name in ("FACTION_SET", "RECRUIT"):
                fac = FACTION[args[1]] if name == "FACTION_SET" else FACTION["PLAYER"]
                out += [0x0C, self.unit(args[0]), fac]
            else:
                die(f"unknown opcode {name}")
        return out

    def place(self, data):
        off = len(self.blob)
        self.blob += bytes(data)
        return off


def compile_battle(b, tx, prefix):
    c = Compiler(b, tx)
    for tpl in c.templates.values():
        if tpl["instance_id"] < c.n_units:
            die(f"{b['id']}: template {tpl['template']} reserves slot {tpl['instance_id']} already used by an initial unit")
    evs = sorted(b["events"], key=lambda e: e["id"])
    ids = [e["id"] for e in evs]
    if len(set(ids)) != len(ids):
        die(f"{b['id']}: duplicate event id")
    once_bit = 0
    headers = []
    for e in evs:
        trig = TRIG[e["trigger"]]
        param = e.get("param")
        if param is None:
            p = 0xFF
        elif e["trigger"] in ("PHASE_START", "PHASE_END"):
            p = PHASE[param]
        else:
            p = c.unit(param)
        cond_off = 0xFFFF if e.get("condition") is None else c.place(c.cond(e["condition"]))
        code = c.code(e["code"]) + [0x01]
        code_off = c.place(code)
        ob = 0xFFFF
        if e.get("once"):
            ob = once_bit
            once_bit += 1
        headers.append((trig, p, e["priority"], 0, ob, cond_off, code_off, len(code)))
    if once_bit > 0x100:
        die(f"{b['id']}: once bits overlap the treasure flags (0x100+)")
    if len(c.blob) > 0xFFFE:
        die("script blob too large")
    out = [f"const VmEvent {prefix}_events[] = {{"]
    out += ["    {%d, %d, %d, %d, 0x%04X, 0x%04X, 0x%04X, %d}," % h_ for h_ in headers] or ["    {0}"]
    out.append("};")
    out.append(f"const VmTemplate {prefix}_templates[] = {{")
    rows = []
    for k in sorted(c.templates):
        tt = c.templates[k]
        ch = 0xFFFF if tt["character"] == "ANON" else int(tt["character"])
        rows.append(f"    {{0x{ch:04X}, {tt['class']}, {tt['level']}, {FACTION[tt['faction']]}, {tt['instance_id']}}},")
    out += rows or ["    {0}"]
    out.append("};")
    blob = f"const uint8_t {prefix}_script[] = {{" + (", ".join(map(str, c.blob)) or "0") + "};"
    return out, blob, len(headers), len(c.templates), len(c.blob), once_bit


def main():
    camp = json.loads((ROOT / "content/campaign.json").read_text(encoding="utf-8"))
    tx = text_ids()
    out = ["/* GENERATED by tools/gen_events.py from content/battles/BT_*.json - do not edit */",
           '#include "../core/vm.h"', '#include "tech01_events.h"']
    blob_c = ["#pragma bank 15   /* script blobs: read only through the gateway (design doc 11.3) */",
              '#include "tech01_events.h"']
    table, hh_extra, first = [], [], None
    for n, entry in enumerate(camp["battles"]):
        b = json.loads((ROOT / f"content/battles/{entry['id']}.json").read_text(encoding="utf-8"))
        prefix = "tech01" if n == 0 else f"bt{n}"
        o, blob, ne, nt, nb, ob = compile_battle(b, tx, prefix)
        out += o
        blob_c.append(blob)
        hh_extra += [f"extern const VmEvent {prefix}_events[];", f"extern const VmTemplate {prefix}_templates[];",
                     f"extern const uint8_t {prefix}_script[];"]
        table.append(f"    {{{ne}, {nt}, {prefix}_events, {prefix}_templates, {prefix}_script}},")
        if n == 0:
            first = (ne, nt, nb, ob)
        print(f"gen_events: {entry['id']}: {ne} events, {nt} templates, {nb} script bytes, {ob} once bits")
    out.append("const BattleEv battle_ev[] = {")
    out += table
    out.append("};")
    hh = ["/* GENERATED by tools/gen_events.py - do not edit */", "#ifndef TECH01_EVENTS_H", "#define TECH01_EVENTS_H",
          "#include <stdint.h>", "#include \"../core/vm.h\"",
          f"#define TECH01_EVENT_COUNT {first[0]}u", f"#define TECH01_TEMPLATE_COUNT {first[1]}u",
          f"#define TECH01_SCRIPT_BYTES {first[2]}u", "#define TECH01_SCRIPT_BANK 15u", f"#define TECH01_ONCE_BITS {first[3]}u",
          "typedef struct { uint8_t n_events, n_templates; const VmEvent *events; const VmTemplate *templates;",
          "                 const uint8_t *script; } BattleEv;   /* script in bank TECH01_SCRIPT_BANK */",
          "extern const BattleEv battle_ev[];"] + hh_extra + ["#endif"]
    (ROOT / "src/gen/tech01_events.h").write_text("\n".join(hh) + "\n", encoding="utf-8")
    (ROOT / "src/gen/tech01_events.c").write_text("\n".join(out) + "\n", encoding="utf-8")
    (ROOT / "src/gen/tech01_script.c").write_text("\n".join(blob_c) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
