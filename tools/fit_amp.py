#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# OpenPocket — fit one of the engine's amp models to a NAM capture (docs/FITTED-MODELS.md).
#
# The capture is the reference: a real guitar DI goes through NAM's official engine (namrender) and through OUR engine
# (build/fitlib.so: the real px_dsp.c, AMP block alone, knobs at noon), and scipy's least_squares moves the amp's
# numbers (tube stages, filters, tone stack, power amp) until our output matches. What comes out is a row of the
# engine's amp table: the same code runs on the pedal, only the numbers change.
#
#   fit_amp.py --capture X.nam --start "TWIN CLEAN" --stages 1 --name "TWIN" [--di train.wav --val test.wav]
import argparse, ctypes, json, os, re, sys, time, wave
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from scipy import signal
from scipy.optimize import least_squares

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SR = 44100
IN_GAIN = 0.15            # DI (peaks ~0.55) -> the pedal's level when playing hard (-17 dBFS at the ADC, x0.5)
FIELDS = (["g%d" % i for i in range(4)] + ["bias%d" % i for i in range(4)] + ["lp%d" % i for i in range(4)] +
          ["hp%d" % i for i in range(4)] + ["tight", "bright", "bright_db", "bass", "mid", "midq10", "treb", "rb", "rm",
          "rt", "scoop", "pres", "res", "res_db", "vf", "vdb", "sag100", "pw10", "trim", "in_gain"])
LOGF = {"lp0", "lp1", "lp2", "lp3", "hp0", "hp1", "hp2", "hp3", "tight", "bass", "mid", "treb", "pres", "res", "vf", "bright"}
BOUNDS = {"g": (0.05, 20.0), "bias": (-0.5, 0.5), "lp": (1000, 21000), "hp": (5, 600), "tight": (10, 400),
          "bright": (400, 8000), "bright_db": (0, 20), "bass": (50, 500), "mid": (200, 2000), "midq10": (2, 25),
          "treb": (700, 8000), "scoop": (-18, 8), "pres": (1500, 10000), "res": (40, 250), "res_db": (-6, 12),
          "vf": (300, 8000), "vdb": (-15, 15), "sag100": (0, 200), "pw10": (3, 60)}


def table_row(name):
    """the current table row named NAME in engine/px_dsp.c -> (n, hard, values[38])"""
    src = open(os.path.join(ROOT, "engine", "px_dsp.c")).read()
    m = re.search(r'\{"%s",\s*(.*?)\},\s*(?:/\*.*?\*/)?\s*\n' % re.escape(name), src)
    if not m:
        sys.exit("no amp named %s" % name)
    nums = [float(x.rstrip("f")) for x in re.findall(r"-?\d+\.?\d*(?:e[+-]?\d+)?f?", m.group(1))]
    n, hard, rest = int(nums[0]), int(nums[1]), nums[2:]
    v = rest[:16] + rest[16:34]                     # g bias lp hp, then 18 scalars
    return n, hard, np.array(v + [1.0, IN_GAIN], dtype=np.float64)


def read_wav(path):
    w = wave.open(path)
    x = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float64) / 32768.0
    assert w.getframerate() == SR and w.getnchannels() == 1
    return x


class Engine:
    def __init__(self):
        lib = os.path.join(ROOT, "build", "fitlib.so")
        self.lib = ctypes.CDLL(lib)
        f = ctypes.POINTER(ctypes.c_float)
        self.lib.fit_run.argtypes = [f, ctypes.c_int, ctypes.c_int, ctypes.c_int, f, f, ctypes.c_int]

    def run(self, v, n, hard, x, gain_knob=50):
        v32 = np.ascontiguousarray(v, dtype=np.float32)
        x32 = np.ascontiguousarray(x, dtype=np.float32)
        y = np.zeros_like(x32)
        f = ctypes.POINTER(ctypes.c_float)
        self.lib.fit_run(v32.ctypes.data_as(f), n, hard, gain_knob, x32.ctypes.data_as(f), y.ctypes.data_as(f), len(x32))
        return y.astype(np.float64)


