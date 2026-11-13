#!/usr/bin/env python3
"""Procedural 绣像 (Ming-Qing woodblock novel illustration) portraits -> art_src/v04/portraits_8_xiuxiang.png.

Each portrait is a 64x64 bust drawn the way a woodcut is cut: flat regions on paper, every region boundary
inked as a one-pixel contour, then line work (hatching on the robe, brows, eyes, beard strands) in ink.
Four colours per portrait (paper, ink, skin, robe accent), so every 8x8 tile is within the CGB limit and one
palette serves the whole portrait. The sheet keeps the v03 layout (320x200, 8 frames) read by gen_portraits.py.
Identity comes from the novel's descriptions: 刘备 large ears and official cap, 关羽 red face, green kerchief and
long beard, 张飞 dark face with round eyes and bristling whiskers, 诸葛亮 纶巾 and feather fan, 赵云 plumed
helmet, 曹操 tall cap and narrow eyes, 周瑜 young with a jade crown, 司马懿 old with a thin long beard.
"""
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from png_shot import write_png  # noqa: E402

PAPER, INK = (232, 220, 184), (40, 32, 24)
ORIGINS = [(8, 21), (88, 21), (168, 21), (248, 21), (8, 109), (88, 109), (168, 109), (248, 109)]
N = 64
BG, SKIN, ROBE, CAP, HAIR, BEARD, TRIM, FAN = range(8)   # region ids
INKC, PAP, SK, AC = 1, 0, 2, 3                            # colour indices


class Canvas:
    def __init__(self):
        self.r = [[BG] * N for _ in range(N)]      # region map
        self.lines = [[0] * N for _ in range(N)]   # extra ink

    def ellipse(self, cx, cy, rx, ry, reg, y0=0, y1=N):
        for y in range(max(0, y0), min(N, y1)):
            for x in range(N):
                if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1.0:
                    self.r[y][x] = reg

    def poly(self, pts, reg):
        ys = [p[1] for p in pts]
        for y in range(max(0, int(min(ys))), min(N, int(max(ys)) + 1)):
            xs = []
            for (x0, y0), (x1, y1) in zip(pts, pts[1:] + pts[:1]):
                if (y0 <= y + 0.5 < y1) or (y1 <= y + 0.5 < y0):
                    xs.append(x0 + (y + 0.5 - y0) * (x1 - x0) / (y1 - y0))
            xs.sort()
            for a, b in zip(xs[::2], xs[1::2]):
                for x in range(max(0, int(round(a))), min(N, int(round(b)))):
                    self.r[y][x] = reg

    def rect(self, x0, y0, x1, y1, reg):
        for y in range(max(0, y0), min(N, y1)):
            for x in range(max(0, x0), min(N, x1)):
                self.r[y][x] = reg

    def ink(self, x, y):
        if 0 <= x < N and 0 <= y < N:
            self.lines[y][x] = 1

    def line(self, x0, y0, x1, y1):
        n = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
        for i in range(n + 1):
            t = i / n
            self.ink(int(round(x0 + (x1 - x0) * t)), int(round(y0 + (y1 - y0) * t)))

    def curve(self, pts):
        for a, b in zip(pts, pts[1:]):
            self.line(*a, *b)

    def hatch(self, reg, step=3, slope=1, phase=0):
        """woodcut shading: diagonal ink strokes inside one region"""
        for y in range(N):
            for x in range(N):
                if self.r[y][x] == reg and (x + slope * y + phase) % step == 0:
                    self.ink(x, y)

    def render(self, colour_of):
        out = [[PAP] * N for _ in range(N)]
        for y in range(N):
            for x in range(N):
                reg = self.r[y][x]
                out[y][x] = colour_of[reg]
                edge = False
                for dx, dy in ((1, 0), (0, 1), (-1, 0), (0, -1)):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < N and 0 <= ny < N and self.r[ny][nx] != reg and reg != BG and \
                            (self.r[ny][nx] == BG or reg > self.r[ny][nx]):
                        edge = True
                if edge or self.lines[y][x]:
                    out[y][x] = INKC
        for k in range(N):                         # woodblock frame
            out[0][k] = out[N - 1][k] = out[k][0] = out[k][N - 1] = INKC
            out[2][k] = out[N - 3][k] = out[k][2] = out[k][N - 3] = INKC
        return out


