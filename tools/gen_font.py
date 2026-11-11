#!/usr/bin/env python3
"""Text + font pipeline (design doc 10.4).

- Scans content/text/*.json plus class/terrain names in content/default_rules.json.
- Assigns stable glyph IDs in content/text/glyph_registry.json (append-only, never renumbered).
- Rasterises only the used glyphs from the OFL Fusion Pixel 12px BDF into 12x12 1bpp
  (12 rows x u16, MSB first) plus a width table (6 = half width, 12 = full width).
- Encodes strings as u16 glyph-ID streams. Control codes: 0xFFF1 = number variable {n},
  0xFFFE = newline, 0xFFFF = end. Unknown characters fail the build (no silent tofu).
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BDF = ROOT / "third_party" / "fonts" / "fusion-pixel-12px-monospaced-zh_hans.bdf"
REG = ROOT / "content" / "text" / "glyph_registry.json"
OUT_H = ROOT / "src" / "gen" / "text_data.h"
OUT_C = ROOT / "src" / "gen" / "text_data.c"

CTRL_PAGE = 0xFFFD
CTRL_VAR = 0xFFF1
CTRL_NL = 0xFFFE
CTRL_END = 0xFFFF


def die(msg):
    sys.exit(f"gen_font: {msg}")


def load_bdf(path, wanted):
    glyphs = {}
    ascent = None
    cur = None
    with open(path, encoding="utf-8") as f:
        for line in f:
            if line.startswith("FONT_ASCENT"):
                ascent = int(line.split()[1])
            elif line.startswith("STARTCHAR"):
                cur = {"bits": []}
            elif cur is not None:
                if line.startswith("ENCODING"):
                    cur["cp"] = int(line.split()[1])
                    if cur["cp"] not in wanted:
                        cur = None
                elif line.startswith("DWIDTH"):
                    cur["dw"] = int(line.split()[1])
                elif line.startswith("BBX"):
                    cur["bbx"] = tuple(int(v) for v in line.split()[1:5])
                elif line.startswith("BITMAP"):
                    cur["in"] = True
                elif line.startswith("ENDCHAR"):
                    glyphs[cur["cp"]] = cur
                    cur = None
                elif cur.get("in"):
                    cur["bits"].append(line.strip())
    return ascent, glyphs


def raster(g, ascent):
    w, h, xo, yo = g["bbx"]
    rows = [0] * 12
    top = ascent - (yo + h)
    for i, hexrow in enumerate(g["bits"]):
        r = top + i
        if not 0 <= r < 12:
            continue
        nbits = len(hexrow) * 4
        v = int(hexrow, 16)
        for x in range(w):
            if (v >> (nbits - 1 - x)) & 1:
                px = xo + x
                if 0 <= px < 12:
                    rows[r] |= 1 << (15 - px)
    return rows


def main():
    texts = {}
    for p in sorted((ROOT / "content" / "text").glob("*.json")):
        if p.name == "glyph_registry.json":
            continue
        for k, v in json.loads(p.read_text(encoding="utf-8")).items():
            if k.startswith("_"):
                continue
            if k in texts:
                die(f"duplicate text id {k}")
            texts[k] = v
    rules = json.loads((ROOT / "content" / "default_rules.json").read_text(encoding="utf-8"))
    for c in rules["classes"]:
        texts[f"TX_CLASS_{c['id']}"] = c["name"]
    for t in rules["terrain"]:
        texts[f"TX_TERRAIN_{t['id']}"] = t["name"]
    for s_ in rules["strategies"]:
        texts[f"TX_STRAT_{s_['id']}"] = s_["name"]
    items = json.loads((ROOT / "content" / "items.json").read_text(encoding="utf-8"))["items"]
    for it in items:
        texts[f"TX_ITEM_{it['id']}"] = it["name"]

    def chars_of(s):
        return re.sub(r"\{n\}", "", s).replace("\n", "")

    used = sorted({ch for s in texts.values() for ch in chars_of(s)} | {" ", "　"})
    reg = json.loads(REG.read_text(encoding="utf-8")) if REG.exists() else {"next": 0, "glyphs": {}}
    for ch in used:
        if ch not in reg["glyphs"]:
            reg["glyphs"][ch] = reg["next"]
            reg["next"] += 1
    if reg["next"] >= CTRL_VAR:
        die("glyph id space exhausted")

    ascent, bdf = load_bdf(BDF, {ord(ch) for ch in reg["glyphs"]})
    missing = [ch for ch in reg["glyphs"] if ord(ch) not in bdf]
    if missing:
        die(f"characters missing from font: {''.join(missing)!r}")

    ids = sorted(reg["glyphs"].items(), key=lambda kv: kv[1])
    data, widths = [], []
    for ch, gid in ids:
        g = bdf[ord(ch)]
        rows = raster(g, ascent)
        for r in rows:
            data += [r >> 8, r & 0xFF]
        widths.append(12 if g["dw"] >= 12 else 6)

    NO_START = set("，。！？；：、）》」』…—")
    NO_END = set("（《「『")
    width = lambda ch: 12 if bdf[ord(ch)]["dw"] >= 12 else 6

    def paginate(s, max_px=144, lines_per_page=2):
        """design doc 10.4: greedy lines <= 144 px; closing punctuation never starts a line and opening
        brackets never end one (back off one character); 2 lines per page"""
        lines, cur, px = [], [], 0
        for ch in s:
            w = width(ch)
            if px + w > max_px and cur:
                carry = []
                if ch in NO_START and len(cur) > 1:
                    carry = [cur.pop()]
                while len(cur) > 1 and cur[-1] in NO_END:
                    carry.insert(0, cur.pop())
                lines.append("".join(cur))
                cur = carry[:]
                px = sum(width(c) for c in cur)
            cur.append(ch)
            px += w
        if cur:
            lines.append("".join(cur))
        for ln in lines:
            if sum(width(c) for c in ln) > max_px:
                die(f"line too wide after breaking: {ln}")
        return [lines[i:i + lines_per_page] for i in range(0, len(lines), lines_per_page)]

    def encode(s, key=""):
        if key.startswith("TXD_"):
            out = []
            for pi, page in enumerate(paginate(s)):
                if pi:
                    out.append(CTRL_PAGE)
                for li, ln in enumerate(page):
                    if li:
                        out.append(CTRL_NL)
                    out += [reg["glyphs"][c] for c in ln]
            out.append(CTRL_END)
            if len(out) > 64:
                die(f"{key}: {len(out)} codes > 64 (split the line into two TEXT entries)")
            return out
        out = []
        i = 0
        while i < len(s):
            if s.startswith("{n}", i):
                out.append(CTRL_VAR)
                i += 3
                continue
            if s[i] == "\n":
                out.append(CTRL_NL)
            else:
                out.append(reg["glyphs"][s[i]])
            i += 1
        out.append(CTRL_END)
        return out

    names = sorted(texts)
    pool, offs = [], []
    for n in names:
        offs.append(len(pool))
        pool += encode(texts[n], n)

    REG.write_text(json.dumps(reg, ensure_ascii=False, indent=1, sort_keys=True), encoding="utf-8")
    h = ["/* GENERATED by tools/gen_font.py - do not edit */",
         "#ifndef TEXT_DATA_H\n#define TEXT_DATA_H\n#include <stdint.h>",
         f"#define GLYPH_COUNT {len(ids)}u", "#define TEXT_BANK 2u",
         f"#define TXC_VAR 0x{CTRL_VAR:04X}u\n#define TXC_NL 0x{CTRL_NL:04X}u\n#define TXC_END 0x{CTRL_END:04X}u\n#define TXC_PAGE 0x{CTRL_PAGE:04X}u",
         f"#define GLYPH_SPACE {reg['glyphs'][' ']}u",
         f"#define GLYPH_DIGIT0 {reg['glyphs']['0']}u"]
    for d in "123456789":
        if reg["glyphs"][d] != reg["glyphs"]["0"] + int(d):
            h.append(f"#define GLYPH_DIGITS_NONCONTIGUOUS 1")
            break
    h += [f"#define GLYPH_{nm} {reg['glyphs'][ch]}u" for nm, ch in
          (("SLASH", "/"), ("PLUS", "+"), ("MINUS", "-"), ("PERCENT", "%"))]
    for i, n in enumerate(names):
        h.append(f"#define {n} {i}u")
    h += [f"#define TEXT_COUNT {len(names)}u",
          "extern const uint8_t glyph_rows[];      /* GLYPH_COUNT x 24 bytes */",
          "extern const uint8_t glyph_width[];",
          "extern const uint16_t text_pool[];", "extern const uint16_t text_offset[];",
          "extern const uint8_t digit_glyph[10];",
          "extern const uint8_t class_name_tx[];", "extern const uint8_t terrain_name_tx[];",
          "extern const uint8_t strat_name_tx[];", "extern const uint8_t item_name_tx[];", "#endif"]
    c = ["#pragma bank 2", "#include \"text_data.h\""]    # asset bank: read through rom.c gateway
    ci = ['#include "text_data.h"']

    def arr(name, typ, vals, per=16):
        lines = [f"const {typ} {name}[] = {{"]
        for i in range(0, len(vals), per):
            lines.append("    " + ", ".join(str(v) for v in vals[i:i + per]) + ",")
        lines.append("};")
        return lines

    c += arr("glyph_rows", "uint8_t", data, 24)
    c += arr("text_pool", "uint16_t", pool)
    ci += arr("glyph_width", "uint8_t", widths, 32)
    ci += arr("text_offset", "uint16_t", offs + [len(pool)])
    ci.append("const uint8_t digit_glyph[10] = {" + ", ".join(str(reg["glyphs"][d]) for d in "0123456789") + "};")
    ci.append("const uint8_t class_name_tx[] = {" + ", ".join(str(names.index(f"TX_CLASS_{c_['id']}")) for c_ in rules["classes"]) + "};")
    ci.append("const uint8_t strat_name_tx[] = {" + ", ".join(str(names.index(f"TX_STRAT_{s_['id']}")) for s_ in rules["strategies"]) + "};")
    ibyid = {it["id"]: it for it in items}
    ci.append("const uint8_t item_name_tx[] = {" + ", ".join(str(names.index(f"TX_ITEM_{i}")) if i in ibyid else "0"
                                                       for i in range(max(ibyid) + 1)) + "};")
    ci.append("const uint8_t terrain_name_tx[] = {" + ", ".join(str(names.index(f"TX_TERRAIN_{t_['id']}")) for t_ in rules["terrain"]) + "};")
    OUT_H.write_text("\n".join(h) + "\n", encoding="utf-8")
    OUT_C.write_text("\n".join(c) + "\n", encoding="utf-8")
    (ROOT / "src" / "gen" / "text_index.c").write_text("\n".join(ci) + "\n", encoding="utf-8")
    print(f"gen_font: {len(ids)} glyphs ({len(data)} bytes), {len(names)} strings")


if __name__ == "__main__":
    main()
