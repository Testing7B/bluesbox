#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# OpenPocket — trains an A2-Lite network (NAM's 2026 architecture, 3 channels; tools/nam_a2.py) to sound like a
# reference, at the pedal's rate (44.1 kHz). Used for the factory models: the reference is a NAM A1 capture from
# the GPL v3 community collection (pelennor2170/NAM_models), played on real DI guitar (Alec-Wright's GPL v3 data).
#
#   train_a2.py --capture "X.nam" --di train.wav --val val.wav --out model.nam [--steps 3000] [--seconds 120]
#
# 1. The DI (44.1 kHz) is resampled to the capture's 48 kHz, played through the A1 capture (tools/nam_a1.py) and
#    the result resampled back: the target, at 44.1 kHz.
# 2. A2-Lite is trained on (DI, target) with Adam (JAX): error-to-signal ratio on the pre-emphasised signals plus a
#    multi-resolution spectral term, random 1.2 s segments.
# 3. The result is written as a standard .nam file (a WaveNet entry the NAM plugin and tools/nam_a2.py read), with
#    the validation error and the loudness of the output in its metadata.
import argparse, json, os, sys, time, wave
import numpy as np
from scipy.signal import resample_poly

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import nam_a1, nam_a2

SR = 44100
C = 3
RF = 1 + sum((k - 1) * d for k, d in zip(nam_a2.KERNELS, nam_a2.DILATIONS)) + nam_a2.HEAD_K - 1   # 6347


def read_wav(path):
    w = wave.open(path)
    n, ch, sw, sr = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
    raw = np.frombuffer(w.readframes(n), dtype={2: np.int16, 4: np.int32}[sw]).astype(np.float64)
    x = raw.reshape(-1, ch)[:, 0] / (32768.0 if sw == 2 else 2147483648.0)
    if sr != SR:
        raise ValueError("%s: %d Hz, want %d" % (path, sr, SR))
    return x


