#!/usr/bin/env python3
"""Title cover (160x112 picture above the two HUD lines): the game's name in doubled font glyphs, a ruled
band, the three sworn brothers (art_src/v05/title_*.png, 48x48 woodcut busts) with their names.
Output: src/gen/title_data.{h,c} (asset bank 26): unique 8x8 tiles + a 20x14 tile map, one 4-tone palette.
"""
import runpy
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from png_shot import write_png  # noqa: E402

W, H, BANK = 160, 112, 26
TONES = [(232, 220, 184), (176, 164, 136), (104, 92, 76), (32, 26, 20)]
# per-tile palettes (GBC): 0 ink on paper (title, frame), 1 石绿 busts, 2 朱砂 seal, 3 赭石 name band
PALS = [TONES,
        [(232, 220, 184), (176, 204, 160), (64, 112, 88), (32, 26, 20)],
        [(232, 220, 184), (240, 176, 150), (190, 48, 32), (32, 26, 20)],
        [(232, 220, 184), (220, 200, 150), (172, 132, 84), (32, 26, 20)]]
gp = runpy.run_path(str(ROOT / "tools/gen_portraits.py"), run_name="gen_title")
gf = runpy.run_path(str(ROOT / "tools/gen_font.py"), run_name="gen_title")


def die(m):
    sys.exit("gen_title: " + m)


def text(img, x, y, s, scale=1, tone=3):
    asc, gl = gf["load_bdf"](gf["BDF"], {ord(c) for c in s})
    for ch in s:
        g = gl.get(ord(ch))
        if g is None:
            die(f"glyph {ch!r} missing in the font")
        rows = gf["raster"](g, asc)
        wd = 6 if g["dw"] <= 6 else 12
        for r in range(12):
            for px in range(wd):
                if rows[r] >> (15 - px) & 1:
                    for dy in range(scale):
                        for dx in range(scale):
                            yy, xx = y + r * scale + dy, x + px * scale + dx
                            if 0 <= yy < H and 0 <= xx < W:
                                img[yy][xx] = tone
        x += wd * scale + (1 if scale > 1 else 0)
    return x


def bust(img, name, x, y):
    w, h, rows = gp["read_png"](ROOT / "art_src/v05" / f"title_{name}.png")
    if (w, h) != (48, 48):
        die(f"title_{name}.png must be 48x48")
    for j in range(h):
        for i in range(w):
            img[y + j][x + i] = TONES.index(tuple(rows[j][i * 3:i * 3 + 3]))
    for i in range(-1, w + 1):                       # woodcut frame
        img[y - 1][x + i] = img[y + h][x + i] = 3
    for j in range(-1, h + 1):
        img[y + j][x - 1] = img[y + j][x + w] = 3


def main():
    img = [[0] * W for _ in range(H)]
    # 回纹 (fret) border, one 8 px tile repeated round the picture, in the ochre pigment with ink rails
    FRET = ["########", ".......#", ".#####.#", ".#...#.#", ".#.#.#.#", ".#.###.#", ".#.....#", ".#######"]
    for ty in range(H // 8):
        for tx in range(W // 8):
            if tx in (0, W // 8 - 1) or ty in (0, H // 8 - 1):
                for y in range(8):
                    for x in range(8):
                        if FRET[y][x] == "#":
                            img[ty * 8 + y][tx * 8 + x] = 2
    for x in range(7, W - 7):                        # inner ink rail
        img[7][x] = img[H - 8][x] = 3
    for y in range(7, H - 7):
        img[y][7] = img[y][W - 8] = 3
    text(img, 11, 10, "三国豪杰传", 2)
    # 朱文印: a vermilion seal with the era cut out in paper, right of the title
    for y in range(10, 34):
        for x in range(137, 151):
            img[y][x] = 2
    seal = [[0] * W for _ in range(H)]
    text(seal, 138, 10, "汉", 1); text(seal, 138, 22, "末", 1)
    for y in range(10, 34):
        for x in range(137, 151):
            if seal[y][x]:
                img[y][x] = 0
    for x in range(12, W - 12):                      # rule under the title: ink over a pigment hairline
        img[36][x] = 3
        img[38][x] = 2
    for k, (nm, label) in enumerate((("guanyu", "关羽"), ("liubei", "刘备"), ("zhangfei", "张飞"))):
        bx = 9 + k * 47
        bust(img, nm, bx, 41)
        for y in range(90, 103):                     # name band: a pale wash under the names
            for x in range(bx, bx + 48):
                if (x + y) % 2 == 0:
                    img[y][x] = 1
        text(img, bx + 12, 90, label)
    tiles, index, tmap, attr = [[0] * 16], {tuple([0] * 16): 0}, [], []
    for ty in range(H // 8):
        for tx in range(W // 8):
            d = []
            for y in range(8):
                lo = hi = 0
                for x in range(8):
                    v = img[ty * 8 + y][tx * 8 + x]
                    lo |= (v & 1) << (7 - x)
                    hi |= ((v >> 1) & 1) << (7 - x)
                d += [lo, hi]
            k = tuple(d)
            if k not in index:
                index[k] = len(tiles)
                tiles.append(d)
            tmap.append(index[k])
            border = tx in (0, 19) or ty in (0, 13)
            attr.append(3 if border else (2 if (tx in (17, 18) and 1 <= ty <= 4) else (1 if 5 <= ty <= 10 else (3 if ty in (11, 12) else 0))))
    if len(tiles) > 255:
        die(f"{len(tiles)} unique tiles > 255 (BG tile ids of VRAM bank 0)")

    def rgb555(c):
        r, g, b = (v >> 3 for v in c)
        return r | (g << 5) | (b << 10)
    h = ["/* GENERATED by tools/gen_title.py - do not edit */", "#ifndef TITLE_DATA_H", "#define TITLE_DATA_H", "#include <stdint.h>",
         f"#define TITLE_BANK {BANK}u", f"#define TITLE_TILES {len(tiles)}u", "#define TITLE_W 20u", "#define TITLE_H 14u",
         f"#define TITLE_PALS {len(PALS)}u",
         "extern const uint8_t title_tiles[];", "extern const uint8_t title_map[];", "extern const uint8_t title_attr[];",
         "extern const uint16_t title_pal[];", "#endif"]
    c = [f"#pragma bank {BANK}   /* title cover: read only through the rom.c gateway */", '#include "title_data.h"',
         "const uint8_t title_tiles[] = {" + ", ".join(str(b) for t in tiles for b in t) + "};",
         "const uint8_t title_map[] = {" + ", ".join(map(str, tmap)) + "};",
         "const uint8_t title_attr[] = {" + ", ".join(map(str, attr)) + "};",
         "const uint16_t title_pal[] = {" + ", ".join(str(rgb555(c_)) for p_ in PALS for c_ in p_) + "};"]
    (ROOT / "src/gen/title_data.h").write_text("\n".join(h) + "\n", encoding="utf-8")
    (ROOT / "src/gen/title_data.c").write_text("\n".join(c) + "\n", encoding="utf-8")
    (ROOT / "build").mkdir(exist_ok=True)
    S = 3
    rows = []
    for y, r in enumerate(img):
        line = bytearray()
        for x, v in enumerate(r):
            line += bytes(PALS[attr[(y // 8) * 20 + x // 8]][v]) * S
        rows += [line] * S
    write_png(ROOT / "build/art_title.png", W * S, H * S, rows)
    print(f"gen_title: {len(tiles)} tiles")


if __name__ == "__main__":
    main()
