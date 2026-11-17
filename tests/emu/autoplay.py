#!/usr/bin/env python3
"""Autoplay: a bot plays the whole campaign by the normal rules, with real key presses only, from a new game
to an ending, and records every battle (outcome, rounds, retreats, gold, retries).

usage: python3 tests/emu/autoplay.py build/gb/sanguo_hjz.gbc [report.json] [--max-battles N] [--tries N]

The bot reads RAM (units, live terrain, the move range the game computed) and never writes it. Per player
phase every officer, leader last:
  - the game's own move range lists the cells it can reach; for each cell the bot scores the best attack
    from there (expected damage, kill, the objective general, counter damage, the danger of the cell) and
    the plain move (path distance to its goal over the live terrain, danger);
  - goals: the escort area for 刘备 in REACH battles, otherwise the nearest enemy (the objective general
    first); 刘备 and wounded officers keep out of cells where the enemy could finish them.
A lost battle is retried (A on the result page) up to --tries times; after that the bot forces the win so
the campaign data behind it can still be measured, and the battle is marked as not won.
"""
import heapq
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve()
_src = (HERE.parent / "test_rom.py").read_text().splitlines()
_cut = next(k for k, l in enumerate(_src) if l.startswith("# ------------------------------------------------------------------ basic flow"))
sys.argv = [sys.argv[0]] + sys.argv[1:]
_args = [a for a in sys.argv[1:] if not a.startswith("--")]
_opts = {a.split("=")[0]: a.split("=")[1] if "=" in a else "1" for a in sys.argv[1:] if a.startswith("--")}
sys.argv = [sys.argv[0], _args[0] if _args else "build/gb/sanguo_hjz.gbc"]
exec("\n".join(_src[:_cut]))
for _name in ("def win_now", "def outcome_ready", "def through_camp", "def camp_wait", "def camp_cur"):
    _i = next(k for k, l in enumerate(_src) if l.startswith(_name)); _j = _i + 1
    while _j < len(_src) and (_src[_j].startswith(" ") or not _src[_j].strip()):
        _j += 1
    exec("\n".join(_src[_i:_j]))
C_MAIN = 20

ROOT = HERE.parents[2]
RULES = json.load(open(ROOT / "content/default_rules.json"))
CLS = RULES["classes"]
TER = RULES["terrain"]
INF = 255
G_DANGER = 17
FAC_PLAYER, FAC_ALLY, FAC_ENEMY = 0, 1, 2
CASTERS = {9, 10, 11, 16}
TRACE = False
CALIBRATE = False
import random
RNG = random.Random(1)
JITTER = 0.0
PLANS = 1
PROFILE = (1.0, 0.0, 1)
# whole-battle tries: (danger weight, score jitter, phase variants per round with savestate lookahead)
PROFILES = [(1.0, 0.0, 1), (5.0, 0.0, 1), (0.3, 0.0, 1), (1.0, 0.0, 6), (2.0, 40.0, 1), (0.5, 40.0, 1),
            (3.0, 0.0, 6), (0.3, 30.0, 6), (1.0, 80.0, 1), (8.0, 20.0, 1)]
AGGR = 1.0                                    # danger weight multiplier of the current plan


def battle_rows():
    """objective data per battle from the generated table (vic kind/unit, protect, area)"""
    txt = (ROOT / "src/gen/campaign_battles.c").read_text()
    body = txt[txt.index("battle_defs[N_BATTLES] = {"):]
    rows = []
    for line in body.splitlines()[1:]:
        line = line.strip()
        if not line.startswith("{"):
            break
        f = [x.strip() for x in line.strip("{},").split(",")]
        I = lambda s: int(s, 0)
        rows.append(dict(kind=I(f[-12]), unit=I(f[-11]), protect=I(f[-9]),
                         area=(I(f[-8]), I(f[-7]), I(f[-6]), I(f[-5]))))
    return rows


ROWS = battle_rows()
TURN_LIMIT = {k: json.load(open(ROOT / f"content/battles/{b['id']}.json"))["turn_limit"]
              for k, b in enumerate(json.load(open(ROOT / "content/campaign.json"))["battles"])}
