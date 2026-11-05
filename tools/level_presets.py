#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# OpenPocket — level the factory presets (v36): sets each preset's LEVEL (LVL(..) in px_preset_factory) so that it
# plays at TARGET LUFS (BS.1770) on DI guitar, like the amps at noon. Prints loudness and peak.
#   python3 tools/level_presets.py DI.wav [DI2.wav ...]
import os, re, subprocess, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from calib_levels import lufs, ROOT, TARGET

EXE = os.path.join(ROOT, "build", "lvl_preset")
SRC = os.path.join(ROOT, "engine", "px_dsp.c")
NFACT = 24                                           # the levelled ones (the USER slots stay at 50)


def build():
    subprocess.run(["cc", "-O2", "-I" + os.path.join(ROOT, "engine"), os.path.join(ROOT, "tests", "engine", "preset.c"),
                    os.path.join(ROOT, "engine", "px_dsp.c"), os.path.join(ROOT, "firmware", "px_math.c"), "-lm", "-o", EXE], check=True)


def measure(k, dis):
    out = os.path.join(ROOT, "build", "lvlp.f32"); ls, pk = [], 0.0
    for d in dis:
        name = subprocess.run([EXE, str(k), d, out], check=True, capture_output=True, text=True).stdout.strip()
        y = np.fromfile(out, np.float32).astype(np.float64)
        ls.append(lufs(y)); pk = max(pk, float(np.abs(y).max()))
    return name, float(np.mean(ls)), 20 * np.log10(pk + 1e-12)


def set_levels(levels):
    s = open(SRC).read()
    for k, v in levels.items():
        pat = re.compile(r"(case %d: setname\(p, \"[^\"]*\"\);[^\n]*?LVL\()(\d+)(\))" % k)
        s, n = pat.subn(lambda m: m.group(1) + str(v) + m.group(3), s, count=1)
        if n != 1: sys.exit("preset %d: no LVL() on its case line" % k)
    open(SRC, "w").write(s)


def main():
    dis = sys.argv[1:]
    build()
    for it in range(3):
        lv = {}
        for k in range(NFACT):
            name, l, pk = measure(k, dis)
            cur = int(re.search(r"case %d: setname\(p, \"[^\"]*\"\);[^\n]*?LVL\((\d+)\)" % k, open(SRC).read()).group(1))
            lv[k] = int(np.clip(round(cur + (TARGET - l) / 0.3), 0, 100))
            if it == 2: print("%2d %-14s %6.1f LUFS  peak %5.1f dBFS  LEVEL %d" % (k + 1, name, l, pk, cur))
        if it < 2: set_levels(lv); build()


if __name__ == "__main__":
    main()
