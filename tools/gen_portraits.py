#!/usr/bin/env python3
"""Portraits (design doc 13.2): 64x64 4-tone 绣像 busts (art_src/v05, from tools/import_art.py) -> CGB tiles.

Source: art_src/v03/portraits_8_v03.png (320x200, extracted 1:1 from the original design PDF figure 2-1;
the art was authored inside CGB limits: <=4 colours per 8x8 tile). Portrait IDs P0001..P0008 follow
design doc 9.6. Output (asset bank 9) src/gen/portrait_data.c + unbanked index.
"""
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

NAMES = ["刘备", "关羽", "张飞", "诸葛亮", "赵云", "曹操", "周瑜", "司马懿"]
ORIGINS = [(8, 21), (88, 21), (168, 21), (248, 21), (8, 109), (88, 109), (168, 109), (248, 109)]
MAX_PALS = 5
BANK = 9


def die(m):
    sys.exit("gen_portraits: " + m)


def read_png(path):
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        die("not a PNG")
    pos, idat, w = 8, b"", 0
    while pos < len(data):
        n, = struct.unpack(">I", data[pos:pos + 4])
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
            if depth != 8 or ctype != 2:
                die("only 8-bit RGB PNG supported")
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = w * 3
    out, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - 3] if i >= 3 else 0
            b = prev[i]
            c = prev[i - 3] if i >= 3 else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out.append(bytes(line))
        prev = line
    return w, h, out


def rgb555(c):
    r, g, b = (v >> 3 for v in c)
    return r | (g << 5) | (b << 10)


def portrait(rows, x0, y0, name):
    px = lambda x, y: tuple(rows[y0 + y][(x0 + x) * 3:(x0 + x) * 3 + 3])
    sets = []
    for ty in range(8):
        for tx in range(8):
            s = frozenset(px(tx * 8 + x, ty * 8 + y) for y in range(8) for x in range(8))
            if len(s) > 4:
                die(f"{name}: tile ({tx},{ty}) has {len(s)} colours (>4)")
            sets.append(s)
    pals = []
    for s in sorted(set(sets), key=lambda s: (-len(s), sorted(s))):
        for p in pals:
            if len(p | s) <= 4:
                p |= s
                break
        else:
            pals.append(set(s))
    if len(pals) > MAX_PALS:
        die(f"{name}: needs {len(pals)} palettes (> {MAX_PALS})")
    pals = [sorted(p, key=lambda c: -sum(c)) for p in pals]          # light -> dark inside a palette
    for p in pals:
        while len(p) < 4:
            p.append(p[-1])
    tiles, attrs = [], []
    for k, s in enumerate(sets):
        pi = next(i for i, p in enumerate(pals) if s <= set(p))
        tx, ty = k % 8, k // 8
        data = []
        for y in range(8):
            lo = hi = 0
            for x in range(8):
                v = pals[pi].index(px(tx * 8 + x, ty * 8 + y))
                lo |= (v & 1) << (7 - x)
                hi |= ((v >> 1) & 1) << (7 - x)
            data += [lo, hi]
        tiles += data
        attrs.append(pi)
    pal_words = [rgb555(c) for p in pals for c in p] + [0] * (4 * (MAX_PALS - len(pals)))
    return tiles, attrs, pal_words, len(pals)


FIRST_BANK = 19          # portrait banks 19.. (15 portraits of 1 KiB per bank); the ref table is in bank 19
PER_BANK = 15
PAPER, LIGHT, MID, INK = (232, 220, 184), (176, 164, 136), (104, 92, 76), (32, 26, 20)
# 淡彩: the hatched areas (robes, armour, hair) take a mineral pigment by faction; faces stay paper and ink
TINTS = {"shu": ((176, 204, 160), (64, 112, 88)),        # 石绿 malachite
         "wei": ((168, 192, 216), (56, 84, 136)),        # 花青 indigo
         "wu": ((236, 180, 160), (164, 56, 40)),         # 朱砂 vermilion
         "huangjin": ((232, 204, 120), (160, 116, 32)),  # 藤黄 gamboge
         "dong": ((204, 176, 208), (104, 64, 120)),      # 紫 purple
         "lords": ((212, 184, 148), (136, 92, 56))}      # 赭石 ochre
TINT_ORDER = list(TINTS)
TONE = {PAPER: 0, LIGHT: 1, MID: 2, INK: 3}


def tiles_of(rows, name):
    """64x64 4-tone master -> 64 tiles x 16 B (2bpp), row-major 8 x 8 tiles"""
    out = []
    for ty in range(8):
        for tx in range(8):
            for y in range(8):
                lo = hi = 0
                r = rows[ty * 8 + y]
                for x in range(8):
                    px = tuple(r[(tx * 8 + x) * 3:(tx * 8 + x) * 3 + 3])
                    if px not in TONE:
                        die(f"{name}: colour {px} is not one of the 4 tones (re-run tools/import_art.py)")
                    v = TONE[px]
                    lo |= (v & 1) << (7 - x)
                    hi |= ((v >> 1) & 1) << (7 - x)
                out += [lo, hi]
    return out


