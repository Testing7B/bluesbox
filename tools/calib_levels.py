#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# OpenPocket — level calibration (v36) -> engine/px_trim.h. Loudness = ITU-R BS.1770 (K-weighted, gated) of real DI
# guitar at the pedal's level (x 0.15: hard playing peaks about -17 dBFS at the converter).
#  - amps: amp + 1x12 US OPEN, tone at noon, GAIN 0..100: noon at TARGET LUFS for every amp, +-3 dB over the knob
#  - cabinets: each one at the level of the 1x12 (TWIN at noon in front)
#  - every other type (knobs at noon, in front of / after TWIN at noon): switching it on keeps the loudness
#   python3 tools/calib_levels.py DI.wav [DI2.wav ...]
import os, re, subprocess, sys
import numpy as np
from scipy import signal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SR = 44100
TARGET = -23.0                      # LUFS at noon (EBU R128; engine output, before the master volume): headroom for cleans
RISE = 6.0                          # dB from GAIN 0 to GAIN 100
EXE = os.path.join(ROOT, "build", "lvl_chain")
TRIM = os.path.join(ROOT, "engine", "px_trim.h")
B = dict(GT=0, CMP=1, PIT=2, SFX=3, DRV=4, AMP=5, CAB=6, EQ=7, MOD=8, DLY=9, REV=10, SYN=11)
NT = dict(CMP=4, PIT=5, SFX=7, DRV=4, CAB=7, MOD=11, DLY=8, REV=8, SYN=11)


def kweight(x):
    f0, G, Q = 1681.974450955533, 3.999843853973347, 0.7071752369554196
    K = np.tan(np.pi * f0 / SR); Vh = 10 ** (G / 20); Vb = Vh ** 0.4996667741545416; a0 = 1 + K / Q + K * K
    y = signal.lfilter([(Vh + Vb * K / Q + K * K) / a0, 2 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0],
                       [1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0], x)
    f0, Q = 38.13547087602444, 0.5003270373238773; K = np.tan(np.pi * f0 / SR); a0 = 1 + K / Q + K * K
    return signal.lfilter([1, -2, 1], [1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0], y)


def lufs(x):
    y = kweight(x); blk, hop = int(0.4 * SR), int(0.1 * SR)
    z = np.array([np.mean(y[i:i + blk] ** 2) for i in range(0, len(y) - blk, hop)])
    z = z[-0.691 + 10 * np.log10(z + 1e-20) > -70]
    if not len(z): return -99.0
    rel = -0.691 + 10 * np.log10(np.mean(z)) - 10
    z = z[-0.691 + 10 * np.log10(z) > rel]
    return float(-0.691 + 10 * np.log10(np.mean(z)))


def build():
    subprocess.run(["cc", "-O2", "-I" + os.path.join(ROOT, "engine"), os.path.join(ROOT, "tests", "engine", "chain.c"),
                    os.path.join(ROOT, "engine", "px_dsp.c"), os.path.join(ROOT, "firmware", "px_math.c"), "-lm", "-o", EXE],
                   check=True)


def loud(dis, blocks):
    """mean loudness over the DIs of a chain: blocks = [(block, type, [5 knobs]), ...]"""
    args = []
    for b, t, k in blocks: args += [str(b), str(t)] + [str(v) for v in k]
    out = os.path.join(ROOT, "build", "lvl.f32")
    ls = []
    for d in dis:
        subprocess.run([EXE, d, out] + args, check=True)
        ls.append(lufs(np.fromfile(out, np.float32).astype(np.float64)))
    return float(np.mean(ls))


def write(t):
    f = lambda v: "{" + ", ".join("%.2ff" % x for x in v) + "}"
    s = open(TRIM).read()
    s = re.sub(r"(AMP_LVL\[PX_AMP_COUNT\]\[11\] = )\{.*?\};", lambda m: m.group(1) + "{\n    " + ",\n    ".join(f(r) for r in t["AMP"]) + "};", s, flags=re.S)
    for k in NT:
        s = re.sub(r"(%s_DB\[PX_%s_COUNT\] = )\{[^}]*\};" % (k, {"CMP": "CMP", "PIT": "PIT", "SFX": "SFX", "DRV": "DRV", "CAB": "CAB", "MOD": "MOD", "DLY": "DLY", "REV": "REV", "SYN": "SYN"}[k]),
                   lambda m: m.group(1) + f(t[k]) + ";", s)
    open(TRIM, "w").write(s)


def main():
    dis = sys.argv[1:]
    if not dis: sys.exit(__doc__ or "usage: calib_levels.py DI.wav [...]")
    t = {"AMP": [[0.0] * 11 for _ in range(8)]}
    t.update({k: [0.0] * n for k, n in NT.items()})
    write(t); build()
    noon = [50] * 5
    cab0 = (B["CAB"], 0, noon)
    # 1. amps over the GAIN knob
    for a in range(8):
        for i, g in enumerate(range(0, 101, 10)):
            l = loud(dis, [(B["AMP"], a, [g, 50, 50, 50, 50]), cab0])
            t["AMP"][a][i] = round(TARGET + RISE * (g / 100.0 - 0.5) - l, 2)
        print("amp %d:" % a, " ".join("%+.1f" % v for v in t["AMP"][a]), flush=True)
    write(t); build()
    amp = (B["AMP"], 0, noon)
    ref = loud(dis, [amp, cab0])
    print("TWIN at noon + 1x12: %.2f LUFS" % ref)
    # 2. cabinets
    for c in range(NT["CAB"]):
        t["CAB"][c] = round(ref - loud(dis, [amp, (B["CAB"], c, noon)]), 2)
    print("cabs:", t["CAB"]); write(t); build()
    # 3. the other blocks, twice (blocks in front of the amp see its slight curve)
    for it in range(2):
        for k in ("CMP", "PIT", "SFX", "SYN", "DRV", "MOD", "DLY", "REV"):
            for ty in range(NT[k]):
                l = loud(dis, [amp, cab0, (B[k], ty, noon)])
                t[k][ty] = round(float(np.clip(t[k][ty] + ref - l, -15, 15)), 2)
            print(it, k, t[k], flush=True)
        write(t); build()
    # check
    worst = 0.0
    for k in ("CMP", "PIT", "SFX", "SYN", "DRV", "MOD", "DLY", "REV", "CAB"):
        for ty in range(NT[k]):
            ch = [amp, (B[k], ty, noon)] if k == "CAB" else [amp, cab0, (B[k], ty, noon)]
            worst = max(worst, abs(loud(dis, ch) - ref))
    print("largest remaining difference: %.2f dB" % worst)


if __name__ == "__main__":
    main()
