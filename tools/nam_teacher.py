# SPDX-License-Identifier: GPL-3.0-only
"""Render A1 or A2 teachers at the pedal's sample rate for offline fitting and training."""
import hashlib
import json
import math
import os
from pathlib import Path
import tempfile

import numpy as np
from scipy import signal

import nam_a1
import nam_a2


SIZES = {"a2-lite": 3, "a2-full": 8}
CHOICES = ("auto", "a1", *SIZES)


class Teacher:
    def __init__(self, capture, variant="auto"):
        raw = Path(capture).read_bytes()
        data = json.loads(raw)
        self.digest = hashlib.sha256(raw).digest()
        if variant == "a1":
            models = {}
        else:
            models = {nam_a2.is_a2(model): model for model in nam_a2.submodels(data)}
        if variant == "auto":
            variant = next((name for name in ("a2-full", "a2-lite") if SIZES[name] in models), "a1")
        self.variant = variant
        if variant == "a1":
            self.model = nam_a1.load(capture)
            self.run = nam_a1.run
            self.sample_rate = int(self.model["sr"])
        else:
            channels = SIZES[variant]
            if channels not in models:
                raise ValueError(f"{capture} has no {variant} submodel")
            model = models[channels]
            self.model = nam_a2.unpack(model["weights"], channels)
            self.run = nam_a2.run
            self.sample_rate = int(model.get("sample_rate") or data.get("sample_rate") or 48000)

    def render(self, x, sample_rate, cache, highpass=None):
        """Return the reference at sample_rate, optionally with its DC filtered at the model rate."""
        x = np.ascontiguousarray(x, dtype=np.float64)
        key = hashlib.sha256(self.digest)
        key.update(f"{self.variant}:{sample_rate}:{highpass}:".encode())
        key.update(x)
        cache = Path(cache)
        cache = cache.with_name(f"{cache.stem}-{key.hexdigest()[:24]}{cache.suffix}")
        if cache.exists():
            saved = np.fromfile(cache, dtype=np.float32)
            if len(saved) == len(x):
                return saved.astype(np.float64)

        divisor = math.gcd(self.sample_rate, sample_rate)
        up, down = self.sample_rate // divisor, sample_rate // divisor
        y = self.run(self.model, signal.resample_poly(x, up, down))
        if highpass is not None:
            y = signal.sosfilt(signal.butter(2, highpass, "highpass", fs=self.sample_rate, output="sos"), y)
        y = signal.resample_poly(y, down, up)[:len(x)].astype(np.float32)
        cache.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(dir=cache.parent, delete=False) as tmp:
            temporary = Path(tmp.name)
        try:
            with temporary.open("wb") as tmp:
                y.tofile(tmp)
            os.replace(temporary, cache)
        finally:
            temporary.unlink(missing_ok=True)
        return y.astype(np.float64)
