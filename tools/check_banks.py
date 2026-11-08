#!/usr/bin/env python3
"""Fail the build if any ROM bank overflows (design doc 11.3 / 16.1 ROM report).

The GBDK linker only warns ("Possible overflow") when a banked area exceeds 16 KiB, and the
spill silently lands in the next bank: that produced a real crash (core tables read as zeros).
"""
import re
import sys

text = open(sys.argv[1]).read()
areas = {}
for m in re.finditer(r"^(_\w+)\s+([0-9A-F]{8})\s+([0-9A-F]{8}) =\s+(\d+)\. bytes", text, re.M):
    areas[m.group(1)] = (int(m.group(2), 16), int(m.group(4)))
bad = []
fixed_end = 0
for name, (start, size) in areas.items():
    if name in ("_CODE", "_HOME", "_BASE", "_GSINIT", "_GSFINAL", "_INITIALIZER", "_LIT", "_HEADER"):
        fixed_end = max(fixed_end, start + size)
    m = re.match(r"_CODE_(\d+)$", name)
    if m and size > 0x4000:
        bad.append(f"{name}: {size} bytes > 16384")
if fixed_end > 0x4000:
    bad.append(f"fixed bank 0 ends at 0x{fixed_end:04X} > 0x4000")
used = {n: s for n, (_, s) in areas.items() if n.startswith("_CODE")}
report = ", ".join(f"{n}={s}" for n, s in sorted(used.items()))
print(f"check_banks: bank0 end 0x{fixed_end:04X}; {report}")
if bad:
    print("check_banks: FAIL\n  " + "\n  ".join(bad))
    sys.exit(1)
