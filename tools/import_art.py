#!/usr/bin/env python3
"""Import the delivered line art (art_src/grok/*.png, 1024 px masters from docs/art_brief.md) into small
4-tone game masters in art_src/v05/ that the generators read (gen_portraits, gen_tech01, gen_actors).

Run by hand after new art arrives (needs macOS `sips` for the first resample and numpy); the build itself only
reads art_src/v05, so it stays reproducible without the 190 MB of originals.

Tones: 0 paper, 1 light (hatching wash), 2 mid, 3 ink. Downscaling keeps ink lines: each target pixel mixes
the block mean with a dark percentile, so a thin stroke still darkens its pixel. Sprites (units, actors) get a
transparency mask from a flood fill of the paper background from the image border; masks are written as
tone 255 (= transparent) in the master.

Targets: bust 64x64, card 144x112, unit 16x21 (+3 clear rows for the foot mark), terrain 16x16, actor 48x64.
"""
import runpy
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from png_shot import write_png  # noqa: E402

SRC, OUT = ROOT / "art_src/grok", ROOT / "art_src/v05"
_read_png = runpy.run_path(str(ROOT / "tools/gen_portraits.py"), run_name="import_art")["read_png"]
GREY = {0: (232, 220, 184), 1: (176, 164, 136), 2: (104, 92, 76), 3: (32, 26, 20), 255: (255, 0, 255)}
TRANSPARENT = 255


def load(path, long_side):
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td) / "r.png"
        subprocess.run(["sips", "-s", "format", "png", "-Z", str(long_side), str(path), "--out", str(tmp)],
                       check=True, capture_output=True)
        w, h, rows = _read_png(tmp)
    a = np.frombuffer(b"".join(rows), dtype=np.uint8).reshape(h, w, 3).astype(np.float32)
    return a.mean(axis=2) / 255.0


def bbox(L, thr=0.80, pad=0.02):
    ys, xs = np.where(L < thr)
    if not len(xs):
        return 0, L.shape[0], 0, L.shape[1]
    py, px = int(L.shape[0] * pad), int(L.shape[1] * pad)
    return max(0, ys.min() - py), min(L.shape[0], ys.max() + py + 1), max(0, xs.min() - px), min(L.shape[1], xs.max() + px + 1)


def fit(L, y0, y1, x0, x1, tw, th, anchor_bottom=False):
    """crop to the box, pad with paper to the target aspect (centred; sprites stand on the bottom edge)"""
    c = L[y0:y1, x0:x1]
    h, w = c.shape
    s = max(w / tw, h / th)
    nw, nh = int(round(tw * s)), int(round(th * s))
    out = np.ones((nh, nw), np.float32)
    oy = nh - h if anchor_bottom else (nh - h) // 2
    ox = (nw - w) // 2
    out[oy:oy + h, ox:ox + w] = c
    return out


def reduce(L, tw, th, dark=0.45, pct=20):
    ys = np.linspace(0, L.shape[0], th + 1).astype(int)
    xs = np.linspace(0, L.shape[1], tw + 1).astype(int)
    o = np.zeros((th, tw), np.float32)
    for j in range(th):
        for i in range(tw):
            b = L[ys[j]:max(ys[j + 1], ys[j] + 1), xs[i]:max(xs[i + 1], xs[i] + 1)]
            o[j, i] = (1 - dark) * b.mean() + dark * np.percentile(b, pct)
    return o


def line_reduce(L, tw, th, ink=0.30, mid=0.14, light=0.86, dark=0.40):
    """small-size reduction by line coverage: a pixel is ink when enough of its block is drawn line,
    mid for sparse hatching, light for a faint wash, else paper (keeps tiles and sprites clean)"""
    ys = np.linspace(0, L.shape[0], th + 1).astype(int)
    xs = np.linspace(0, L.shape[1], tw + 1).astype(int)
    q = np.zeros((th, tw), np.uint8)
    for j in range(th):
        for i in range(tw):
            b = L[ys[j]:max(ys[j + 1], ys[j] + 1), xs[i]:max(xs[i + 1], xs[i] + 1)]
            f = (b < dark).mean()
            q[j, i] = 3 if f > ink else (2 if f > mid else (1 if b.mean() < light else 0))
    return q


def tones(o, t=(0.30, 0.55, 0.80)):
    q = np.zeros(o.shape, np.uint8)
    q[o < t[2]] = 1
    q[o < t[1]] = 2
    q[o < t[0]] = 3
    return q


def background_mask(L, thr=0.82):
    """True where the pixel is paper connected to the border (outside the figure)"""
    h, w = L.shape
    light = L > thr
    m = np.zeros((h, w), bool)
    stack = [(y, x) for x in range(w) for y in (0, h - 1)] + [(y, x) for y in range(h) for x in (0, w - 1)]
    while stack:
        y, x = stack.pop()
        if 0 <= y < h and 0 <= x < w and light[y, x] and not m[y, x]:
            m[y, x] = True
            stack += [(y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)]
    return m