_UI = json.load(open(ROOT / "content/text/ui_zh.json"))
TITLES = [_UI.get(b.get("title", ""), b["id"]) for b in json.load(open(ROOT / "content/campaign.json"))["battles"]]
VIC_ALL, VIC_DEFEAT, VIC_SURVIVE, VIC_REACH = 0, 1, 2, 3


class Unit:
    __slots__ = ("i", "ch", "fac", "cls", "lv", "x", "y", "move", "hp", "mhp", "mp", "atk", "df", "flags")

    def __init__(self, em, i):
        a = 0xD000 + 40 * i
        m = em.m
        rd16 = lambda o: m[1, a + o] | (m[1, a + o + 1] << 8)
        self.i = i; self.ch = rd16(0); self.fac = m[1, a + 3]; self.cls = m[1, a + 4]; self.lv = m[1, a + 5]
        self.x = m[1, a + 7]; self.y = m[1, a + 8]; self.move = m[1, a + 9]
        self.hp = rd16(10); self.mhp = rd16(12); self.mp = rd16(14); self.atk = rd16(18); self.df = rd16(20)
        self.flags = m[1, a + 34]

    @property
    def alive(self):
        return self.hp > 0 and not (self.flags & 0x38)

    @property
    def rng(self):
        return CLS[self.cls]["range"] if self.cls < len(CLS) else [1, 1]

    @property
    def mtype(self):
        return CLS[self.cls]["move_type"] if self.cls < len(CLS) else "foot"


def units(em):
    return [Unit(em, i) for i in range(24)]


def est_dmg(a, d):
    """rough damage of a on d (the game's formula without type, morale and terrain): attack scales with hp"""
    p = a.atk * (0.5 + 0.5 * a.hp / max(1, a.mhp))
    return max(2 * p - d.df, p / 5)


class Board:
    def __init__(self, em):
        self.w = em.r8("_grid_w"); self.h = em.r8("_grid_h")
        self.t = [em.m[2, 0xD000 + k] & 0x0F for k in range(self.w * self.h)]

    def cost(self, x, y, mtype):
        t = self.t[y * self.w + x]
        return TER[t][mtype] if t < len(TER) else INF

    def field(self, goals, mtype):
        """path cost from every cell to the nearest goal cell over the live terrain (units ignored)"""
        d = [10 ** 6] * (self.w * self.h)
        pq = []
        for x, y in goals:
            if 0 <= x < self.w and 0 <= y < self.h:
                d[y * self.w + x] = 0; pq.append((0, x, y))
        heapq.heapify(pq)
        while pq:
            c, x, y = heapq.heappop(pq)
            if c > d[y * self.w + x]:
                continue
            for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                if 0 <= nx < self.w and 0 <= ny < self.h:
                    # cost of stepping toward the goal = entering (x, y); a goal cell itself never blocks
                    # (an enemy in a forest is attacked from next to it, cavalry need not enter)
                    k = 1 if c == 0 else self.cost(x, y, mtype)
                    if k >= INF:
                        continue
                    nc = c + k
                    if nc < d[ny * self.w + nx]:
                        d[ny * self.w + nx] = nc; heapq.heappush(pq, (nc, nx, ny))
        return d


def move_range(em):
    """cells the selected unit can reach (the game's MoveRange in WRAM7 D600)"""
    s8 = lambda v: v - 256 if v > 127 else v
    bx, by = s8(em.m[7, 0xD600]), s8(em.m[7, 0xD601])
    out = []
    for ly in range(19):
        for lx in range(19):
            if em.m[7, 0xD607 + ly * 19 + lx] != INF:
                out.append((bx + lx, by + ly))
    return out


def danger(cell, foes):
    x, y = cell
    s = 0
    for e in foes:
        r = e.move + e.rng[1]
        if abs(e.x - x) + abs(e.y - y) <= r:
            s += est_dmg(e, _dummy_target)
    return s


class _D:
    df = 0
_dummy_target = _D()


