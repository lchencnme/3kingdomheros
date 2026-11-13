#!/usr/bin/env python3
"""Portraits (design doc 13.2): 绣像 sheet (v04, painted by tools/paint_portraits.py) -> 64x64 CGB portraits.

Source: art_src/v03/portraits_8_v03.png (320x200, extracted 1:1 from the original design PDF figure 2-1;
the art was authored inside CGB limits: <=4 colours per 8x8 tile). Portrait IDs P0001..P0008 follow
design doc 9.6. Output (asset bank 9) src/gen/portrait_data.c + unbanked index.
"""
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "art_src" / "v04" / "portraits_8_xiuxiang.png"   # 绣像 line art (tools/paint_portraits.py)
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


def main():
    w, h, rows = read_png(SRC)
    if (w, h) != (320, 200):
        die(f"unexpected sheet size {w}x{h}")
    c = [f"#pragma bank {BANK}   /* asset bank: read only through the rom.c gateway */", '#include "portrait_data.h"']
    ci = ['#include "portrait_data.h"']
    npal = []
    for i, (name, (x0, y0)) in enumerate(zip(NAMES, ORIGINS)):
        tiles, attrs, pals, n = portrait(rows, x0, y0, name)
        npal.append(n)
        c.append(f"/* P{i + 1:04d} {name}: {n} palettes */")
        c.append(f"const uint8_t portrait_{i}_tiles[1024] = {{" + ", ".join(map(str, tiles)) + "};")
        c.append(f"const uint8_t portrait_{i}_attr[64] = {{" + ", ".join(map(str, attrs)) + "};")
        c.append(f"const uint16_t portrait_{i}_pal[{MAX_PALS * 4}] = {{" + ", ".join(map(str, pals)) + "};")
        ci += [f"extern const uint8_t portrait_{i}_tiles[1024];", f"extern const uint8_t portrait_{i}_attr[64];",
               f"extern const uint16_t portrait_{i}_pal[{MAX_PALS * 4}];"]
    ci.append("const uint8_t *const portrait_tiles[PORTRAIT_COUNT] = {" + ", ".join(f"portrait_{i}_tiles" for i in range(8)) + "};")
    ci.append("const uint8_t *const portrait_attr[PORTRAIT_COUNT] = {" + ", ".join(f"portrait_{i}_attr" for i in range(8)) + "};")
    ci.append("const uint16_t *const portrait_pal[PORTRAIT_COUNT] = {" + ", ".join(f"portrait_{i}_pal" for i in range(8)) + "};")
    ci.append("const uint8_t portrait_npal[PORTRAIT_COUNT] = {" + ", ".join(map(str, npal)) + "};")
    h_ = ["/* GENERATED by tools/gen_portraits.py - do not edit */", "#ifndef PORTRAIT_DATA_H", "#define PORTRAIT_DATA_H",
          "#include <stdint.h>", f"#define PORTRAIT_BANK {BANK}u", "#define PORTRAIT_COUNT 8u", f"#define PORTRAIT_MAX_PALS {MAX_PALS}u",
          "#define PORTRAIT_NARRATOR 0xFFu",
          "/* 0 P0001 刘备, 1 P0002 关羽, 2 P0003 张飞, 3 P0004 诸葛亮, 4 P0005 赵云, 5 P0006 曹操, 6 P0007 周瑜, 7 P0008 司马懿 */",
          "extern const uint8_t *const portrait_tiles[PORTRAIT_COUNT];", "extern const uint8_t *const portrait_attr[PORTRAIT_COUNT];",
          "extern const uint16_t *const portrait_pal[PORTRAIT_COUNT];", "extern const uint8_t portrait_npal[PORTRAIT_COUNT];", "#endif"]
    (ROOT / "src/gen/portrait_data.h").write_text("\n".join(h_) + "\n", encoding="utf-8")
    (ROOT / "src/gen/portrait_data.c").write_text("\n".join(c) + "\n", encoding="utf-8")
    (ROOT / "src/gen/portrait_index.c").write_text("\n".join(ci) + "\n", encoding="utf-8")
    print(f"gen_portraits: 8 portraits, palettes per portrait {npal}")


if __name__ == "__main__":
    main()
