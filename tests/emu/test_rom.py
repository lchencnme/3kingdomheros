#!/usr/bin/env python3
"""PyBoy end-to-end checks for TECH01 v2 (real key presses only; RAM is read, never written).

usage: python3 tests/emu/test_rom.py build/gb/sanguo_hjz.gbc
"""
import struct
import sys
import warnings
import zlib
from pathlib import Path

warnings.filterwarnings("ignore")
import logging  # noqa: E402

logging.disable(logging.WARNING)
from pyboy import PyBoy  # noqa: E402

ROM = Path(sys.argv[1] if len(sys.argv) > 1 else "build/gb/sanguo_hjz.gbc")
SYMS = {}
for line in ROM.with_suffix(".noi").read_text().splitlines():
    p = line.split()
    if len(p) == 3 and p[0] == "DEF":
        SYMS[p[1]] = int(p[2], 16)

G_IDLE, G_MOVE, G_MENU, G_TARGET, G_SHOW, G_ENEMY, G_BANNER, G_OUTCOME, G_TITLE = range(9)
N_UNITS = 7
results = []


def check(name, cond, detail=""):
    results.append((name, bool(cond), detail))


class Emu:
    def __init__(self, ram=None):
        import io
        kw = {"ram_file": io.BytesIO(ram)} if ram is not None else {}
        self.pb = PyBoy(str(ROM), window="null", cgb=True, sound_emulated=False, **kw)
        self.m = self.pb.memory
        self.fc = 0
        self.frames(120)  # PyBoy plays the ~77-frame CGB boot animation first
        # the first player banner performs the phase-start autosave (~40 frames, blocking by design)
        n = 0
        while self.m[SYMS["_dbg_frame_lo"]] == 0 and n < 200:
            self.frames(1)
            n += 1

    def frames(self, n):
        for _ in range(n):
            self.pb.tick()
        self.fc += n

    def r8(self, s):
        return self.m[SYMS[s]]

    def r16(self, s):
        a = SYMS[s]
        return self.m[a] | (self.m[a + 1] << 8)

    def r32(self, s):
        a = SYMS[s]
        return self.m[a] | (self.m[a + 1] << 8) | (self.m[a + 2] << 16) | (self.m[a + 3] << 24)

    def unit(self, i):
        a = SYMS["_dbg_unit_xy"] + 2 * i
        return self.m[a], self.m[a + 1]

    def hp(self, i):
        a = SYMS["_dbg_unit_hp"] + 2 * i
        return self.m[a] | (self.m[a + 1] << 8)

    def state(self):
        return self.r8("_dbg_state")

    def cur(self):
        return self.r8("_dbg_cur_x"), self.r8("_dbg_cur_y")

    def press(self, btn, after=4):
        self.pb.button_press(btn)
        self.frames(2)
        self.pb.button_release(btn)
        self.frames(after)

    def hold(self, btn, n):
        self.pb.button_press(btn)
        self.frames(n)
        self.pb.button_release(btn)
        self.frames(4)

    def wait_state(self, states, limit=600):
        n = 0
        while self.state() not in states and n < limit:
            self.frames(1)
            n += 1
        return n

    def goto(self, x, y):
        for _ in range(80):
            cx, cy = self.cur()
            if (cx, cy) == (x, y):
                return
            if cx != x:
                self.press("right" if x > cx else "left")
            else:
                self.press("down" if y > cy else "up")

    def power_off(self):
        """stop the emulator and return the battery RAM image (like pulling the cartridge)"""
        import io
        bio = io.BytesIO()
        self.pb.stop(ram_file=bio)
        return bio.getvalue()

    def canary_ok(self):
        return all(self.m[0xCE00 + i] == 0xA5 for i in range(16))

    def stack_free(self):
        """bytes of the A5 fill pattern still untouched above CE00 (stack high-water margin)"""
        n = 0
        while 0xCE00 + n < 0xD000 and self.m[0xCE00 + n] == 0xA5:
            n += 1
        return n

    def png(self, path):
        a = self.pb.screen.ndarray
        h, w = a.shape[0], a.shape[1]
        raw = b"".join(b"\x00" + bytes(a[y, :, :3].tobytes()) for y in range(h))

        def chunk(k, d):
            return struct.pack(">I", len(d)) + k + d + struct.pack(">I", zlib.crc32(k + d) & 0xFFFFFFFF)
        data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
        data += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
        Path(path).write_bytes(data)