def bust(c, robe_open=True):
    """shoulders, robe with crossed collar (交领), neck and head oval"""
    c.poly([(4, 63), (10, 47), (22, 41), (42, 41), (54, 47), (60, 63)], ROBE)
    c.poly([(26, 37), (38, 37), (39, 44), (25, 44)], SKIN)
    if robe_open:
        c.poly([(22, 41), (32, 54), (36, 49), (27, 41)], TRIM)       # left lapel over right
        c.poly([(42, 41), (37, 41), (33, 50), (36, 53)], TRIM)
    c.ellipse(32, 25, 10.5, 13, SKIN)


def eyes(c, y=24, kind="calm"):
    for sx in (-1, 1):
        x = 32 + sx * 5
        if kind == "round":                     # 环眼
            for dx, dy in ((-1, -1), (0, -1), (1, -1), (-2, 0), (2, 0), (-1, 1), (0, 1), (1, 1)):
                c.ink(x + dx, y + dy)
            c.ink(x, y)
        elif kind == "phoenix":                 # 丹凤眼: long, rising at the outer corner
            c.line(x - 2 * sx, y + 1, x + 2 * sx, y - 1)
            c.ink(x, y)
        elif kind == "narrow":
            c.line(x - 2, y, x + 2, y)
        else:
            c.line(x - 2, y, x + 2, y)
            c.ink(x, y + 1)


def brows(c, y=20, kind="plain"):
    for sx in (-1, 1):
        x = 32 + sx * 5
        if kind == "silkworm":                  # 卧蚕眉: thick and curved
            c.curve([(x - 3 * sx, y + 1), (x - sx, y - 1), (x + 2 * sx, y), (x + 3 * sx, y + 1)])
            c.curve([(x - 2 * sx, y + 2), (x, y), (x + 2 * sx, y + 1)])
        elif kind == "fierce":
            c.line(x - 3 * sx, y - 1, x + 3 * sx, y + 2)
            c.line(x - 3 * sx, y, x + 2 * sx, y + 2)
        else:
            c.line(x - 3 * sx, y + 1, x + 2 * sx, y)


def nose_mouth(c, mouth=33):
    c.line(32, 26, 31, 29); c.ink(33, 29)
    c.line(30, mouth, 34, mouth)


def ears(c, big=False):
    ry = 6 if big else 3.5
    for sx in (-1, 1):
        c.ellipse(32 + sx * 11, 27 if big else 25, 2.2, ry, SKIN)