def main():
    import json
    import re
    reg = json.loads((ROOT / "content/portraits.json").read_text(encoding="utf-8"))["portraits"]
    tx = {m.group(1): int(m.group(2)) for m in re.finditer(r"#define (TX_\w+) (\d+)u", (ROOT / "src/gen/text_data.h").read_text(encoding="utf-8"))}
    if len(reg) > 254:
        die("more than 254 portraits (speaker byte, 0xFF = narrator)")
    files = {}
    refs = []
    for i, e in enumerate(reg):
        src = ROOT / "art_src/v05" / f"{e['pinyin']}_bust.png"
        if not src.exists():
            die(f"missing {src.relative_to(ROOT)} (tools/import_art.py)")
        w, h, rows = read_png(src)
        if (w, h) != (64, 64):
            die(f"{src.name}: {w}x{h}, expected 64x64")
        bank = FIRST_BANK + i // PER_BANK
        name_tx = tx.get("TX_NAME_" + e["pinyin"].upper())
        if name_tx is None:
            die(f"no TX_NAME_{e['pinyin'].upper()} in content/text/ui_zh.json")
        files.setdefault(bank, []).append(f"/* P{i + 1:04d} {e['name']} */\nconst uint8_t portrait_{i}_tiles[1024] = {{"
                                          + ", ".join(map(str, tiles_of(rows, src.name))) + "};")
        if e.get("tint", "lords") not in TINTS:
            die(f"{e['pinyin']}: unknown tint {e.get('tint')}")
        refs.append((bank, i, name_tx, TINT_ORDER.index(e.get("tint", "lords"))))
    for old in (ROOT / "src/gen").glob("portrait_b*.c"):
        old.unlink()
    for bank, items in files.items():
        lines = [f"#pragma bank {bank}   /* portrait tiles: read only through the rom.c gateway */", '#include "portrait_data.h"'] + items
        if bank == FIRST_BANK:
            lines += [f"extern const uint8_t portrait_{i}_tiles[1024];" for b, i, _, _ in refs]
            lines.append("const PortraitRef portrait_refs[PORTRAIT_COUNT] = {"
                         + ", ".join(f"{{{b}, portrait_{i}_tiles, {n}, {tn}}}" for b, i, n, tn in refs) + "};")
            pal = [rgb555(c) for k in TINT_ORDER for c in (PAPER, TINTS[k][0], TINTS[k][1], INK)]
            lines.append(f"const uint16_t portrait_pal[{len(pal)}] = {{" + ", ".join(map(str, pal)) + "};")
            lines.append("/* woodcut frame round the portrait: 0 corner, 1 top edge, 2 left edge (flipped for the others) */")
            fr = [[0xFF, 0xFF, 0x80, 0x80, 0xBF, 0xBF, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0],
                  [0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00],
                  [0xA0, 0xA0] * 8]
            lines.append("const uint8_t portrait_frame[48] = {" + ", ".join(str(b) for t_ in fr for b in t_) + "};")
        (ROOT / f"src/gen/portrait_b{bank}.c").write_text("\n".join(lines) + "\n", encoding="utf-8")
    for old in ("portrait_data.c", "portrait_index.c"):
        if (ROOT / "src/gen" / old).exists():
            (ROOT / "src/gen" / old).unlink()
    h_ = ["/* GENERATED by tools/gen_portraits.py - do not edit */", "#ifndef PORTRAIT_DATA_H", "#define PORTRAIT_DATA_H",
          "#include <stdint.h>", f"#define PORTRAIT_REF_BANK {FIRST_BANK}u", f"#define PORTRAIT_COUNT {len(reg)}u",
          "#define PORTRAIT_NARRATOR 0xFFu",
          "/* order: content/portraits.json (P0001 刘备 .. P0008 司马懿 first, then by pinyin) */",
          "typedef struct { uint8_t bank; const uint8_t *tiles; uint16_t name_tx; uint8_t tint; } PortraitRef;",
          "extern const PortraitRef portrait_refs[PORTRAIT_COUNT];   /* in PORTRAIT_REF_BANK */",
          "extern const uint16_t portrait_pal[];                     /* in PORTRAIT_REF_BANK: 4 colours per tint */",
          "extern const uint8_t portrait_frame[48];                  /* in PORTRAIT_REF_BANK: corner, top, left */",
          "#endif"]
    (ROOT / "src/gen/portrait_data.h").write_text("\n".join(h_) + "\n", encoding="utf-8")
    print(f"gen_portraits: {len(reg)} portraits in banks {sorted(files)}")


if __name__ == "__main__":
    main()
