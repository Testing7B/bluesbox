#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# OpenPocket — fit one of the engine's drive pedals to a NAM capture of the pedal (docs/FITTED-MODELS.md). Same method
# as tools/fit_amp.py, on the DRIVE block (knobs at noon): filters before the clipper, gain, the clipper's bias, the
# tone filter after it, the clean blend. The clipper's shape (soft, germanium, hard...) is kept from the start row.
#   fit_drive.py --capture X.nam --start "TS 9" --name TS9 --di train.wav --val test.wav --out ts9.json [--loss spectral]
import argparse, ctypes, json, os, re, sys, time
import numpy as np
from scipy import signal
from scipy.optimize import least_squares
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fit_amp as F

ROOT = F.ROOT
SR = F.SR
FIELDS = ["hp", "shelf", "lp", "mid", "mid_db", "mid_q10", "g0", "g1", "t0", "t1", "p2", "p2db", "blend", "bias",
          "gate", "trim", "in_gain"]
ROLES = ["R_DRIVE", "R_DIST", "R_FUZZ", "R_SUST", "R_BOOST", "R_TONE", "R_TREBLE", "R_LEVEL", "R_BASS", "R_VOICE",
         "R_GATE", "R_BIAS", "R_LOW", "R_HIGH", "R_TIGHT", "R_BRIGHT", "R_BLEND"]
CLIPS = ["CL_SOFT", "CL_ASYM", "CL_HARD", "CL_GE", "CL_LED", "CL_MOS", "CL_MUFF", "CL_FUZZ", "CL_OCTA", "CL_BOOST"]
TMS = ["TM_LP", "TM_MUFF", "TM_HM2", "TM_MT2"]


def table_row(name):
    src = open(os.path.join(ROOT, "engine", "px_dsp.c")).read()
    m = re.search(r'\{"%s",\s*L(\d)\(([^)]*)\),(.*?)\},\s*\n' % re.escape(name), src)
    if not m:
        sys.exit("no drive named %s" % name)
    np_ = int(m.group(1))
    roles = [ROLES.index(r.strip()) for r in m.group(2).split(",")] + [0] * (5 - np_)
    tok = [t.strip() for t in m.group(3).split(",") if t.strip()]
    nums, clip, tm = [], None, None
    for t in tok:
        if t in CLIPS: clip = CLIPS.index(t)
        elif t in TMS: tm = TMS.index(t)
        else: nums.append(float(t.rstrip("f")))
    hp, shelf, lp, mid, mid_db, mid_q10, g0, g1 = nums[:8]
    t0, t1, p2, p2db, blend, bias, gate = nums[8:15]
    v = np.array([hp, shelf, lp, mid, mid_db, mid_q10, g0, g1, t0, t1, p2, p2db, blend, bias, gate, 1.0, F.IN_GAIN])
    return clip, tm, roles, np_, v


