#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# OpenPocket — the level trim of a NAM model: the same loudness for every model. The model plays docs/nam_di.wav
# (6 s of real DI guitar, Alec-Wright/Automated-GuitarAmpModelling, GPL v3) at its own level; trim = REF - loudness
# (ITU BS.1770 K-weighting, 400 ms blocks, absolute and relative gates). docs/pxedit.js computes the same for the
# models the web editor loads (namLevelTrim): keep the two in step.
import os, sys, wave
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SR = 44100
REF = -20.0              # LUFS of the model's own output after the trim (v37: NAM + cabinet ~ the fitted amps at noon)
CLAMP = 24.0


def kweight(x):
    # BS.1770 pre-filter + RLB high-pass at 44.1 kHz (same coefficients as tools/calib_levels.py)
    from scipy import signal
    f0, G, Q = 1681.974450955533, 3.999843853973347, 0.7071752369554196
    K = np.tan(np.pi * f0 / SR); Vh = 10 ** (G / 20); Vb = Vh ** 0.4996667741545416; a0 = 1 + K / Q + K * K
    y = signal.lfilter([(Vh + Vb * K / Q + K * K) / a0, 2 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0],
                       [1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0], x)
    f0, Q = 38.13547087602444, 0.5003270373238773; K = np.tan(np.pi * f0 / SR); a0 = 1 + K / Q + K * K
    return signal.lfilter([1, -2, 1], [1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0], y)


def lufs(x):
    y = kweight(x); blk, hop = int(0.4 * SR), int(0.1 * SR)
    z = np.array([np.mean(y[i:i + blk] ** 2) for i in range(0, len(y) - blk + 1, hop)])
    z = z[-0.691 + 10 * np.log10(z + 1e-20) > -70]
    if not len(z):
        return -99.0
    rel = -0.691 + 10 * np.log10(np.mean(z)) - 10
    z = z[-0.691 + 10 * np.log10(z) > rel]
    return float(-0.691 + 10 * np.log10(np.mean(z)))


def di(path=None):
    w = wave.open(path or os.path.join(ROOT, "docs", "nam_di.wav"))
    return np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float64) / 32768.0


def trim_db(net, path=None):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import nam_a2
    y = nam_a2.run(net, di(path))
    t = REF - lufs(y)
    return float(max(-CLAMP, min(CLAMP, t)))


if __name__ == "__main__":
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import nam_a2
    for p in sys.argv[1:]:
        print("%s: trim %+.2f dB" % (p, trim_db(nam_a2.load(p))))