HEAL_ITEMS = (2, 1, 0)                        # 大军粮, 军粮, 干粮


def heal_slot(em, u):
    """bag slot of the best hp remedy the officer carries (inventory in SRAM bank 15, 20 B per unit)"""
    best = None
    for s in range(8):
        lo, hi = em.m[15, 0xA000 + u.i * 20 + s * 2], em.m[15, 0xA000 + u.i * 20 + s * 2 + 1]
        if hi == 0 and lo in HEAL_ITEMS and (best is None or HEAL_ITEMS.index(lo) < HEAL_ITEMS.index(best[1])):
            best = (s, lo)
    return None if best is None else best[0]


def use_item(em, slot):
    """action menu open: 道具 -> the slot -> on itself"""
    for _ in range(6):
        if em.r8("_dbg_menu_item") == 4:
            break
        em.press("right")
    if em.r8("_dbg_menu_item") != 4:
        return False
    press_wait(em, "a", [11], 20)             # G_ILIST
    if em.state() != 11:
        return False
    for _ in range(8):
        if em.r8("_isel") == slot:
            break
        em.press("down")
    if em.r8("_isel") != slot:
        em.press("b"); em.frames(4)
        return False
    press_wait(em, "a", [12], 20)             # G_IAIM, on the officer's own cell
    em.press("a")
    em.wait_state([G_IDLE, G_BANNER, G_ENEMY, G_OUTCOME], 600)
    for _ in range(3):
        if em.state() in (G_MOVE, G_MENU, G_TARGET, 9, 10, 11, 12, 13):
            em.press("b"); em.frames(4)
    return True


def press_wait(em, btn, states, n=60, after=4):
    em.press(btn, after=after)
    em.wait_state(states, n)


