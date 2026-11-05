#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# OpenPocket — a small, independent NumPy implementation of NAM's A1 WaveNet (the "standard / lite / feather / nano"
# captures), used only as the REFERENCE when fitting the pedal's amp models (tools/fit_amp.py). A .nam file is JSON:
# the architecture's config and a flat list of weights; this reads both and runs the network, whole file at a time.
#
# Network (per layer array): rechannel 1x1 (no bias) -> layers [dilated causal conv (bias) + input mixin from the raw
# input (no bias) -> activation -> added to the head accumulator; residual: x + 1x1(z) (bias)] -> head rechannel.
# Array k+1 takes array k's residual stream as input and its head output as the start of its head accumulator. The
# model's output is head_scale x the last head. Weights are stored in that order, out x in (x kernel), then biases.
import json, sys
import numpy as np


class Reader:
    def __init__(self, w):
        self.w = np.asarray(w, dtype=np.float64)
        self.i = 0

    def take(self, n):
        r = self.w[self.i:self.i + n]
        if len(r) != n:
            raise ValueError("not enough weights")
        self.i += n
        return r


def conv_weights(rd, cout, cin, k, bias):
    w = rd.take(cout * cin * k).reshape(cout, cin, k)   # order: out, in, kernel
    b = rd.take(cout) if bias else np.zeros(cout)
    return w, b


def load(path):
    j = json.load(open(path))
    if j.get("architecture") != "WaveNet":
        raise ValueError("only A1 WaveNet captures: %s" % j.get("architecture"))
    cfg = j["config"]
    rd = Reader(j["weights"])
    arrays = []
    for la in cfg["layers"]:
        if la.get("gated"):
            raise ValueError("gated layers not supported")
        if la.get("activation", "Tanh") != "Tanh":
            raise ValueError("activation %s not supported" % la.get("activation"))
        c, k = la["channels"], la["kernel_size"]
        a = {"cfg": la, "rechannel": conv_weights(rd, c, la["input_size"], 1, False), "layers": []}
        for d in la["dilations"]:
            conv = conv_weights(rd, c, c, k, True)
            mix = conv_weights(rd, c, la["condition_size"], 1, False)
            one = conv_weights(rd, c, c, 1, True)
            a["layers"].append((d, conv, mix, one))
        a["head"] = conv_weights(rd, la["head_size"], c, 1, la.get("head_bias", False))
        arrays.append(a)
    head_scale = float(rd.take(1)[0])
    if cfg.get("head") is not None:
        raise ValueError("post-head not supported")
    if rd.i != len(rd.w):
        raise ValueError("weights left over: %d of %d used" % (rd.i, len(rd.w)))
    return {"arrays": arrays, "head_scale": head_scale, "sr": float(j.get("sample_rate") or 48000.0),
            "nweights": len(rd.w)}


def run(model, x):
    """x: 1-D float array at the model's sample rate -> 1-D output"""
    x = np.asarray(x, dtype=np.float64)[None, :]          # (1, T): also the condition
    cond = x
    T = x.shape[1]
    h_in, head = x, None
    for a in model["arrays"]:
        w, _ = a["rechannel"]
        h = w[:, :, 0] @ h_in
        acc = np.zeros((a["cfg"]["channels"], T)) if head is None else head.copy()
        for d, (cw, cb), (mw, _), (ow, ob) in a["layers"]:
            K = cw.shape[2]
            z = cb[:, None] + mw[:, :, 0] @ cond
            for k in range(K):
                off = d * (K - 1 - k)                          # tap k looks back d*(K-1-k) samples
                if off == 0:
                    z += cw[:, :, k] @ h
                else:
                    z[:, off:] += cw[:, :, k] @ h[:, :-off]
            z = np.tanh(z)
            acc += z
            h = h + ow[:, :, 0] @ z + ob[:, None]
        hw, hb = a["head"]
        head = hw[:, :, 0] @ acc + hb[:, None]
        h_in = h
    return model["head_scale"] * head[0]


if __name__ == "__main__":
    m = load(sys.argv[1])
    print("weights", m["nweights"], "arrays", [(a["cfg"]["channels"], len(a["layers"])) for a in m["arrays"]],
          "head_scale %.4f" % m["head_scale"], "sr", m["sr"])