def save(q, name):
    OUT.mkdir(parents=True, exist_ok=True)
    rows = [bytearray(b for v in r for b in GREY[int(v)]) for r in q]
    write_png(OUT / name, q.shape[1], q.shape[0], rows)


def portrait(name, tw, th, long_side, zoom=0.82):
    """busts: a square crop from the top of the drawing, centred on the head (dark pixels of the top third)"""
    L = load(SRC / name, long_side)
    y0, y1, x0, x1 = bbox(L)
    if tw == th:
        side = int(min(x1 - x0, y1 - y0) * zoom)
        top = L[y0:y0 + (y1 - y0) // 3, x0:x1] < 0.6
        cols = np.where(top.any(axis=0))[0]
        cx = x0 + (int(cols.mean()) if len(cols) else (x1 - x0) // 2)
        cx0 = min(max(0, cx - side // 2), L.shape[1] - side)
        y0, y1, x0, x1 = y0, min(L.shape[0], y0 + side), cx0, cx0 + side
    q = tones(reduce(fit(L, y0, y1, x0, x1, tw, th), tw, th, 0.55), (0.36, 0.62, 0.86))
    save(q, name)


def dilate(m, r):
    out = m.copy()
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            out |= np.roll(np.roll(m, dy, 0), dx, 1)
    return out


def sprite(name, tw, th, line=False, pad_bottom=0, thr=(0.10, 0.05, 0.70, 0.60)):
    if line:
        # map units (outline drawings): work at 512 px so thin strokes stay dark, close small gaps in the
        # outline before the background flood fill, keep stroke-only pixels (weapons) even when mostly paper
        L = load(SRC / name, 512)
        y0, y1, x0, x1 = bbox(L, 0.7)
        F = fit(L, y0, y1, x0, x1, tw, th, anchor_bottom=True)
        closed = dilate(F < thr[3], 3)
        bg = background_mask(np.where(closed, 0.0, 1.0), 0.5)
    else:
        L = load(SRC / name, 4 * max(tw, th) * 2)
        y0, y1, x0, x1 = bbox(L)
        F = fit(L, y0, y1, x0, x1, tw, th, anchor_bottom=True)
        bg = background_mask(F)
    ys = np.linspace(0, F.shape[0], th + 1).astype(int)
    xs = np.linspace(0, F.shape[1], tw + 1).astype(int)
    if line:          # line coverage inside, flat light fill, ink outline round the silhouette
        q = line_reduce(F, tw, th, *thr)
        solid = np.array([[bg[ys[j]:ys[j + 1], xs[i]:xs[i + 1]].mean() <= 0.6 or q[j, i] >= 2
                           for i in range(tw)] for j in range(th)])
        q[(q < 2) & solid] = 1
        for j in range(th):
            for i in range(tw):
                if solid[j, i] and any(not (0 <= j + dy < th and 0 <= i + dx < tw) or not solid[j + dy, i + dx]
                                       for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                    q[j, i] = 3
    else:
        solid = np.array([[bg[ys[j]:ys[j + 1], xs[i]:xs[i + 1]].mean() <= 0.55 for i in range(tw)] for j in range(th)])
        q = tones(reduce(F, tw, th, 0.5))
    q[~solid] = TRANSPARENT
    if pad_bottom:    # map units: rows below the feet stay clear for the foot mark (design doc 12.2)
        q = np.vstack([q, np.full((pad_bottom, tw), TRANSPARENT, np.uint8)])
    save(q, name)


def terrain(name, mode):
    L = load(SRC / name, 256)
    if mode == "texture":                       # tileable texture at map scale: sparse, so it can repeat
        n = L.shape[0]
        c = L[n // 2 - 40:n // 2 + 40, n // 2 - 40:n // 2 + 40]
        q = line_reduce(c, 16, 16, 0.45, 0.25, 0.70)
    else:                                       # object: the whole drawing in the cell
        y0, y1, x0, x1 = bbox(L)
        q = line_reduce(fit(L, y0, y1, x0, x1, 16, 16), 16, 16, 0.30, 0.15, 0.80)
    save(q, name)


TERRAIN = {"terrain_plain.png": "texture", "terrain_road.png": "texture", "terrain_river_shallow.png": "texture",
           "terrain_river_deep.png": "texture", "terrain_forest.png": "object", "terrain_hill.png": "object",
           "terrain_cliff.png": "object", "terrain_village.png": "object", "terrain_fort_wall.png": "texture",
           "terrain_fort_gate.png": "object", "terrain_fort_corner.png": "object", "terrain_bridge.png": "object",
           "terrain_camp.png": "object"}


def main():
    names = sorted(p.name for p in SRC.glob("*.png"))
    for n in names:
        if n.endswith("_bust.png"):
            portrait(n, 64, 64, 512)
        elif n.endswith("_card.png"):
            portrait(n, 144, 112, 576)
        elif n.startswith("unit_"):
            sprite(n, 16, 21, line=True, pad_bottom=3)
        elif n.startswith("actor_"):
            sprite(n, 48, 64)
        elif n in TERRAIN:
            terrain(n, TERRAIN[n])
    print(f"import_art: {len(names)} pictures -> {OUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
