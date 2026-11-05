#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# OpenPocket — A/B listening files for a fitted amp (tools/fit_amp.py): the same guitar DI (a passage the fit never
# heard) through 1. the capture (NAM, the real amp), 2. the fitted model, 3. the current model; each followed by the
# same cabinet so it sounds like a guitar amp, levels matched, and written as one WAV: capture / fitted / current.
#   fit_ab.py FIT.json CAPTURE.nam DI.wav OUT.wav [--seconds 10 --offset 0]
import argparse, json, os, sys, wave
import numpy as np
from scipy import signal
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fit_amp as F


def cab(x):
    """a neutral 4x12-ish cabinet curve (same for all three), so the amps can be judged as they would be heard"""
    sos = np.vstack([signal.butter(2, 80, "highpass", fs=F.SR, output="sos"),
                     signal.butter(4, 5000, "lowpass", fs=F.SR, output="sos")])
    y = signal.sosfilt(sos, x)
    b, a = signal.iirpeak(120 / (F.SR / 2), 2.0)            # cabinet thump
    return y + 0.4 * signal.lfilter(b, a, y)


def write(path, x):
    x = np.clip(x, -1, 1)
    w = wave.open(path, "wb")
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(F.SR)
    w.writeframes((x * 32767).astype("<i2").tobytes())
    w.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("fit"); ap.add_argument("capture"); ap.add_argument("di"); ap.add_argument("out")
    ap.add_argument("--seconds", type=float, default=10.0)
    ap.add_argument("--offset", type=float, default=0.0)
    a = ap.parse_args()
    fit = json.load(open(a.fit))
    eng = F.Engine()
    s0 = int(a.offset * F.SR)
    x = F.read_wav(a.di)[s0:s0 + int(a.seconds * F.SR)]
    ref = F.nam_render(a.capture, x, a.out + ".ref.f32")
    v = np.array([fit["values"][f] for f in F.FIELDS])
    yf = eng.run(v, fit["stages"], fit["hard"], x)
    n0, h0, v0 = F.table_row(fit["start"])
    y0 = eng.run(v0, n0, h0, x)
    rms = lambda z: np.sqrt(np.mean(z ** 2))

    def align(y):                                   # its own delay and polarity, then the capture's RMS level
        ys = F.shift(y, F.delay_of(y, ref))
        if np.dot(ys, ref) < 0:
            ys = -ys
        return ys * (rms(ref) / rms(ys))
    yf, y0 = align(yf), align(y0)
    res = {"esr_fitted": F.esr(F.pre(yf), F.pre(ref)), "esr_current": F.esr(F.pre(y0), F.pre(ref))}
    gap = np.zeros(int(0.6 * F.SR))
    parts = [cab(ref), cab(yf), cab(y0)]
    peak = max(np.abs(p).max() for p in parts)
    write(a.out, np.concatenate([np.concatenate([p / peak * 0.8, gap]) for p in parts]))
    os.remove(a.out + ".ref.f32")
    print(json.dumps(res))


if __name__ == "__main__":
    main()