def act(em, u, ctx):
    """one officer's turn; returns True when it acted"""
    if em.state() != G_IDLE:
        return False
    em.goto(u.x, u.y)
    if em.state() != G_IDLE:
        return False
    press_wait(em, "a", [G_MOVE], 20)
    if em.state() != G_MOVE:
        return False
    us = units(em)
    me = us[u.i]
    occ = {(o.x, o.y) for o in us if o.alive and o.i != u.i}
    foes = [o for o in us if o.alive and o.fac == FAC_ENEMY]
    cells = [c for c in move_range(em) if c not in occ]
    if not cells:
        cells = [(me.x, me.y)]
    leader = u.i == 0
    row = ctx["row"]
    caster = me.cls in CASTERS
    wounded = me.hp < 0.4 * me.mhp
    careful = leader or caster or wounded
    goal_field = ctx["field_leader"] if leader and ctx["field_leader"] is not None else None
    if goal_field is None:
        tgt = ctx["targets"]
        if tgt:
            goal_field = ctx["board"].field([(t.x, t.y) for t in tgt], me.mtype)
    best = (-1e18, (me.x, me.y), None)
    rmin, rmax = me.rng
    w = ctx["board"].w
    heal = heal_slot(em, me) if me.hp < (0.75 if leader else 0.55) * me.mhp else None
    if wounded and heal is None:              # no remedy in the bag: back to a village / fort
        goal_field = ctx["heal_field"].get(me.mtype)
    if leader and not ctx["leader_goal"]:     # 刘备 stays with the army, behind it
        goal_field = ctx["board"].field([(o.x, o.y) for o in us if o.alive and o.fac == FAC_PLAYER and o.i != 0], me.mtype)
    kw = 4.0 if leader else (1.0 if (wounded or caster) else 0.1) * AGGR   # 刘备's caution is not a profile knob
    for c in cells:
        dz = danger(c, foes)
        pen = kw * max(0.0, dz - (0.0 if leader else 0.3 * me.hp))
        if leader and dz >= me.hp:
            pen += 3000
        elif dz >= 1.3 * me.hp:                # fighters commit; only near-certain death holds them back
            pen += 300
        gd = goal_field[c[1] * w + c[0]] if goal_field else 0
        if gd >= 10 ** 6:
            gd = 500
        rush = 25 if (row["protect"] < 24 and not leader) else 10   # a protected NPC: get there first
        move_sc = -gd * (40 if leader and ctx["leader_goal"] else rush) - pen
        if heal is not None:
            move_sc += 50
        if leader and not ctx["leader_goal"]:
            move_sc = -pen - 3 * max(0, gd - 1)
        move_sc += RNG.random() * JITTER
        if move_sc > best[0]:
            best = (move_sc, c, None)
        if rmax <= 0:
            continue
        for e in foes:
            dist = abs(e.x - c[0]) + abs(e.y - c[1])
            if not rmin <= dist <= rmax:
                continue
            dmg = est_dmg(me, e)
            kill = dmg >= e.hp
            sc = 200 + min(dmg, e.hp) + (1500 if kill else 0) - gd
            if row["kind"] == VIC_DEFEAT and e.i == row["unit"]:
                sc += 2500
            erng = e.rng
            if not kill and erng[0] <= dist <= erng[1]:
                sc -= 0.6 * est_dmg(e, me)    # counter attack
            sc -= pen
            if leader and not kill:
                sc -= 300
            if row["protect"] < 24:
                if abs(e.x - us[row["protect"]].x) + abs(e.y - us[row["protect"]].y) <= e.move + e.rng[1]:
                    sc += 600                    # it can reach the protected NPC next turn
                else:
                    sc -= 25 * gd                # a side fight costs the rescue
            sc += RNG.random() * JITTER
            if sc > best[0]:
                best = (sc, c, e)
    sc, cell, target = best
    use_heal = heal is not None and (target is None or me.hp < 0.35 * me.mhp)
    if use_heal:
        target = None
    if TRACE:
        print(f"  r{em.r16('_dbg_round')} u{u.i} cls{me.cls} hp{me.hp}/{me.mhp} ({me.x},{me.y})->{cell} "
              f"tgt={None if target is None else (target.i, target.hp)} sc={sc:.0f} dz={danger(cell, foes):.0f} cells={len(cells)}")
    em.goto(*cell)
    press_wait(em, "a", [G_MENU, G_MOVE], 30)
    if em.state() != G_MENU:                  # refused endpoint: stay
        em.goto(me.x, me.y)
        press_wait(em, "a", [G_MENU, G_MOVE], 30)
        if em.state() != G_MENU:
            press_wait(em, "b", [G_IDLE], 20)
            return False
        target = None
    if use_heal and use_item(em, heal):
        return True
    want = 0 if target is not None else 2     # MI_ATTACK / MI_WAIT
    for _ in range(6):
        if em.r8("_dbg_menu_item") == want:
            break
        em.press("right")
    if em.r8("_dbg_menu_item") != want:       # attack not offered (range mismatch): wait instead
        target = None
        for _ in range(6):
            if em.r8("_dbg_menu_item") == 2:
                break
            em.press("right")
    press_wait(em, "a", [G_TARGET, G_IDLE, G_BANNER, G_SHOW, G_ENEMY, G_OUTCOME], 30)
    if em.state() == G_TARGET:
        for _ in range(10):
            if target is None or em.cur() == (target.x, target.y):
                break
            em.press("right")
        em.press("a")
    em.wait_state([G_IDLE, G_BANNER, G_ENEMY, G_OUTCOME], 600)
    leave_menus(em)
    return True


def leave_menus(em):
    """never stay in a sub-menu; a prompt that ignores B (who takes a chest's item) gets A"""
    for k in range(8):
        if em.state() not in (G_MOVE, G_MENU, G_TARGET, 9, 10, 11, 12, 13):
            return
        em.press("b" if k % 2 == 0 else "a", after=8)