def nam_render(capture, x, cache):
    """the capture's output for x (44.1 kHz): resampled to its rate (48 kHz), the A1 network (tools/nam_a1.py), DC
    removed (a real amp's output is AC-coupled), back to 44.1 kHz"""
    if os.path.exists(cache):
        return np.fromfile(cache, dtype=np.float32).astype(np.float64)
    import nam_a1
    m = nam_a1.load(capture)
    sr = int(m["sr"])
    g = np.gcd(sr, SR)
    y = nam_a1.run(m, signal.resample_poly(x, sr // g, SR // g))
    y = signal.sosfilt(signal.butter(2, 8.0, "highpass", fs=sr, output="sos"), y)
    y = signal.resample_poly(y, SR // g, sr // g)[:len(x)]
    y.astype(np.float32).tofile(cache)
    return y


def pre(x):                                         # pre-emphasis: the ear's weight on the upper mids
    return signal.lfilter([1.0, -0.85], [1.0], x)


BANDS = np.unique(np.geomspace(3, 1024, 72).astype(int))


def logspec(x, nper):
    _, _, z = signal.stft(x, fs=SR, nperseg=nper, noverlap=nper // 2, boundary=None, padded=False)
    m = np.abs(z) ** 2
    edges = (BANDS * nper // 2048).clip(1, nper // 2)
    edges = np.unique(edges)
    b = np.add.reduceat(m, edges, axis=0)
    return np.log10(b + 1e-10)


def delay_of(y, ref):
    """y's delay against ref, in samples (fractional): the slope of the cross-spectrum's phase over 150..3000 Hz,
    weighted by coherence (a cross-correlation peak is fooled by a different tone balance)"""
    f, Pxy = signal.csd(ref, y, SR, nperseg=4096)
    _, C = signal.coherence(ref, y, SR, nperseg=4096)
    k = (f > 150) & (f < 3000)
    ph = np.unwrap(np.angle(Pxy[k]))
    w = C[k]
    A = np.vstack([f[k], np.ones(k.sum())]).T * w[:, None]
    slope, _ = np.linalg.lstsq(A, ph * w, rcond=None)[0]
    return -slope / (2 * np.pi) * SR


def shift(y, d):
    """y advanced by d samples (fractional), through the FFT"""
    n = len(y)
    N = 1 << int(np.ceil(np.log2(n + 512)))
    Y = np.fft.rfft(y, N) * np.exp(2j * np.pi * np.fft.rfftfreq(N) * d)
    return np.fft.irfft(Y, N)[:n]


def match(y, ref, lag=None):
    """y aligned on ref (its delay measured each time unless given) and scaled by the best gain -> (y, gain)"""
    d = delay_of(y, ref) if lag is None else lag
    ys = shift(y, d)
    g = float(np.dot(ys, ref) / max(np.dot(ys, ys), 1e-20))
    return ys * g, g


def esr(y, ref):
    return float(np.sum((y - ref) ** 2) / np.sum(ref ** 2))


def best_lag(y, ref, maxlag=None):
    return delay_of(y, ref)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--capture", required=True)
    ap.add_argument("--start", required=True, help="the table row to start from")
    ap.add_argument("--stages", type=int)
    ap.add_argument("--name", required=True)
    ap.add_argument("--di", required=True)
    ap.add_argument("--val", required=True)
    ap.add_argument("--fit-seconds", type=float, default=12.0)
    ap.add_argument("--fit-offset", type=float, default=20.0)
    ap.add_argument("--nfev", type=int, default=300)
    ap.add_argument("--out", required=True)
    ap.add_argument("--loss", choices=("time", "spectral"), default="time",
                    help="time: sample by sample (clean amps); spectral: spectrum + dynamics (distortion)")
    a = ap.parse_args()

    eng = Engine()
    n, hard, v0 = table_row(a.start)
    if a.stages and a.stages != n:                  # more stages: copy the last one
        for i in range(n, a.stages):
            for f in ("g", "bias", "lp", "hp"):
                v0[FIELDS.index("%s%d" % (f, i))] = v0[FIELDS.index("%s%d" % (f, n - 1))]
        n = a.stages
    di = read_wav(a.di)
    s0 = int(a.fit_offset * SR)
    x = di[s0:s0 + int(a.fit_seconds * SR)]
    xv = read_wav(a.val)[:20 * SR]
    tag = os.path.splitext(os.path.basename(a.capture))[0].replace(" ", "_")
    cache = os.path.join(os.path.dirname(a.out) or ".", "cache")
    os.makedirs(cache, exist_ok=True)
    ref = nam_render(a.capture, x, os.path.join(cache, "%s_fit_%d_%d.f32" % (tag, s0, len(x))))
    refv = nam_render(a.capture, xv, os.path.join(cache, "%s_val.f32" % tag))

    # the free numbers, in a space the optimizer likes (log for frequencies)
    free = [f for f in FIELDS[:16] if int(f[-1]) < n] + ["tight", "bass", "mid", "midq10", "treb", "scoop", "pres",
                                                          "res", "res_db", "vf", "vdb", "sag100", "pw10"]
    free += ["bright", "bright_db"]                 # every amp may have a bright cap (0 dB = none)
    if v0[FIELDS.index("bright")] <= 0:
        v0[FIELDS.index("bright")], v0[FIELDS.index("bright_db")] = 2500.0, 0.5
    idx = [FIELDS.index(f) for f in free]

    def lim(f):
        k = re.sub(r"\d$", "", f) if f[:-1] in ("g", "bias", "lp", "hp") else f
        return BOUNDS[k]

    def to_u(v):
        return np.array([np.log(v[i]) if f in LOGF else v[i] for f, i in zip(free, idx)])

    def from_u(u):
        v = v0.copy()
        for f, i, x_ in zip(free, idx, u):
            v[i] = np.exp(x_) if f in LOGF else x_
        if v[FIELDS.index("vdb")] != 0 and v[FIELDS.index("vf")] <= 0:
            v[FIELDS.index("vf")] = 1000.0
        return v

    lo = np.array([np.log(lim(f)[0]) if f in LOGF else lim(f)[0] for f in free])
    hi = np.array([np.log(lim(f)[1]) if f in LOGF else lim(f)[1] for f in free])
    if v0[FIELDS.index("vf")] <= 0:
        v0[FIELDS.index("vf")] = 1000.0
    u0 = np.clip(to_u(v0), lo + 1e-6, hi - 1e-6)

    y0 = eng.run(from_u(u0), n, hard, x)
    lag = best_lag(y0, ref)
    S_ref = [logspec(ref, 2048), logspec(ref, 256)]
    pref = pre(ref)
    nref = np.sqrt(np.mean(pref ** 2))
    W_T, W_S = 1.0, 0.25
    # spectral loss: 1/6-octave long-term spectrum, short-term spectra (2048 / 256), the level envelope (10 ms)
    fb = np.geomspace(60, 16000, 45)

    def ltas(z):
        f, P = signal.welch(z, SR, nperseg=4096)
        return np.array([10 * np.log10(np.mean(P[(f >= lo_) & (f < hi_)]) + 1e-20) for lo_, hi_ in zip(fb[:-1], fb[1:])])

    def env(z, floor):
        e_ = np.sqrt(np.maximum(signal.sosfilt(signal.butter(1, 15, fs=SR, output="sos"), z * z), 1e-12))
        return np.maximum(20 * np.log10(e_[::256]), floor)      # below the floor: silence + the capture's hiss

    def env_floor(r_):
        e_ = np.sqrt(np.maximum(signal.sosfilt(signal.butter(1, 15, fs=SR, output="sos"), r_ * r_), 1e-12))
        return 20 * np.log10(e_.max()) - 50.0

    FL, FLV = env_floor(ref), env_floor(refv)
    L_ref, E_ref = ltas(ref), env(ref, FL)
    rms_ref = np.sqrt(np.mean(ref ** 2))

    def residual_spec(u):
        y = eng.run(from_u(u), n, hard, x)
        y = shift(y, lag) * (rms_ref / max(np.sqrt(np.mean(y ** 2)), 1e-12))
        rl = (ltas(y) - L_ref) / 3.0 / np.sqrt(len(L_ref))                 # 3 dB off everywhere -> 1
        rs = [((logspec(y, nn) - s_) / np.sqrt(s_.size)).ravel() * 0.5 for nn, s_ in zip((2048, 256), S_ref)]
        re_ = (env(y, FL) - E_ref) / 6.0 / np.sqrt(len(E_ref))                 # dynamics: 6 dB off -> 1
        return np.concatenate([rl * 2.0, re_] + rs)

    def spec_dist(y, r_):
        y = shift(y, lag) * (np.sqrt(np.mean(r_ ** 2)) / max(np.sqrt(np.mean(y ** 2)), 1e-12))
        fl = env_floor(r_)
        return {"ltas_db": float(np.sqrt(np.mean((ltas(y) - ltas(r_)) ** 2))),
                "env_db": float(np.sqrt(np.mean((env(y, fl) - env(r_, fl)) ** 2)))}

    def residual(u):
        y = eng.run(from_u(u), n, hard, x)
        ys, _ = match(y, ref)
        rt = (pre(ys) - pref) / nref / np.sqrt(len(ys)) * W_T
        rs = [((logspec(ys, nn) - s) / np.sqrt(s.size)).ravel() * W_S for nn, s in zip((2048, 256), S_ref)]
        return np.concatenate([rt[::2]] + rs)

    def report(u, what):
        v = from_u(u)
        yv, g = match(eng.run(v, n, hard, xv), refv)
        yf, _ = match(eng.run(v, n, hard, x), ref)
        out_ = {"what": what, "esr_fit": esr(pre(yf), pre(ref)), "esr_val": esr(pre(yv), pre(refv)), "gain": g}
        sd = spec_dist(eng.run(v, n, hard, xv), refv)
        out_.update({"val_" + k_: v_ for k_, v_ in sd.items()})
        return out_

    t0 = time.time()
    if a.loss == "spectral":
        # the delay (for the short-term spectra) from a quiet probe, where even a high-gain amp is nearly linear
        rng = np.random.default_rng(1)
        probe = signal.sosfilt(signal.butter(2, [100, 5000], "bandpass", fs=SR, output="sos"), rng.standard_normal(SR * 2)) * 0.01
        pref_ = nam_render(a.capture, probe, os.path.join(cache, "%s_probe.f32" % tag))
        lag = delay_of(eng.run(v0, n, hard, probe), pref_)
        print("delay %.1f samples" % lag, flush=True)
    base = report(u0, "start (%s)" % a.start)
    print(json.dumps(base), flush=True)
    r = least_squares(residual_spec if a.loss == "spectral" else residual, u0, bounds=(lo, hi), method="trf", x_scale="jac", diff_step=2e-3,
                      max_nfev=a.nfev, xtol=1e-12, ftol=1e-9, gtol=1e-12, verbose=1)
    fit = report(r.x, "fitted")
    print(json.dumps(fit), flush=True)
    v = from_u(r.x)
    yv_ = eng.run(v, n, hard, xv)
    v[FIELDS.index("trim")] = (fit["gain"] if a.loss == "time" else
                               float(np.sqrt(np.mean(refv ** 2)) / max(np.sqrt(np.mean(yv_ ** 2)), 1e-12)))
    # (the capture's output level; the table's trims are normalised afterwards, as px_trim.h)
    out = {"name": a.name, "capture": os.path.basename(a.capture), "start": a.start, "stages": n, "hard": hard,
           "lag": float(lag), "values": dict(zip(FIELDS, [float(t) for t in v])), "before": base, "after": fit,
           "seconds": round(time.time() - t0)}
    json.dump(out, open(a.out, "w"), indent=1)
    print("saved", a.out, "%.0f s" % (time.time() - t0))


if __name__ == "__main__":
    main()
