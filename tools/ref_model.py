#!/usr/bin/env python3
"""Independent reference model for DEFAULT_D1 rev2 (design doc 第5章, 4.2, 4.4, 附录D).

Written separately from src/core so the C core can be cross-checked:
  python3 tools/ref_model.py > build/host/ref_cases.txt
Each line: KIND <inputs...> = <expected...>
"""
import heapq
import json
import random
import sys
from fractions import Fraction
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
R = json.loads((ROOT / "content" / "default_rules.json").read_text(encoding="utf-8"))
FAM = R["families"]
MT = R["move_types"]
CLS = R["classes"]
TER = R["terrain"]


def floordiv(a, b):
    return a // b  # Python floors (mathematical)


def clamp(v, lo, hi):
    return max(lo, min(hi, v))


def type_pct(fa, fd):
    for a, b in R["type_advantage"]:
        if FAM[fa] == a and FAM[fd] == b:
            return 125
        if FAM[fa] == b and FAM[fd] == a:
            return 80
    return 100


def derive(cls, L, s, i, c):
    k = CLS[cls]
    lv = L - 1
    return dict(
        max_hp=clamp(k["hp"] + 12 * lv + 2 * c, 1, 9999),
        max_mp=clamp(k["mp"] + 2 * lv + i // 5, 0, 999),
        atk=clamp(k["atk"] + 2 * lv + s // 2, 0, 999),
        df=clamp(k["def"] + 2 * lv + c // 2, 0, 999),
        mag=clamp(k["mag"] + 2 * lv + i // 2, 0, 999),
        res=clamp(k["res"] + 2 * lv + i // 3, 0, 999),
        agi=clamp((s + c) // 4 + k["agi"], 0, 100),
        move=clamp(k["move"], 0, 9),
    )


def phys_hit(aa, ad, mor, ter):
    return clamp(85 + floordiv(aa - ad, 2) + mor // 10 - TER[ter]["hit_penalty"], 10, 100)


def phys_dmg(atk, df, hp, mhp, fa, fd, mor, ter, rain, var):
    P = atk * (50 + (50 * hp) // mhp) // 100
    Rr = max(P // 5, 2 * P - df, 1)
    tr = [P, Rr]
    wea = R["rain_bow_percent"] if (rain and FAM[fa] == "bow") else 100
    for m in (type_pct(fa, fd), 75 + mor // 2, 100 - TER[ter]["dr"], wea, var):
        Rr = Rr * m // 100
        tr.append(Rr)
    return clamp(Rr, 1, 9999), tr


def mag_hit(ia, idd, mor):
    return clamp(70 + floordiv(ia - idd, 2) + mor // 10, 5, 100)


def mag_dmg(power, mag, res, mor, elem, var):
    Rr = max(1, power + mag - res // 2)
    tr = [Rr]
    for m in (75 + mor // 2, elem, var):
        Rr = Rr * m // 100
        tr.append(Rr)
    return clamp(Rr, 1, 9999), tr


def xorshift(seed, n):
    x = seed or 0x6D2B79F5
    out = []
    for _ in range(n):
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        out.append(x)
    return out


# ---------- grids ----------
def cost(grid, w, x, y, mt):
    return TER[grid[y * w + x]][MT[mt]]


def move_range(grid, occ, w, h, sx, sy, move, mt, side):
    """Dijkstra limited to budget and +-move box; returns dict cell->dist."""
    move = min(move, 9)
    dist = {(sx, sy): 0}
    pq = [(0, sx, sy)]
    while pq:
        d, x, y = heapq.heappop(pq)
        if d > dist.get((x, y), 1 << 30):
            continue
        for dx, dy in ((0, -1), (-1, 0), (1, 0), (0, 1)):
            nx, ny = x + dx, y + dy
            if not (0 <= nx < w and 0 <= ny < h):
                continue
            if abs(nx - sx) > move or abs(ny - sy) > move:
                continue
            c = cost(grid, w, nx, ny, mt)
            if c == 255:
                continue
            o = occ[ny * w + nx]
            if o and o != side:
                continue
            nd = d + c
            if nd <= move and nd < dist.get((nx, ny), 1 << 30):
                dist[(nx, ny)] = nd
                heapq.heappush(pq, (nd, nx, ny))
    return dist


def backtrack(dist, grid, w, h, sx, sy, ex, ey, mt):
    if (ex, ey) not in dist:
        return []
    path = [(ex, ey)]
    cx, cy = ex, ey
    while (cx, cy) != (sx, sy):
        cc = cost(grid, w, cx, cy, mt)
        cands = []
        for dx, dy in ((0, -1), (-1, 0), (1, 0), (0, 1)):
            px, py = cx + dx, cy + dy
            if (px, py) in dist and dist[(px, py)] + cc == dist[(cx, cy)]:
                cands.append((py * w + px, px, py))
        _, cx, cy = min(cands)
        path.append((cx, cy))
    return [y * w + x for x, y in reversed(path)]


def field(grid, occ, w, h, mt, side, targets):
    """For every cell v: min cost to walk v -> some target, paying entry cost of each entered cell.
    Computed by forward Dijkstra from each standable cell (independent of the C reverse SPFA)."""
    tset = set(targets)
    out = []
    for v in range(w * h):
        vx, vy = v % w, v // w
        if v in tset:
            out.append(0)
            continue
        if cost(grid, w, vx, vy, mt) == 255 or (occ[v] and occ[v] != side):
            out.append(0xFFFF)
            continue
        best = 0xFFFF
        dist = {v: 0}
        pq = [(0, v)]
        while pq:
            d, u = heapq.heappop(pq)
            if d > dist[u]:
                continue
            if u in tset:
                best = d
                break
            ux, uy = u % w, u // w
            for dx, dy in ((0, -1), (-1, 0), (1, 0), (0, 1)):
                nx, ny = ux + dx, uy + dy
                if not (0 <= nx < w and 0 <= ny < h):
                    continue
                n = ny * w + nx
                c = cost(grid, w, nx, ny, mt)
                if c == 255:
                    continue
                if n not in tset and occ[n] and occ[n] != side:
                    continue
                if d + c < dist.get(n, 1 << 30):
                    dist[n] = d + c
                    heapq.heappush(pq, (d + c, n))
        out.append(best)
    return out


def los(grid, w, x0, y0, x1, y1):
    """Closed-square / segment intersection with exact fractions (== supercover)."""
    if abs(x1 - x0) + abs(y1 - y0) <= 1:
        return 1
    ax, ay = Fraction(2 * x0 + 1, 2), Fraction(2 * y0 + 1, 2)
    bx, by = Fraction(2 * x1 + 1, 2), Fraction(2 * y1 + 1, 2)
    for cy in range(min(y0, y1), max(y0, y1) + 1):
        for cx in range(min(x0, x1), max(x0, x1) + 1):
            if (cx, cy) in ((x0, y0), (x1, y1)):
                continue
            if not TER[grid[cy * w + cx]]["blocks_los"]:
                continue
            t0, t1 = Fraction(0), Fraction(1)
            ok = True
            for p, q0, lo, hi in ((bx - ax, ax, cx, cx + 1), (by - ay, ay, cy, cy + 1)):
                if p == 0:
                    if q0 < lo or q0 > hi:
                        ok = False
                    continue
                ta, tb = (lo - q0) / p, (hi - q0) / p
                if ta > tb:
                    ta, tb = tb, ta
                t0, t1 = max(t0, ta), min(t1, tb)
            if ok and t0 <= t1:
                return 0
    return 1


def rand_grid(rng, w, h):
    weights = [30, 8, 10, 3, 3, 8, 5, 4, 6, 2]
    grid = rng.choices(range(10), weights=weights, k=w * h)
    occ = rng.choices([0, 1, 2], weights=[80, 10, 10], k=w * h)
    return grid, occ


def main():
    rng = random.Random(20261108)
    out = []
    # fixed appendix-D vectors are tested directly in C; here: random cross-checks
    for _ in range(1000):
        aa, ad, mor, ter = rng.randint(0, 100), rng.randint(0, 100), rng.randint(0, 100), rng.randint(0, 9)
        out.append(f"PHIT {aa} {ad} {mor} {ter} = {phys_hit(aa, ad, mor, ter)}")
        mhp = rng.randint(1, 9999)
        hp = rng.randint(0, mhp)
        atk, df = rng.randint(0, 999), rng.randint(0, 999)
        fa, fd = rng.randint(0, 7), rng.randint(0, 7)
        rain, var = rng.randint(0, 1), 90 + rng.randint(0, 20)
        d, tr = phys_dmg(atk, df, hp, mhp, fa, fd, mor, ter, rain, var)
        out.append(f"PDMG {atk} {df} {hp} {mhp} {fa} {fd} {mor} {ter} {rain} {var} = {d} {' '.join(map(str, tr))}")
        ia, idd = rng.randint(0, 100), rng.randint(0, 100)
        out.append(f"MHIT {ia} {idd} {mor} = {mag_hit(ia, idd, mor)}")
        pw, mg, rs, el = rng.choice([20, 40, 60, 80, 160]), rng.randint(0, 999), rng.randint(0, 999), rng.choice([100, 125])
        d, tr = mag_dmg(pw, mg, rs, mor, el, var)
        out.append(f"MDMG {pw} {mg} {rs} {mor} {el} {var} = {d} {' '.join(map(str, tr))}")
        cl, L, s, i, c = rng.randint(0, 17), rng.randint(1, 99), rng.randint(0, 100), rng.randint(0, 100), rng.randint(0, 100)
        st = derive(cl, L, s, i, c)
        out.append(f"DERIVE {cl} {L} {s} {i} {c} = {st['max_hp']} {st['max_mp']} {st['atk']} {st['df']} "
                   f"{st['mag']} {st['res']} {st['agi']} {st['move']}")
    out.append("RNG 1 = " + " ".join(map(str, xorshift(1, 5))))
    out.append("RNG 0 = " + " ".join(map(str, xorshift(0, 5))))
    out.append("RNG 123456789 = " + " ".join(map(str, xorshift(123456789, 5))))

    # random grids: move range + backtrack, field, LOS
    for case in range(500):
        w, h = rng.randint(3, 14), rng.randint(3, 14)
        grid, occ = rand_grid(rng, w, h)
        sx, sy = rng.randrange(w), rng.randrange(h)
        grid[sy * w + sx] = 0
        side = rng.choice([1, 2])
        occ[sy * w + sx] = side
        mt, mv = rng.randint(0, 3), rng.randint(0, 9)
        dist = move_range(grid, occ, w, h, sx, sy, mv, mt, side)
        ex, ey = rng.randrange(w), rng.randrange(h)
        bt = backtrack(dist, grid, w, h, sx, sy, ex, ey, mt)
        dists = " ".join(str(dist.get((x, y), 255)) for y in range(h) for x in range(w))
        nt = rng.randint(1, 3)
        targets = sorted(set(rng.randrange(w * h) for _ in range(nt)))
        fl = field(grid, occ, w, h, mt, side, targets)
        lx0, ly0, lx1, ly1 = rng.randrange(w), rng.randrange(h), rng.randrange(w), rng.randrange(h)
        out.append(f"GRID {w} {h} {' '.join(map(str, grid))} OCC {' '.join(map(str, occ))}")
        out.append(f"MOVE {sx} {sy} {mv} {mt} {side} = {dists}")
        out.append(f"PATH {ex} {ey} = {len(bt)} {' '.join(map(str, bt))}")
        out.append(f"FIELD {mt} {side} {len(targets)} {' '.join(map(str, targets))} = {' '.join(map(str, fl))}")
        out.append(f"LOS {lx0} {ly0} {lx1} {ly1} = {los(grid, w, lx0, ly0, lx1, ly1)}")
    sys.stdout.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
