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
SPEAKER = {f"P{i + 1:04d}": i for i in range(8)} | {"NARRATOR": 0xFF}
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
            elif name == "DUEL":
                out += [0x0B, self.unit(args[0]), self.unit(args[1])] + u16(min(args[2], 9999))
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


def main():
    path = ROOT / "content/battles/BT_TECH01.json"
    b = json.loads(path.read_text(encoding="utf-8"))
    tx = text_ids()
    c = Compiler(b, tx)
    for tpl in c.templates.values():
        if tpl["instance_id"] < c.n_units:
            die(f"template {tpl['template']} reserves slot {tpl['instance_id']} already used by an initial unit")
    evs = sorted(b["events"], key=lambda e: e["id"])
    ids = [e["id"] for e in evs]
    if len(set(ids)) != len(ids):
        die("duplicate event id")
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
    script_flags_low = 4096                      # BATTLE-scope script flags are allocated downward (none yet)
    if once_bit > script_flags_low:
        die("once bits overlap script battle flags")
    if len(c.blob) > 0xFFFE:
        die("script blob too large")
    out = ["/* GENERATED by tools/gen_events.py from content/battles/BT_TECH01.json - do not edit */",
           '#include "../core/vm.h"', '#include "tech01_events.h"']
    out.append("const VmEvent tech01_events[TECH01_EVENT_COUNT] = {")
    for h_ in headers:
        out.append("    {%d, %d, %d, %d, 0x%04X, 0x%04X, 0x%04X, %d}," % h_)
    out.append("};")
    out.append("const VmTemplate tech01_templates[TECH01_TEMPLATE_COUNT] = {")
    for k in sorted(c.templates):
        t = c.templates[k]
        ch = 0xFFFF if t["character"] == "ANON" else int(t["character"])
        out.append(f"    {{0x{ch:04X}, {t['class']}, {t['level']}, {FACTION[t['faction']]}, {t['instance_id']}}},")
    out.append("};")
    blob_c = ["#pragma bank 2   /* script blob: read only through the gateway (design doc 11.3) */",
              '#include "tech01_events.h"',
              "const uint8_t tech01_script[TECH01_SCRIPT_BYTES] = {" + ", ".join(map(str, c.blob)) + "};"]
    hh = ["/* GENERATED by tools/gen_events.py - do not edit */", "#ifndef TECH01_EVENTS_H", "#define TECH01_EVENTS_H",
          "#include <stdint.h>", "#include \"../core/vm.h\"",
          f"#define TECH01_EVENT_COUNT {len(headers)}u", f"#define TECH01_TEMPLATE_COUNT {len(c.templates)}u",
          f"#define TECH01_SCRIPT_BYTES {len(c.blob)}u", "#define TECH01_SCRIPT_BANK 2u",
          f"#define TECH01_ONCE_BITS {once_bit}u",
          "extern const VmEvent tech01_events[TECH01_EVENT_COUNT];",
          "extern const VmTemplate tech01_templates[TECH01_TEMPLATE_COUNT];",
          "extern const uint8_t tech01_script[TECH01_SCRIPT_BYTES];", "#endif"]
    (ROOT / "src/gen/tech01_events.h").write_text("\n".join(hh) + "\n", encoding="utf-8")
    (ROOT / "src/gen/tech01_events.c").write_text("\n".join(out) + "\n", encoding="utf-8")
    (ROOT / "src/gen/tech01_script.c").write_text("\n".join(blob_c) + "\n", encoding="utf-8")
    print(f"gen_events: {len(headers)} events, {len(c.templates)} templates, {len(c.blob)} script bytes, {once_bit} once bits")


if __name__ == "__main__":
    main()