def beard_long(c, length=60, width=7):
    c.poly([(26, 31), (38, 31), (32 + width, length - 6), (32, length), (32 - width, length - 6)], BEARD)
    for k in range(-width + 1, width, 2):                      # cut as strands on paper (woodcut beard)
        c.curve([(32 + k * 0.5, 33), (32 + k * 0.9, (33 + length) // 2), (32 + k * 0.6, length - 2)])


def beard_short(c):
    c.poly([(28, 33), (36, 33), (34, 39), (32, 41), (30, 39)], BEARD)
    c.line(25, 31, 29, 32); c.line(39, 31, 35, 32)            # thin moustache


def portrait(spec):
    c = Canvas()
    bust(c, spec.get("lapel", True))
    if spec.get("big_ears"):
        ears(c, True)
    else:
        ears(c)
    c.ellipse(32, 25, 10.5, 13, SKIN)
    hat = spec["hat"]
    if hat == "official":            # 进贤冠 / 梁冠: stepped crown with a ridge
        c.poly([(21, 18), (22, 10), (42, 10), (43, 18)], CAP)
        c.poly([(26, 10), (28, 4), (36, 4), (38, 10)], CAP)
        c.line(32, 4, 32, 9)
    elif hat == "kerchief":          # 关羽 green 巾
        c.poly([(20, 19), (21, 9), (32, 5), (43, 9), (44, 19)], CAP)
        c.poly([(43, 12), (52, 20), (49, 22), (42, 16)], CAP)     # tail of the kerchief
    elif hat == "helmet":            # 盔 with a ridge and a plume
        c.poly([(19, 20), (20, 9), (32, 4), (44, 9), (45, 20)], CAP)
        c.rect(19, 18, 46, 21, TRIM)
        c.poly([(31, 4), (33, 4), (38, 0), (35, 0)], HAIR)
    elif hat == "lunjin":            # 纶巾 with ribbons
        c.poly([(21, 17), (22, 7), (42, 7), (43, 17)], CAP)
        c.poly([(42, 14), (50, 30), (47, 31), (41, 18)], CAP)
        c.poly([(22, 14), (14, 30), (17, 31), (23, 18)], CAP)
    elif hat == "tall":              # 曹操: tall black cap
        c.poly([(21, 17), (23, 2), (41, 2), (43, 17)], CAP)
    elif hat == "jade":              # 周瑜: small crown with a pin
        c.ellipse(32, 9, 6, 4, CAP)
        c.line(22, 9, 42, 9)
        c.ellipse(32, 15, 10.5, 4, HAIR, 12, 17)
    elif hat == "spikes":            # 张飞: helmet with a spiked ridge
        c.poly([(19, 19), (20, 8), (32, 5), (44, 8), (45, 19)], CAP)
        for x in (24, 28, 32, 36, 40):
            c.line(x, 6, x, 2)
    if spec.get("hair"):
        c.rect(21, 17, 44, 19, HAIR)
    brows(c, kind=spec.get("brows", "plain"))
    eyes(c, kind=spec.get("eyes", "calm"))
    nose_mouth(c)
    beard = spec.get("beard")
    if beard == "long":
        beard_long(c)
    elif beard == "thin":
        beard_long(c, 56, 4)
    elif beard == "short":
        beard_short(c)
    elif beard == "whiskers":        # 虎须: radial bristles round the jaw
        c.poly([(23, 30), (41, 30), (43, 40), (32, 46), (21, 40)], BEARD)
        for a in range(0, 181, 12):
            r = math.radians(a)
            c.line(32 + 7 * math.cos(r), 36 + 4 * math.sin(r), 32 + 12 * math.cos(r), 37 + 9 * math.sin(r))
    if spec.get("fan"):              # 羽扇 in front of the chest
        c.ellipse(46, 50, 7, 9, FAN)
        c.line(46, 58, 44, 63)
        for k in range(-3, 4, 2):
            c.line(46, 57, 46 + k * 2, 43)
    c.hatch(ROBE, step=spec.get("hatch", 3), slope=1)
    if spec.get("armour"):
        c.hatch(ROBE, step=4, slope=-1, phase=2)
    colour_of = {BG: PAP, SKIN: SK, ROBE: AC, CAP: spec.get("cap_col", INKC), HAIR: INKC,
                 BEARD: spec.get("beard_col", PAP), TRIM: PAP, FAN: PAP}
    return c.render(colour_of), spec["skin"], spec["accent"]


SPECS = [
    {"name": "刘备", "hat": "official", "big_ears": True, "beard": "short", "beard_col": INKC, "skin": (232, 184, 136), "accent": (168, 120, 48),
     "cap_col": INKC, "hair": True},
    {"name": "关羽", "hat": "kerchief", "beard": "long", "eyes": "phoenix", "brows": "silkworm",
     "skin": (184, 64, 40), "accent": (56, 112, 72), "cap_col": AC},
    {"name": "张飞", "hat": "spikes", "beard": "whiskers", "eyes": "round", "brows": "fierce",
     "skin": (160, 112, 80), "accent": (72, 64, 88), "armour": True, "beard_col": SK},
    {"name": "诸葛亮", "hat": "lunjin", "beard": "thin", "fan": True, "skin": (232, 192, 144), "accent": (112, 136, 168),
     "cap_col": AC, "hair": True, "hatch": 4},
    {"name": "赵云", "hat": "helmet", "skin": (236, 196, 152), "accent": (136, 152, 176), "cap_col": AC, "armour": True,
     "lapel": False},
    {"name": "曹操", "hat": "tall", "beard": "short", "beard_col": INKC, "eyes": "narrow", "brows": "fierce", "skin": (224, 176, 128),
     "accent": (128, 48, 56)},
    {"name": "周瑜", "hat": "jade", "skin": (240, 200, 160), "accent": (176, 56, 40), "cap_col": AC},
    {"name": "司马懿", "hat": "official", "beard": "thin", "eyes": "narrow", "brows": "fierce", "skin": (216, 172, 128),
     "accent": (48, 56, 88), "hair": True},
]


def main():
    W, H = 320, 200
    sheet = [[PAPER] * W for _ in range(H)]
    for (ox, oy), spec in zip(ORIGINS, SPECS):
        img, skin, acc = portrait(spec)
        cols = {PAP: PAPER, INKC: INK, SK: skin, AC: acc}
        for y in range(N):
            for x in range(N):
                sheet[oy + y][ox + x] = cols[img[y][x]]
    out = ROOT / "art_src/v04/portraits_8_xiuxiang.png"
    out.parent.mkdir(parents=True, exist_ok=True)
    write_png(out, W, H, [bytearray(b for px in r for b in px) for r in sheet])
    big = [[sheet[y // 3][x // 3] for x in range(W * 3)] for y in range(H * 3)]
    write_png(ROOT / "build/art_portraits.png", W * 3, H * 3, [bytearray(b for px in r for b in px) for r in big])
    print("paint_portraits:", out)


if __name__ == "__main__":
    main()
