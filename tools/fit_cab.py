#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# OpenPocket — fit one of the engine's cabinets to a measured cabinet response (docs/FITTED-MODELS.md).
# The target is a magnitude response in dB on a frequency grid (with a weight per point, e.g. the coherence); the
# engine's CAB block (build/fitlib_cab.so, knobs at noon) gives its impulse response, compared in 1/12 octaves
# from 60 Hz to 12 kHz after removing the overall level.
#   fit_cab.py --target target.json --start "4x12 V30" --out v30.json
import argparse, ctypes, json, os, re, sys
import numpy as np
from scipy.optimize import least_squares

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SR = 44100
FIELDS = ["hp", "hpq10", "bump", "bump_db", "d1", "d1db", "d2", "d2db", "p1", "p1db", "p2", "p2db", "lp", "lpq"]
BANDS = np.geomspace(60, 12000, 12 * int(np.log2(12000 / 60)) + 1)


def table(name):
    src = open(os.path.join(ROOT, "engine", "px_dsp.c")).read()
    i = src.index("static const cab_v CV[PX_CAB_COUNT] = {")
    rows = re.findall(r'\{"([^"]+)",\s*([^}]*)\}', src[i:src.index("};", i)])
    for k, (n, body) in enumerate(rows):
        if n == name:
            v = [float(t.strip().rstrip("f")) for t in body.split(",")]
            return k, np.array(v + [0.9] * (len(FIELDS) - len(v)))
    sys.exit("no cab named %s" % name)


class Cab:
    def __init__(self):
        self.lib = ctypes.CDLL(os.path.join(ROOT, "build", "fitlib_cab.so"))
        f = ctypes.POINTER(ctypes.c_float)
        self.lib.fit_run_cab.argtypes = [f, ctypes.c_int, f, ctypes.c_int]

    def response(self, v, typ, n=8192):
        f = ctypes.POINTER(ctypes.c_float)
        v32 = np.ascontiguousarray(v, dtype=np.float32)
        h = np.zeros(n, dtype=np.float32)
        self.lib.fit_run_cab(v32.ctypes.data_as(f), typ, h.ctypes.data_as(f), n)
        H = np.fft.rfft(h.astype(np.float64))
        return np.fft.rfftfreq(n, 1 / SR), 20 * np.log10(np.abs(H) + 1e-12)


def bands(f, db, w=None):
    w = np.ones_like(db) if w is None else w
    out, ww = [], []
    for c in BANDS:
        k = (f >= c * 2 ** (-1 / 24)) & (f < c * 2 ** (1 / 24))
        if not k.any():
            k = np.abs(f - c) == np.abs(f - c).min()
        out.append(np.sum(db[k] * w[k]) / max(np.sum(w[k]), 1e-9))
        ww.append(np.mean(w[k]))
    return np.array(out), np.array(ww)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--target", required=True); ap.add_argument("--start", required=True); ap.add_argument("--out", required=True)
    a = ap.parse_args()
    t = json.load(open(a.target))
    tb, tw = bands(np.array(t["f"]), np.array(t["mag_db"]), np.array(t.get("weight", np.ones(len(t["f"])))))
    typ, v0 = table(a.start)
    cab = Cab()
    log = {"hp", "bump", "d1", "d2", "p1", "p2", "lp"}
    lim = {"hp": (30, 200), "hpq10": (5, 15), "bump": (60, 250), "bump_db": (-3, 12), "d1": (300, 4000), "d1db": (-12, 3),
           "d2": (300, 8000), "d2db": (-12, 3), "p1": (800, 6000), "p1db": (-3, 12), "p2": (1500, 8000), "p2db": (-3, 12),
           "lp": (2500, 12000), "lpq": (0.5, 3.0)}
    v0 = v0.copy()
    for k_, f_ in (("d2", 3500.0), ("p2", 4500.0), ("d1", 1500.0), ("p1", 2500.0)):    # a zero frequency = unused: start it
        if v0[FIELDS.index(k_)] <= 0:
            v0[FIELDS.index(k_)] = f_
    to_u = lambda v: np.array([np.log(v[i]) if f in log else v[i] for i, f in enumerate(FIELDS)])
    from_u = lambda u: np.array([np.exp(u[i]) if f in log else u[i] for i, f in enumerate(FIELDS)])
    lo = np.array([np.log(lim[f][0]) if f in log else lim[f][0] for f in FIELDS])
    hi = np.array([np.log(lim[f][1]) if f in log else lim[f][1] for f in FIELDS])
    u0 = np.clip(to_u(v0), lo + 1e-6, hi - 1e-6)

    def curve(u):
        f, db = cab.response(from_u(u), typ)
        return bands(f, db)[0]

    def resid(u):
        d = curve(u) - tb
        d -= np.sum(d * tw) / np.sum(tw)                 # the level does not count
        return d * np.sqrt(tw)

    def rms(u):
        r = resid(u)
        return float(np.sqrt(np.sum(r ** 2) / np.sum(tw)))

    before = rms(u0)
    r = least_squares(resid, u0, bounds=(lo, hi), x_scale="jac", diff_step=1e-3, max_nfev=400)
    after = rms(r.x)
    v = from_u(r.x)
    json.dump({"start": a.start, "type": typ, "values": dict(zip(FIELDS, [float(x) for x in v])),
               "rms_db_before": before, "rms_db_after": after, "bands": BANDS.tolist(), "target": tb.tolist(),
               "fitted": curve(r.x).tolist(), "start_curve": curve(u0).tolist()}, open(a.out, "w"), indent=1)
    print("%s: %.1f dB -> %.1f dB (rms, 60 Hz..12 kHz)" % (a.start, before, after))


if __name__ == "__main__":
    main()