def player_phase(em):
    bt = em.r8("_dbg_battle")
    row = ROWS[bt]
    board = Board(em)
    us = units(em)
    foes = [o for o in us if o.alive and o.fac == FAC_ENEMY]
    if row["kind"] == VIC_DEFEAT and row["unit"] < 24 and us[row["unit"]].alive:
        targets = [us[row["unit"]]] + foes[:0]
        near = sorted(foes, key=lambda e: abs(e.x - us[0].x) + abs(e.y - us[0].y))[:3]
        targets = targets + near
    elif row["kind"] == VIC_REACH and row["unit"] != 0 and row["unit"] < 24 and us[row["unit"]].alive:
        esc = us[row["unit"]]                 # escort: clear the enemies around the escortee
        targets = sorted(foes, key=lambda e: abs(e.x - esc.x) + abs(e.y - esc.y))[:4]
    else:
        targets = foes
    if row["protect"] < 24 and us[row["protect"]].alive:   # a protected NPC: whatever threatens it first
        pr = us[row["protect"]]
        near = sorted(foes, key=lambda e: abs(e.x - pr.x) + abs(e.y - pr.y))[:4]
        targets = near + [x for x in targets if x not in near]
    leader_goal = row["kind"] == VIC_REACH and row["unit"] == 0
    fl = None
    if leader_goal:
        x0, y0, x1, y1 = row["area"]
        fl = board.field([(x, y) for x in range(x0, x1 + 1) for y in range(y0, y1 + 1)], us[0].mtype)
    heal_cells = [(x, y) for y in range(board.h) for x in range(board.w) if board.t[y * board.w + x] in (3, 4)]
    heal_field = {mt: board.field(heal_cells, mt) for mt in ("foot", "cavalry", "rough", "wagon")} if heal_cells else {}
    ctx = dict(row=row, board=board, targets=targets, field_leader=fl, leader_goal=leader_goal, heal_field=heal_field)
    order = [o for o in us if o.alive and o.fac == FAC_PLAYER and not (o.flags & 0x02)]
    order.sort(key=lambda o: (o.i == 0, o.cls in CASTERS))
    for o in order:
        if em.state() != G_IDLE:
            break
        act(em, o, ctx)
    if em.state() in (G_IDLE, G_ENDTURN):
        em.end_turn()


def evaluate(em):
    """how good the position after a player phase + the enemy's reply is"""
    if em.state() == G_OUTCOME:
        return 1e9 - em.r16("_dbg_round") if em.r8("_dbg_outcome") == 1 else -1e9
    row = ROWS[em.r8("_dbg_battle")]
    us = units(em)
    sc = 0.0
    limit = TURN_LIMIT.get(em.r8("_dbg_battle"), 25)
    press = 1.0 + max(0.0, em.r16("_dbg_round") / limit - 0.5) * 2    # late in the battle: progress counts more
    for o in us:
        if o.fac == FAC_PLAYER and not (o.flags & 0x20):
            w = 3.0 if o.i == 0 else 1.0
            sc += w * (1000 * o.hp / max(1, o.mhp) + (1000 if o.alive else 0))
        elif o.fac == FAC_ALLY and o.i in (row["protect"], row["unit"]):
            sc += 3.0 * (1000 * o.hp / max(1, o.mhp) + (1000 if o.alive else 0))
        elif o.fac == FAC_ENEMY and o.mhp:
            w = 4.0 if (row["kind"] == VIC_DEFEAT and o.i == row["unit"]) else 1.0
            sc -= press * w * (1000 * o.hp / o.mhp + 1000 if o.alive else 0)
    if row["kind"] == VIC_REACH and row["unit"] < 24:
        x0, y0, x1, y1 = row["area"]
        r = us[row["unit"]]
        sc -= 60 * (max(x0 - r.x, 0, r.x - x1) + max(y0 - r.y, 0, r.y - y1))
    return sc


