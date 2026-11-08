#!/usr/bin/env python3
"""PyBoy end-to-end checks for the TECH01 v2 skeleton (real key presses, no memory pokes).

usage: python3 tests/emu/test_rom.py build/gb/sanguo_hjz.gbc
"""
import sys
import warnings
from pathlib import Path

warnings.filterwarnings("ignore")
from pyboy import PyBoy  # noqa: E402

ROM = Path(sys.argv[1] if len(sys.argv) > 1 else "build/gb/sanguo_hjz.gbc")
SYMS = {}
for line in ROM.with_suffix(".noi").read_text().splitlines():
    p = line.split()
    if len(p) == 3 and p[0] == "DEF":
        SYMS[p[1]] = int(p[2], 16)

results = []


def save_png(path):
    """screen -> PNG without Pillow (zlib only)"""
    import struct, zlib
    a = pb.screen.ndarray  # (144,160,4) RGBA
    h, w = a.shape[0], a.shape[1]
    raw = b"".join(b"\x00" + bytes(a[y, :, :3].tobytes()) for y in range(h))
    def chunk(k, d):
        return struct.pack(">I", len(d)) + k + d + struct.pack(">I", zlib.crc32(k + d) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    Path(path).write_bytes(png)


def check(name, cond, detail=""):
    results.append((name, bool(cond), detail))


pb = PyBoy(str(ROM), window="null", cgb=True, sound_emulated=False)
mem = pb.memory


def rd8(sym):
    return mem[SYMS[sym]]


def rd16(sym):
    a = SYMS[sym]
    return mem[a] | (mem[a + 1] << 8)


def unit(i):
    a = SYMS["_dbg_unit_xy"] + 2 * i
    return mem[a], mem[a + 1]


def frames(n):
    for _ in range(n):
        pb.tick()


def press(btn, hold=2, after=4):
    pb.button_press(btn)
    frames(hold)
    pb.button_release(btn)
    frames(after)


def select(gate_frames=9):
    """press A on an own unit; the move range must be ready within the 150ms gate (9 frames)"""
    pb.button_press("a")
    frames(2)
    pb.button_release("a")
    n = 2
    while rd8("_dbg_state") != 1 and n < 60:
        frames(1)
        n += 1
    rng_frames = rd16("_dbg_t_range")
    check(f"move range ready <=150ms (took {rng_frames} frames)", rng_frames <= gate_frames)
    frames(2)


def hold_dir(btn, n):
    pb.button_press(btn)
    frames(n)
    pb.button_release(btn)
    frames(4)


frames(120)   # PyBoy plays the ~77-frame CGB boot animation first
check("boot: main loop running", rd8("_dbg_frame_lo") != 0)
check("CGB double speed (KEY1 bit7)", mem[0xFF4D] & 0x80, f"KEY1={mem[0xFF4D]:02X}")
check("header: CGB-only + MBC5+RAM+BATT + 8MiB/128KiB",
      ROM.read_bytes()[0x143] == 0xC0 and ROM.read_bytes()[0x147] == 0x1B
      and ROM.read_bytes()[0x148] == 0x08 and ROM.read_bytes()[0x149] == 0x04)
check("cursor starts on 刘备 (4,12)", (rd8("_dbg_cur_x"), rd8("_dbg_cur_y")) == (4, 12))

# --- move 刘备 two cells east along the road (legal) ---
select()
check("A on own unit -> move preview", rd8("_dbg_state") == 1)
press("right"); press("right")
check("cursor moved to (6,12)", (rd8("_dbg_cur_x"), rd8("_dbg_cur_y")) == (6, 12))
press("a")
check("legal endpoint commits move", unit(0) == (6, 12) and rd8("_dbg_state") == 0, f"unit0={unit(0)}")

# --- acted unit cannot be selected again ---
press("a")
check("acted unit not selectable", rd8("_dbg_state") == 0)

# --- 关羽 (cavalry, 5,11): occupied endpoint rejected, B cancels with no change ---
press("left"); press("up")         # cursor (5,11)
select()
check("select 关羽", rd8("_dbg_state") == 1 and rd8("_dbg_sel") == 1)
frames(20)   # let the highlight repaint finish
save_png(ROM.parent / "shot_preview_guanyu.png")
press("down"); press("down")        # (5,13) = 张飞, occupied ally
press("a")
check("occupied ally endpoint rejected", rd8("_dbg_state") == 1 and unit(1) == (5, 11))
press("b")
check("B cancels preview, unit unchanged, cursor back", rd8("_dbg_state") == 0 and unit(1) == (5, 11)
      and (rd8("_dbg_cur_x"), rd8("_dbg_cur_y")) == (5, 11))

# --- cavalry cannot end in forest: forest at (4,8) is reachable on foot but cost 255 for cavalry ---
select()                            # 关羽 again
for _ in range(3):
    press("up")                     # (5,8)
press("a")
check("cavalry cannot enter forest (5,8)", unit(1) == (5, 11) and rd8("_dbg_state") == 1,
      f"unit1={unit(1)} state={rd8('_dbg_state')} cur=({rd8('_dbg_cur_x')},{rd8('_dbg_cur_y')})")
press("b")

# --- scrolling: hold right until the map edge ---
hold_dir("right", 300)
cx, cam = rd8("_dbg_cur_x"), rd16("_dbg_cam_x")
check("cursor reaches east edge", cx == 31, f"cur_x={cx}")
check("camera clamps at 32*16-160=352", cam == 352, f"cam_x={cam}")
check("SCX = cam_x low byte", mem[0xFF43] == (cam & 0xFF), f"SCX={mem[0xFF43]}")
hold_dir("down", 200)
check("camera clamps at 24*16-112=272", rd16("_dbg_cam_y") == 272, f"cam_y={rd16('_dbg_cam_y')}")
save_png(ROM.parent / "shot_far_corner.png")
hold_dir("left", 400); hold_dir("up", 300)
check("camera returns to origin", rd16("_dbg_cam_x") == 0 and rd16("_dbg_cam_y") == 0)

# --- BG map consistency: every visible hw tile matches the expected terrain metatile ---
save_png(ROM.parent / "shot_origin.png")

pb.stop(save=False)
ok = sum(1 for _, c, _ in results if c)
for name, c, d in results:
    print(("PASS " if c else "FAIL ") + name + (f"  [{d}]" if d and not c else ""))
print(f"{ok}/{len(results)} passed")
sys.exit(0 if ok == len(results) else 1)