class Engine:
    def __init__(self):
        self.lib = ctypes.CDLL(os.path.join(ROOT, "build", "fitlib_drv.so"))
        f = ctypes.POINTER(ctypes.c_float)
        self.lib.fit_run_drive.argtypes = [f, ctypes.c_int, ctypes.c_int, ctypes.POINTER(ctypes.c_ubyte), ctypes.c_int, f,
                                           f, ctypes.c_int]

    def run(self, v, clip, tm, roles, np_, x):
        f = ctypes.POINTER(ctypes.c_float)
        v32 = np.ascontiguousarray(v, dtype=np.float32)
        x32 = np.ascontiguousarray(x, dtype=np.float32)
        r8 = (ctypes.c_ubyte * 5)(*roles)
        y = np.zeros_like(x32)
        self.lib.fit_run_drive(v32.ctypes.data_as(f), clip, tm, r8, np_, x32.ctypes.data_as(f), y.ctypes.data_as(f), len(x32))
        return y.astype(np.float64)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--capture", required=True); ap.add_argument("--start", required=True)
    ap.add_argument("--name", required=True); ap.add_argument("--di", required=True); ap.add_argument("--val", required=True)
    ap.add_argument("--out", required=True); ap.add_argument("--nfev", type=int, default=250)
    ap.add_argument("--loss", choices=("time", "spectral"), default="time")
    ap.add_argument("--clip", help="force a clipper (e.g. CL_LED)")
    a = ap.parse_args()
    eng = Engine()
    clip, tm, roles, np_, v0 = table_row(a.start)
    if a.clip:
        clip = CLIPS.index(a.clip)
    di = F.read_wav(a.di)
    x = di[20 * SR:28 * SR]
    xv = F.read_wav(a.val)[:20 * SR]
    tag = os.path.splitext(os.path.basename(a.capture))[0].replace(" ", "_")[:60]
    cache = os.path.join(os.path.dirname(a.out) or ".", "cache")
    os.makedirs(cache, exist_ok=True)
    ref = F.nam_render(a.capture, x, os.path.join(cache, "%s_fit.f32" % tag))
    refv = F.nam_render(a.capture, xv, os.path.join(cache, "%s_val.f32" % tag))

    # free numbers: (name, getter -> u, setter u -> v); frequencies in log; the gain range and the tone range move as
    # a whole (only noon is captured)
    P = []

    def add(name, lo, hi, log=False, cond=True):
        if cond:
            P.append((name, lo, hi, log))
    add("hp", 10, 1000, True)
    add("shelf", 100, 3000, True, v0[1] > 0)
    add("lp", 500, 12000, True, v0[2] > 0)
    add("mid", 100, 6000, True, v0[3] > 0)
    add("mid_db", -12, 15, False, v0[3] > 0)
    add("mid_q10", 3, 30, False, v0[3] > 0)
    add("gain", -20, 70)                              # the gain at noon (g0, g1 keep their spread)
    add("tone", 100, 20000, True)                     # TM_LP: the geometric mean; others: t0 and t1 together
    if tm in (2, 3):
        add("t1r", 0.2, 5, True)                      # HM-2 / MT-2: the 2nd band's ratio
    add("p2", 300, 12000, True, v0[10] > 0)
    add("p2db", -15, 15, False, v0[10] > 0 and v0[11] != 0)
    add("blend", 0, 100, False, v0[12] > 0)
    add("bias", 0, 60, False, clip in (1, 7))
    spread = v0[7] - v0[6]
    tratio = v0[9] / v0[8]

    def from_u(u):
        v = v0.copy()
        for (name, lo, hi, log), x_ in zip(P, u):
            val = np.exp(x_) if log else x_
            if name == "gain":
                v[6], v[7] = val - spread / 2, val + spread / 2
            elif name == "tone":
                if tm == 0:
                    v[8], v[9] = val / np.sqrt(tratio), val * np.sqrt(tratio)
                else:
                    v[8] = val
            elif name == "t1r":
                v[9] = v[8] * val * tratio
            else:
                v[FIELDS.index(name)] = val
        return v

    def to_u():
        out = []
        for name, lo, hi, log in P:
            if name == "gain": val = (v0[6] + v0[7]) / 2
            elif name == "tone": val = np.sqrt(v0[8] * v0[9]) if tm == 0 else v0[8]
            elif name == "t1r": val = 1.0
            else: val = v0[FIELDS.index(name)]
            val = min(max(val, lo * 1.001 if lo > 0 else lo + 1e-3), hi * 0.999)
            out.append(np.log(val) if log else val)
        return np.array(out)

    lo = np.array([np.log(p[1]) if p[3] else p[1] for p in P])
    hi = np.array([np.log(p[2]) if p[3] else p[2] for p in P])
    u0 = np.clip(to_u(), lo + 1e-6, hi - 1e-6)
    lag = F.delay_of(eng.run(from_u(u0), clip, tm, roles, np_, x), ref)
    S_ref = [F.logspec(ref, 2048), F.logspec(ref, 256)]
    pref = F.pre(ref)
    nref = np.sqrt(np.mean(pref ** 2))
    fb = np.geomspace(60, 16000, 45)

    def ltas(z):
        f, P_ = signal.welch(z, SR, nperseg=4096)
        return np.array([10 * np.log10(np.mean(P_[(f >= l_) & (f < h_)]) + 1e-20) for l_, h_ in zip(fb[:-1], fb[1:])])
    L_ref = ltas(ref)
    rms = lambda z: np.sqrt(np.mean(z ** 2))

    def residual(u):
        y = eng.run(from_u(u), clip, tm, roles, np_, x)
        if a.loss == "time":
            ys, _ = F.match(y, ref)
            rt = (F.pre(ys) - pref) / nref / np.sqrt(len(ys))
            rs = [((F.logspec(ys, nn) - s) / np.sqrt(s.size)).ravel() * 0.25 for nn, s in zip((2048, 256), S_ref)]
            return np.concatenate([rt[::2]] + rs)
        y = F.shift(y, lag) * (rms(ref) / max(rms(y), 1e-12))
        rl = (ltas(y) - L_ref) / 3.0 / np.sqrt(len(L_ref))
        rs = [((F.logspec(y, nn) - s) / np.sqrt(s.size)).ravel() * 0.5 for nn, s in zip((2048, 256), S_ref)]
        return np.concatenate([rl * 2.0] + rs)

    def report(u, what):
        v = from_u(u)
        yv = eng.run(v, clip, tm, roles, np_, xv)
        ys, g = F.match(yv, refv)
        yr = F.shift(yv, lag) * (rms(refv) / max(rms(yv), 1e-12))
        return {"what": what, "esr_val": F.esr(F.pre(ys), F.pre(refv)), "gain": g,
                "val_ltas_db": float(np.sqrt(np.mean((ltas(yr) - ltas(refv)) ** 2)))}

    t0 = time.time()
    base = report(u0, "start (%s)" % a.start)
    print(json.dumps(base), flush=True)
    r = least_squares(residual, u0, bounds=(lo, hi), method="trf", x_scale="jac", diff_step=2e-3, max_nfev=a.nfev,
                      xtol=1e-12, ftol=1e-9, gtol=1e-12, verbose=1)
    fit = report(r.x, "fitted")
    print(json.dumps(fit), flush=True)
    v = from_u(r.x)
    yv = eng.run(v, clip, tm, roles, np_, xv)
    v[FIELDS.index("trim")] = abs(fit["gain"]) if a.loss == "time" else float(rms(refv) / max(rms(yv), 1e-12))
    json.dump({"name": a.name, "capture": os.path.basename(a.capture), "start": a.start, "clip": CLIPS[clip],
               "tm": TMS[tm], "roles": [ROLES[r_] for r_ in roles[:np_]], "lag": float(lag),
               "values": dict(zip(FIELDS, [float(t) for t in v])), "before": base, "after": fit,
               "loss": a.loss, "seconds": round(time.time() - t0)}, open(a.out, "w"), indent=1)
    print("saved", a.out)


if __name__ == "__main__":
    main()