def run_round(em, plan):
    """one player phase with the given exploration plan, then the enemy (and ally) phases"""
    global JITTER, AGGR
    aggr, jit, plans = PROFILE
    if plans == 1:
        AGGR, JITTER = aggr, jit
    else:
        AGGR = (aggr, aggr * 3.0, aggr * 0.2)[plan % 3]   # this profile, more cautious, bolder
        JITTER = jit if plan < 3 else jit + 60.0 * (plan - 2)
    RNG.seed(em.r8("_dbg_battle") * 1009 + em.r16("_dbg_round") * 31 + plan)
    player_phase(em)
    start = em.fc
    stuck = 0
    while em.fc - start < 60 * 60 * 3:
        st = em.state()
        if st in (G_MOVE, G_MENU, G_TARGET, 9, 10, 11, 12, 13):
            stuck += 1
            if stuck % 4 == 0:                # a blocking prompt (who takes the chest's item) wants A
                em.press("a", after=8)
                continue
        else:
            stuck = 0
        if st == G_OUTCOME:
            return
        if st == G_IDLE and em.r8("_dbg_dialog_active") == 0:
            em.wait_state([G_IDLE, G_OUTCOME], 10)
            if em.state() == G_IDLE:
                return
            continue
        if st in (G_MOVE, G_MENU, G_TARGET, G_ENDTURN, G_DANGER, 9, 10, 11, 12, 13):
            em.press("b"); em.frames(2)
            continue
        if em.fc % 9 == 0:
            em.press("b", after=1)            # skip fullscreen presentations
        em.frames(1)


def play_until_outcome(em, max_rounds=60):
    """round by round: try PLANS variants of the player phase from a savestate, keep the best result"""
    import io
    for _ in range(max_rounds):
        em.wait_state([G_IDLE, G_OUTCOME], 3000)
        if em.state() == G_OUTCOME:
            return True
        if em.state() != G_IDLE:
            return False
        s0 = io.BytesIO(); em.pb.save_state(s0)
        fc0 = em.fc
        best = None
        for plan in range(PROFILE[2]):
            s0.seek(0); em.pb.load_state(s0); em.fc = fc0
            run_round(em, plan)
            v = evaluate(em)
            if best is None or v > best[0]:
                s1 = io.BytesIO(); em.pb.save_state(s1)
                best = (v, s1, em.fc)
            if v >= 1e9:                      # a win: take it
                break
        best[1].seek(0); em.pb.load_state(best[1]); em.fc = best[2]
        em.frames(2)
        if em.state() == G_OUTCOME:
            return True
    return False


def loss_reason(em, bt):
    row, us = ROWS[bt], units(em)
    if not us[0].alive:
        return "刘备撤退"
    if row["protect"] < 24 and not us[row["protect"]].alive:
        return "保护对象撤退"
    if row["kind"] == VIC_REACH and row["unit"] < 24 and not us[row["unit"]].alive:
        return "护送对象撤退"
    if em.r16("_dbg_round") >= TURN_LIMIT.get(bt, 99):
        return "回合用尽"
    return "其他"


ENEMY_LV = {}
for _k, _b in enumerate(json.load(open(ROOT / "content/campaign.json"))["battles"]):
    _lv = [u.get("level", 1) for u in json.load(open(ROOT / f"content/battles/{_b['id']}.json"))["units"] if u["faction"] == "ENEMY"]
    ENEMY_LV[_k] = sum(_lv) / len(_lv) if _lv else 1


def calibrate(em, next_bt, refill):
    """--calibrate: lift every roster officer to (average enemy level of the next battle - 2) before it starts
    (SRAM12 roster, 48 B records, level at +4, hp at +10: 0 = full). Measures the battle design with an army
    of the expected strength instead of the bot's own (forced wins earn no experience)."""
    n = em.m[12, 0xA000 + 7890]
    want = max(1, int(ENEMY_LV.get(next_bt, 1)) - 2)
    for k in range(min(n, 128)):
        a = 0xA000 + k * 48
        if k < 24 and em.m[1, 0xD000 + 40 * k + 5] < want:   # the camp syncs these instances back into the roster
            em.m[1, 0xD000 + 40 * k + 5] = want
        if em.m[12, a + 4] < want:
            em.m[12, a + 4] = want
            if refill:
                em.m[12, a + 10] = 0; em.m[12, a + 11] = 0; em.m[12, a + 12] = 0; em.m[12, a + 13] = 0


def player_stats(em):
    us = units(em)
    out = [o for o in us if o.fac == FAC_PLAYER and not (o.flags & 0x20) and (o.hp or o.flags & 0x08)]
    lost = [o.i for o in out if o.flags & 0x08 or not o.hp]
    return len(out), lost