# ------------------------------------------------------------------ BG map consistency helper
import json as _json
import re as _re
_B = _json.load(open(ROM.resolve().parents[2] / "content/battles/BT_TECH01.json"))
_W, _H, _TERR = _B["map"]["width"], _B["map"]["height"], _B["map"]["terrain"]
_IDX = (ROM.resolve().parents[2] / "src/gen/tech01_index.c").read_text()
_META = [list(map(int, m.split(","))) for m in _re.findall(r"\{(\d+, \d+, \d+, \d+)\}",
         _re.search(r"tech01_meta\[10\]\[4\] = \{(.*?)\};", _IDX).group(1))]
_TPAL = list(map(int, _re.search(r"tech01_terrain_pal\[10\] = \{(.*?)\}", _IDX).group(1).split(",")))
_BLANK = 40


def view_mismatches(em):
    """visible BG hardware tiles (ids + palette bits) vs terrain data; highlight bit ignored"""
    cx, cy = em.r16("_dbg_cam_x"), em.r16("_dbg_cam_y")
    bad = 0
    for j in range(15):
        for i in range(21):
            wx, wy = cx // 8 + i, cy // 8 + j
            if wy * 8 - cy >= 112 or wx * 8 - cx >= 160:
                continue
            if wx < 0 or wy < 0 or wx >= _W * 2 or wy >= _H * 2:
                exp = (_BLANK, 0)
            else:
                tt = _TERR[(wy >> 1) * _W + (wx >> 1)]
                exp = (_META[tt][((wy & 1) << 1) | (wx & 1)], _TPAL[tt])
            a = 0x9800 + ((wy & 31) << 5) + (wx & 31)
            if (em.m[0, a], em.m[1, a] & 3) != exp:
                bad += 1
    return bad


# ------------------------------------------------------------------ basic flow
e = Emu()
check("boot: main loop running", e.r8("_dbg_frame_lo") != 0)
check("CGB double speed (KEY1 bit7)", e.m[0xFF4D] & 0x80, f"KEY1={e.m[0xFF4D]:02X}")
hdr = ROM.read_bytes()
check("header: CGB-only + MBC5+RAM+BATT + 8MiB/128KiB",
      hdr[0x143] == 0xC0 and hdr[0x147] == 0x1B and hdr[0x148] == 0x08 and hdr[0x149] == 0x04)
check("opening banner shown", e.state() == G_BANNER)
e.wait_state([G_IDLE])
check("player phase idle, round 1", e.state() == G_IDLE and e.r16("_dbg_round") == 1)
check("cursor starts on 刘备 (4,12)", e.cur() == (4, 12))
e.png(ROM.parent / "shot_start.png")

# select 刘备 -> move 2 east -> menu (no targets: 待命 only) -> A = wait
e.press("a")
e.wait_state([G_MOVE], 30)
rng_frames = e.r16("_dbg_t_range")
check(f"move range <=150ms (刘备: {rng_frames} frames)", rng_frames <= 9)
check("A on own unit -> move preview", e.state() == G_MOVE)
e.press("right"); e.press("right")
check("cursor moved to (6,12)", e.cur() == (6, 12))
e.press("a")
check("legal endpoint -> action menu, unit not moved yet", e.state() == G_MENU and e.unit(0) == (4, 12))
e.press("b")
check("B in menu -> back to move preview, nothing committed", e.state() == G_MOVE and e.unit(0) == (4, 12))
e.press("a")
e.press("a")       # 待命
check("wait commits the move", e.unit(0) == (6, 12) and e.state() == G_IDLE, f"u0={e.unit(0)} st={e.state()}")
e.press("a")
check("acted unit not selectable", e.state() == G_IDLE)