def reference(capture, x, cache):
    if os.path.exists(cache):
        y = np.fromfile(cache, dtype=np.float32).astype(np.float64)
        if len(y) == len(x):
            return y
    m = nam_a1.load(capture)
    sr = int(m["sr"])
    x48 = resample_poly(x, sr // 300, SR // 300) if sr != SR else x       # 44100 -> 48000: 160 / 147
    y48 = nam_a1.run(m, x48)
    y = resample_poly(y48, SR // 300, sr // 300)[:len(x)] if sr != SR else y48
    y = np.concatenate([y, np.zeros(len(x) - len(y))]) if len(y) < len(x) else y
    os.makedirs(os.path.dirname(cache) or ".", exist_ok=True)
    y.astype(np.float32).tofile(cache)
    return y


# ------------------------------------------------------------------------------------------------ JAX model ---
def build():
    import jax
    import jax.numpy as jnp
    from jax import lax

    def conv(x, w, d):                       # x [B, Cin, T], w [Cout, Cin, K] -> [B, Cout, T - (K-1) d]
        return lax.conv_general_dilated(x, w, (1,), "VALID", rhs_dilation=(d,),
                                        dimension_numbers=("NCH", "OIH", "NCH"))

    def forward(p, u):                       # u [B, T] -> y [B, T - RF + 1]
        u3 = u[:, None, :]
        x = p["rech"][None, :, None] * u3
        hs = None
        for i, (K, d) in enumerate(zip(nam_a2.KERNELS, nam_a2.DILATIONS)):
            L = p["layers"][i]
            z = conv(x, L["conv"], d)
            T = z.shape[2]
            z = z + L["cb"][None, :, None] + L["mix"][None, :, None] * u3[:, :, -T:]
            a = jnp.where(z >= 0, z, 0.01 * z)
            hs = a if hs is None else hs[:, :, -T:] + a
            x = x[:, :, -T:] + jnp.einsum("oc,bct->bot", L["one"], a) + L["ob"][None, :, None]
        y = conv(hs, p["head"][None, :, :], 1)[:, 0, :] + p["hb"]
        return p["scale"] * y

    return jax, jnp, forward


def init_params(rng):
    p = {"rech": rng.normal(0, 1.0, C), "layers": []}
    for K in nam_a2.KERNELS:
        s = 1.0 / np.sqrt(C * K)
        p["layers"].append({"conv": rng.normal(0, s, (C, C, K)), "cb": np.zeros(C), "mix": rng.normal(0, 0.5, C),
                            "one": rng.normal(0, 1.0 / np.sqrt(C), (C, C)), "ob": np.zeros(C)})
    p["head"] = rng.normal(0, 1.0 / np.sqrt(C * nam_a2.HEAD_K), (C, nam_a2.HEAD_K))
    p["hb"] = np.zeros(())
    p["scale"] = np.array(0.02)
    return p


def flatten(p):
    """The .nam weight list (file order, see nam_a2.py)."""
    w = [np.asarray(p["rech"]).ravel()]
    for L in p["layers"]:
        w += [np.asarray(L["conv"]).ravel(), np.asarray(L["cb"]).ravel(), np.asarray(L["mix"]).ravel(),
              np.asarray(L["one"]).ravel(), np.asarray(L["ob"]).ravel()]
    w += [np.asarray(p["head"]).ravel(), np.asarray(p["hb"]).ravel(), np.asarray(p["scale"]).ravel()]
    w = np.concatenate(w).astype(np.float64)
    assert len(w) == nam_a2.n_weights(C), len(w)
    return w


def unflatten(w):
    """flatten's inverse: a .nam weight list -> the parameters (to go on training a model: --init)"""
    w = np.asarray(w, dtype=np.float64)
    i = 0

    def take(shape):
        nonlocal i
        n = int(np.prod(shape)) if shape else 1
        v = w[i:i + n].reshape(shape) if shape else np.array(w[i])
        i += n
        return v
    p = {"rech": take((C,)), "layers": []}
    for K in nam_a2.KERNELS:
        p["layers"].append({"conv": take((C, C, K)), "cb": take((C,)), "mix": take((C,)), "one": take((C, C)),
                            "ob": take((C,))})
    p["head"] = take((C, nam_a2.HEAD_K))
    p["hb"] = take(())
    p["scale"] = take(())
    assert i == len(w)
    return p


def nam_json(weights, meta):
    layer = {"input_size": 1, "condition_size": 1, "head": {"out_channels": 1, "kernel_size": nam_a2.HEAD_K, "bias": True},
             "channels": C, "kernel_sizes": nam_a2.KERNELS, "dilations": nam_a2.DILATIONS,
             "activation": [{"type": "LeakyReLU", "negative_slope": 0.01}] * len(nam_a2.KERNELS),
             "bottleneck": C, "head1x1": {"active": False, "out_channels": 1, "groups": 1},
             "layer1x1": {"active": True, "groups": 1}, "groups_input": 1, "groups_input_mixin": 1,
             "gating_mode": ["none"] * len(nam_a2.KERNELS), "secondary_activation": [None] * len(nam_a2.KERNELS),
             "slimmable": None}
    for k in ("conv_pre_film", "conv_post_film", "input_mixin_pre_film", "input_mixin_post_film",
              "activation_pre_film", "activation_post_film", "layer1x1_post_film", "head1x1_post_film"):
        layer[k] = {"active": False, "shift": True, "groups": 1}
    return {"version": "0.7.0", "architecture": "WaveNet", "metadata": meta,
            "config": {"layers": [layer], "head": None, "head_scale": float(weights[-1])},
            "weights": [float(v) for v in weights], "sample_rate": SR}


def preemph(x, jnp):
    return x[:, 1:] - 0.85 * x[:, :-1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--capture", required=True)
    ap.add_argument("--di", required=True)
    ap.add_argument("--val", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--name", default="")
    ap.add_argument("--steps", type=int, default=3000)
    ap.add_argument("--seconds", type=float, default=120.0)
    ap.add_argument("--batch", type=int, default=8)
    ap.add_argument("--seg", type=int, default=8192)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--lr", type=float, default=4e-3)
    ap.add_argument("--init", help="start from this A2-Lite .nam (go on training it) instead of random weights")
    a = ap.parse_args()
    t0 = time.time()
    cache = os.path.join(os.path.dirname(os.path.abspath(a.out)), "cache")
    tag = os.path.splitext(os.path.basename(a.capture))[0].replace(" ", "_").replace(",", "_")
    x = read_wav(a.di)[: int(a.seconds * SR)]
    xv = read_wav(a.val)[: 20 * SR]
    y = reference(a.capture, x, os.path.join(cache, "%s_train_%d.f32" % (tag, len(x))))
    yv = reference(a.capture, xv, os.path.join(cache, "%s_val_%d.f32" % (tag, len(xv))))
    print("%s: reference ready (%.0f s), peaks in %.2f out %.2f" % (tag, time.time() - t0, np.max(abs(x)), np.max(abs(y))),
          flush=True)

    jax, jnp, forward = build()
    import optax
    rng = np.random.default_rng(a.seed)
    if a.init:
        m0, c0 = nam_a2.pick(json.load(open(a.init)), "lite")
        assert c0 == C, "--init: not an A2-Lite"
        params = jax.tree_util.tree_map(jnp.asarray, unflatten(m0["weights"]))
    else:
        params = jax.tree_util.tree_map(jnp.asarray, init_params(rng))
    # the output scale stays fixed (NAM keeps head_scale outside training); the rest learns
    win = a.seg + RF - 1

    def stft_mag(s, n):
        hop = n // 4
        frames = jnp.stack([s[:, i:i + n] for i in range(0, s.shape[1] - n + 1, hop)], axis=1)
        return jnp.abs(jnp.fft.rfft(frames * jnp.hanning(n), axis=-1))

    def loss_fn(p, u, t):
        yhat = forward(p, u)
        e1 = preemph(yhat, jnp) - preemph(t, jnp)
        esr = jnp.sum(e1 ** 2) / (jnp.sum(preemph(t, jnp) ** 2) + 1e-9)
        spec = 0.0
        for n in (256, 1024, 2048):
            A, B = stft_mag(yhat, n), stft_mag(t, n)
            spec = spec + jnp.mean(jnp.abs(jnp.log(A + 1e-5) - jnp.log(B + 1e-5)))
        return esr + 0.002 * spec

    sched = optax.warmup_cosine_decay_schedule(0.0, a.lr, 100, a.steps, a.lr * 0.02)
    opt = optax.chain(optax.clip_by_global_norm(1.0), optax.adam(sched))
    frozen = {"scale"}
    st = opt.init(params)

    @jax.jit
    def step(p, s, u, t):
        l, g = jax.value_and_grad(loss_fn)(p, u, t)
        g = dict(g); g["scale"] = jnp.zeros_like(g["scale"])
        upd, s = opt.update(g, s, p)
        return optax.apply_updates(p, upd), s, l

    fwd = jax.jit(forward)

    def val_esr(p):
        # whole validation clip in pieces with their warm-up
        out = []
        n = 4 * SR
        xp = np.concatenate([np.zeros(RF - 1), xv])
        for i in range(0, len(xv), n):
            seg = xp[i:i + n + RF - 1]
            if len(seg) < RF:
                break
            out.append(np.asarray(fwd(p, jnp.asarray(seg[None, :]))[0]))
        yh = np.concatenate(out)[:len(yv)]
        return float(np.sum((yh - yv) ** 2) / np.sum(yv ** 2)), yh

    best = (1e9, None)
    xs = np.concatenate([np.zeros(RF - 1), x])
    for it in range(1, a.steps + 1):
        idx = rng.integers(0, len(x) - a.seg, a.batch)
        u = np.stack([xs[i:i + win] for i in idx])
        t = np.stack([y[i:i + a.seg] for i in idx])
        params, st, l = step(params, st, jnp.asarray(u), jnp.asarray(t))
        if it % 250 == 0 or it == a.steps:
            ev, _ = val_esr(params)
            if ev < best[0]:
                best = (ev, jax.tree_util.tree_map(np.asarray, params))
            print("  step %5d  loss %.4f  val ESR %.4f  (%.0f s)" % (it, float(l), ev, time.time() - t0), flush=True)
    ev, p = best
    w = flatten(p)
    net = nam_a2.unpack(w, C)
    yh = nam_a2.run(net, xv)
    esr = float(np.sum((yh - yv) ** 2) / np.sum(yv ** 2))
    meta = {"name": a.name or tag, "trained_from": os.path.basename(a.capture), "val_esr": round(esr, 5),
            "loudness": float(10 * np.log10(np.mean(yh ** 2) + 1e-12)), "trainer": "OpenPocket tools/train_a2.py",
            "licence": "GPL-3.0 (derived from a GPL-3.0 capture of pelennor2170/NAM_models)"}
    json.dump(nam_json(w, meta), open(a.out, "w"))
    print("%s -> %s: val ESR %.4f (%.0f s)" % (tag, a.out, esr, time.time() - t0), flush=True)


if __name__ == "__main__":
    main()