def main():
    out_path = Path(_args[1]) if len(_args) > 1 else ROOT / "build/autoplay.json"
    global TRACE, CALIBRATE
    TRACE = "--trace" in _opts
    CALIBRATE = "--calibrate" in _opts
    max_b = int(_opts.get("--max-battles", "99"))
    tries = int(_opts.get("--tries", str(len(PROFILES))))
    if "--state" in _opts:                    # start from a saved emulator state (tuning one battle)
        em = Emu(title=True)
        em.pb.load_state(open(_opts["--state"], "rb")); em.frames(30)
    else:
        em = Emu()
    em.wait_state([G_IDLE], 3000)
    report = []
    seen = 0
    while seen < max_b:
        bt = em.r8("_dbg_battle")
        rec = dict(battle=bt, title=TITLES[bt] if bt < len(TITLES) else None, tries=0, won=False, rounds=None, deployed=None, retreated=None,
                   gold_before=em.r16("_dbg_gold"))
        global PROFILE
        sd = ROOT / "build/autoplay_states"; sd.mkdir(parents=True, exist_ok=True)
        with open(sd / f"bt{bt:02d}.state", "wb") as f:          # resume point: --state=...
            em.pb.save_state(f)
        while True:
            PROFILE = PROFILES[rec["tries"] % len(PROFILES)]
            rec["tries"] += 1
            ok = play_until_outcome(em)
            outcome_ready(em)
            if ok and em.r8("_dbg_outcome") == 1:
                rec["won"] = True
                rec["profile"] = list(PROFILE)
                break
            rec["loss"] = loss_reason(em, bt)
            if rec["tries"] >= tries or not ok:
                break
            em.press("a", after=16)           # retry from the pre-battle snapshot
            em.wait_state([G_IDLE], 3000)
        rec["rounds"] = em.r16("_dbg_round")
        n, lost = player_stats(em)
        rec["deployed"], rec["retreated"] = n, lost
        if not rec["won"]:                    # force the win to keep measuring the campaign behind it
            if em.state() == G_OUTCOME:
                em.press("a", after=16); em.wait_state([G_IDLE], 3000)
            leave_menus(em)
            win_now(em, [o.i for o in units(em) if o.alive and o.fac == FAC_ENEMY])
        rec["gold_after"] = em.r16("_dbg_gold")
        report.append(rec)
        seen += 1
        print(json.dumps(rec, ensure_ascii=False), flush=True)
        out_path.write_text(json.dumps(report, ensure_ascii=False, indent=1))
        for k in range(6000):                 # result page, camp, next battle, or an ending back to the cover
            if k % 200 == 0 and em.state() == G_OUTCOME:
                em.press("a", after=16)
            if em.state() in (G_IDLE, 20, 25, G_TITLE) and em.r8("_dbg_dialog_active") == 0:
                break
            em.frames(1)
        if em.state() == G_TITLE:
            print("ending reached after battle", bt, flush=True)
            break
        if CALIBRATE:                         # in the camp: before 出阵 derives the units
            for _ in range(3000):
                if em.state() in (20, 25) or em.r8("_dbg_battle") != bt:
                    break
                em.frames(1)
            if em.state() in (20, 25):
                nid = _CAMPAIGN["battles"][bt].get("next")
                nb = next((k for k, b in enumerate(_CAMPAIGN["battles"]) if b["id"] == nid), bt + 1)
                calibrate(em, nb, True)
                print(f"  calibrated for battle {nb}: level {max(1, int(ENEMY_LV.get(nb, 1)) - 2)}", flush=True)
        through_camp(em)
        em.wait_state([G_IDLE], 9000)          # a chapter card and a long opening can take a while
        if em.state() != G_IDLE:
            print("stuck after battle", bt, "state", em.state(), "battle now", em.r8("_dbg_battle"), "dlg", em.r8("_dbg_dialog_active"), flush=True)
            em.png(ROOT / "build/autoplay_stuck.png")
            break
    out_path.write_text(json.dumps(report, ensure_ascii=False, indent=1))


if __name__ == "__main__":
    main()