# 关羽: occupied ally endpoint rejected; forest rejected for cavalry; B restores cursor
e.goto(5, 11)
e.press("a"); e.wait_state([G_MOVE], 30)
check(f"move range <=150ms (关羽: {e.r16('_dbg_t_range')} frames)", e.r16("_dbg_t_range") <= 9)
e.goto(5, 13)
e.press("a")
check("occupied ally endpoint rejected", e.state() == G_MOVE and e.unit(1) == (5, 11))
e.goto(5, 8)
e.press("a")
check("cavalry cannot end in forest (5,8)", e.state() == G_MOVE)
e.frames(16)
e.png(ROM.parent / "shot_preview_guanyu.png")
e.press("b")
check("B cancels preview, cursor back on unit", e.state() == G_IDLE and e.cur() == (5, 11))

# scrolling to the far corner and back
e.hold("right", 300)
check("camera clamps at 32*16-160=352", e.r16("_dbg_cam_x") == 352, f"cam_x={e.r16('_dbg_cam_x')}")
check("SCX = cam_x low byte", e.m[0xFF43] == (e.r16("_dbg_cam_x") & 0xFF))
e.hold("down", 200)
check("camera clamps at 24*16-112=272", e.r16("_dbg_cam_y") == 272)
check("BG map matches terrain at far corner", view_mismatches(e) == 0)
e.png(ROM.parent / "shot_far_corner.png")
e.hold("left", 400); e.hold("up", 300)
check("camera returns to origin", e.r16("_dbg_cam_x") == 0 and e.r16("_dbg_cam_y") == 0)
check("BG map matches terrain after round trip", view_mismatches(e) == 0)
check("stack canary intact", e.canary_ok())
e.pb.stop(save=False)


# ------------------------------------------------------------------ full battle by a key-press bot
def play_battle(shots=False, max_frames=60 * 60 * 20):
    """Bot: each player unit attacks the first target in reach from where it stands, else waits."""
    em = Emu()
    em.wait_state([G_IDLE])
    worst_ai = worst_range = 0
    saw_enemy_attack = False
    hp_ok = True
    shot_done = False
    start = em.fc
    while em.state() != G_OUTCOME and em.fc - start < max_frames:
        st = em.state()
        if st == G_IDLE:
            for i in range(3):
                if em.hp(i) == 0 or em.state() != G_IDLE:
                    continue
                em.goto(*em.unit(i))
                em.press("a")
                em.wait_state([G_MOVE], 12)
                if em.state() != G_MOVE:
                    continue          # already acted
                worst_range = max(worst_range, em.r16("_dbg_t_range"))
                em.press("a")         # endpoint = own cell
                em.wait_state([G_MENU], 12)
                if em.state() != G_MENU:
                    continue
                em.press("a")         # attack if targets (default) else wait
                em.wait_state([G_TARGET, G_IDLE, G_BANNER, G_SHOW], 12)
                if em.state() == G_TARGET:
                    if shots and not shot_done:
                        em.frames(10)         # HUD pipeline flips a few frames later
                        em.png(ROM.parent / "shot_target.png")
                    em.press("a")
                    if shots and not shot_done:
                        em.frames(10)
                        em.png(ROM.parent / "shot_result.png")
                        shot_done = True
                em.wait_state([G_IDLE, G_BANNER, G_ENEMY, G_OUTCOME], 200)
            if em.state() == G_IDLE:
                em.press("start")     # end turn if anyone is left
        elif st in (G_MOVE, G_MENU, G_TARGET):
            em.press("b")             # never get stuck in a sub-menu
        else:
            before = [em.hp(i) for i in range(3)]
            em.frames(1)
            worst_ai = max(worst_ai, em.r16("_dbg_t_ai"))
            if any(em.hp(i) < before[i] for i in range(3)):
                saw_enemy_attack = True
        for i in range(N_UNITS):
            if em.hp(i) > 9999:
                hp_ok = False
    res = dict(outcome=em.r8("_dbg_outcome"), round=em.r16("_dbg_round"), rng=em.r32("_dbg_rng"),
               draws=em.r32("_dbg_draws"), hps=[em.hp(i) for i in range(N_UNITS)],
               worst_ai=worst_ai, worst_range=worst_range, enemy_attacked=saw_enemy_attack,
               hp_ok=hp_ok, canary=em.canary_ok(), stack_free=em.stack_free(), state=em.state())
    if shots:
        em.frames(10)
        em.png(ROM.parent / "shot_outcome.png")
    em.press("a")            # restart from the outcome screen
    em.wait_state([G_IDLE], 200)
    res["restart_ok"] = em.state() == G_IDLE and em.r16("_dbg_round") == 1 and em.unit(0) == (4, 12)
    em.pb.stop(save=False)
    return res


r1 = play_battle(shots=True)
r2 = play_battle()
print("battle:", r1)
check("bot battle reaches an outcome", r1["state"] == G_OUTCOME and r1["outcome"] in (1, 2), str(r1))
check("enemy phase attacked the player", r1["enemy_attacked"])
check(f"AI decision <=750ms (worst {r1['worst_ai']} frames)", r1["worst_ai"] <= 45)
check(f"move range <=150ms in battle (worst {r1['worst_range']} frames)", r1["worst_range"] <= 9)
check("hp never out of range", r1["hp_ok"])
check("stack canary intact after full battle", r1["canary"])
check(f"stack high-water margin >= 64B (free {r1['stack_free']})", r1["stack_free"] >= 64)
check("restart from outcome resets the battle", r1["restart_ok"])
check("replay: identical outcome/round/RNG/draws/hp",
      all(r1[k] == r2[k] for k in ("outcome", "round", "rng", "draws", "hps")), f"{r1} vs {r2}")

# ------------------------------------------------------------------ in-game save: SELECT -> power off -> cold boot
s = Emu()
s.wait_state([G_IDLE])
s.frames(30)
check("autosave at player phase start", s.r8("_dbg_saves") >= 1 and s.r8("_dbg_save_result") == 0,
      f"saves={s.r8('_dbg_saves')} r={s.r8('_dbg_save_result')}")
s.press("a"); s.wait_state([G_MOVE], 20)
s.press("right"); s.press("right"); s.press("a"); s.wait_state([G_MENU], 20); s.press("a")
s.wait_state([G_IDLE], 30)
n0 = s.r8("_dbg_saves")
s.press("select"); s.frames(90)
check("SELECT writes the suspend save", s.r8("_dbg_saves") == n0 + 1 and s.r8("_dbg_save_result") == 0)
saved = dict(units=[s.unit(i) for i in range(N_UNITS)], hp=[s.hp(i) for i in range(N_UNITS)],
             rng=s.r32("_dbg_rng"), draws=s.r32("_dbg_draws"), cur=s.cur(), round=s.r16("_dbg_round"))
ram = s.power_off()
c = Emu(ram)
c.wait_state([G_TITLE], 200)
check("cold boot finds the suspend save (continue prompt)", c.state() == G_TITLE)
c.press("start")
c.wait_state([G_IDLE], 100)
got = dict(units=[c.unit(i) for i in range(N_UNITS)], hp=[c.hp(i) for i in range(N_UNITS)],
           rng=c.r32("_dbg_rng"), draws=c.r32("_dbg_draws"), cur=c.cur(), round=c.r16("_dbg_round"))
check("continue restores units/hp/RNG/draws/cursor/round exactly", got == saved, f"{got} vs {saved}")
check("restored 刘备 keeps its acted move (6,12)", c.unit(0) == (6, 12))
check("BG map consistent after load", view_mismatches(c) == 0)
c.pb.stop(save=False)
n = Emu(ram)
n.wait_state([G_TITLE], 200)
n.press("a"); n.wait_state([G_IDLE], 200)
check("A at the prompt starts a new battle", n.unit(0) == (4, 12) and n.r16("_dbg_round") == 1)
n.pb.stop(save=False)
# a finished battle must not be offered for continue
f = Emu(ram)
f.wait_state([G_TITLE], 200); f.press("a"); f.wait_state([G_IDLE], 200)
while f.state() != G_OUTCOME and f.fc < 40000:
    if f.state() == G_IDLE:
        f.press("start")
    f.frames(1)
ram2 = f.power_off()
g = Emu(ram2)
g.frames(60)
check("suspend invalidated after the battle ended", g.state() != G_TITLE)
g.pb.stop(save=False)

ok = sum(1 for _, c, _ in results if c)
for name, c, d in results:
    print(("PASS " if c else "FAIL ") + name + (f"  [{d}]" if d and not c else ""))
print(f"{ok}/{len(results)} passed")
sys.exit(0 if ok == len(results) else 1)
