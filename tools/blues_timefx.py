#!/usr/bin/env python3
"""Measure delay and reverb from matched dry, delay-only and reverb-only burst captures."""
# SPDX-License-Identifier: GPL-3.0-only
import argparse
import json

import numpy as np
from scipy import signal
from scipy.io import wavfile


def read_mono(path):
    rate, audio = wavfile.read(path)
    if audio.ndim != 1:
        raise ValueError(f"{path}: expected a mono recording")
    if np.issubdtype(audio.dtype, np.integer):
        audio = audio.astype(float) / (np.iinfo(audio.dtype).max + 1)
    return rate, np.asarray(audio, dtype=float)


def rms(x):
    return np.sqrt(np.mean(x * x)) if len(x) else 0.0


def db(x):
    return 20 * np.log10(max(float(x), 1e-12))


def align_and_subtract(dry, wet, onset, rate):
    width = int(0.042 * rate)
    guard = int(0.055 * rate)
    if onset < guard or onset + width + guard >= len(wet):
        raise ValueError("The dry burst needs at least 55 ms of leading silence")
    ref = dry[onset:onset + width]
    field = wet[onset - guard:onset + guard + width]
    lag = int(np.argmax(signal.correlate(field, ref, mode="valid"))) - guard
    aligned = np.zeros_like(dry)
    if lag >= 0:
        available = min(len(dry), len(wet) - lag)
        aligned[:available] = wet[lag:lag + available]
    else:
        available = min(len(wet), len(dry) + lag)
        aligned[-lag:-lag + available] = wet[:available]
    front = aligned[onset:onset + width]
    correlation = float(np.dot(front, ref) / max(np.linalg.norm(front) * np.linalg.norm(ref), 1e-20))
    if correlation < 0.75:
        raise ValueError(f"Direct burst does not match the dry capture (correlation {correlation:.2f})")
    gain = np.dot(front, ref) / np.dot(ref, ref)
    return aligned - gain * dry, rms(front), lag


def estimate_delay(residual, dry, direct, onset, rate, expected_ms=None):
    band = signal.butter(2, [180, 2300], fs=rate, btype="bandpass", output="sos")
    filtered = signal.sosfiltfilt(band, residual)
    width = int(.045 * rate)
    ref = signal.sosfiltfilt(band, dry)[onset:onset + width]
    match = signal.correlate(filtered, ref, mode="valid", method="fft")
    match /= max(np.linalg.norm(ref), 1e-20)
    lo, hi = .070, min(1.5, (len(match) - onset) / rate)
    if expected_ms is not None:
        lo, hi = max(lo, expected_ms / 1000 - .045), min(hi, expected_ms / 1000 + .045)
    lo, hi = onset + int(lo * rate), onset + int(hi * rate)
    if hi <= lo:
        raise ValueError("Capture is too short for the requested delay search")
    peak = lo + int(np.argmax(abs(match[lo:hi])))
    echo = filtered[peak:peak + width]
    similarity = abs(match[peak]) / max(np.linalg.norm(echo), 1e-20)
    ratio = db(rms(residual[peak:peak + width]) / direct)
    floor = rms(filtered[:max(1, onset - int(.03 * rate))])
    snr = db(rms(echo) / max(floor, 1e-12))
    if similarity < .30 or snr < 14 or ratio < -48:
        return {"detected": False}
    return {"detected": True, "first_repeat_ms": round((peak-onset)*1000/rate, 1),
            "first_repeat_db": round(ratio, 1), "similarity": round(float(similarity), 2)}


def estimate_reverb(residual, direct, onset, rate):
    tail_start = onset + int(.075 * rate)
    stop = len(residual) - int(.08 * rate)
    tail = residual[tail_start:stop]
    if len(tail) < int(.40 * rate):
        raise ValueError("Capture is too short for reverb analysis")
    floor = rms(residual[:max(1, onset - int(.03 * rate))])
    early = rms(tail[:int(.08 * rate)])
    later = rms(tail[int(.20 * rate):int(.32 * rate)])
    relative = db(early / direct)
    snr = db(early / max(floor, 1e-12))
    # A short room can be quiet. At lower SNR require an observable decay
    # relative to a later window as well as energy above the noise floor.
    if relative < -55 or (snr < 14 and (snr < 6 or db(early / max(later, 1e-12)) < 3)):
        return {"detected": False}
    # Truncate the integral once a 50 ms window reaches the measurement floor.
    frame = int(.05 * rate)
    count = len(tail) // frame
    power = np.mean(tail[:count*frame].reshape(count, frame)**2, axis=1)
    audible = np.flatnonzero(power > (max(floor, 1e-9) * 3.16)**2)
    end = int((audible[-1] + 1) * frame) if len(audible) else len(tail)
    decay = np.maximum(np.cumsum((tail[:end]**2-floor**2)[::-1])[::-1], 1e-30)
    curve = 10*np.log10(decay/max(decay[0], 1e-30))
    selection = (curve <= -5) & (curve >= -25)
    t = np.arange(len(tail[:end])) / rate
    rt = None
    if np.count_nonzero(selection) > int(.08 * rate) and np.min(curve) < -25:
        fit, offset = np.polyfit(t[selection], curve[selection], 1)
        predicted = fit*t[selection]+offset
        error = np.std(predicted-curve[selection])
        if fit < 0 and error < 2:
            rt = round(float(-60/fit), 2)
    return {"detected": True, "tail_db": round(relative, 1), "tail_snr_db": round(snr, 1),
            "confidence": "high" if snr >= 14 else "limited", "rt60_t20_seconds": rt}


def analyze(dry, delay_only, reverb_only, rate, expected_ms=None):
    if len(dry) < rate or not np.isfinite(dry).all() or rms(dry) < 1e-6:
        raise ValueError("Dry capture has no usable burst")
    active = np.flatnonzero(abs(dry) > np.max(abs(dry)) * .12)
    onset = int(active[0])
    delay, dry_delay, dly_lag = align_and_subtract(dry, delay_only, onset, rate)
    reverb, dry_rev, rev_lag = align_and_subtract(dry, reverb_only, onset, rate)
    return {"delay": estimate_delay(delay, dry, dry_delay, onset, rate, expected_ms),
            "reverb": estimate_reverb(reverb, dry_rev, onset, rate),
            "alignment_samples": [dly_lag, rev_lag]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("dry", "delay_only", "reverb_only"):
        parser.add_argument(key, help="mono WAV from the same gated burst")
    parser.add_argument("--expected-ms", type=float, help="look within 45 ms of a known delay setting")
    args = parser.parse_args()
    captures = [read_mono(p) for p in (args.dry, args.delay_only, args.reverb_only)]
    rates = {x[0] for x in captures}
    if len(rates) != 1:
        parser.error("all captures must have the same sample rate")
    print(json.dumps(analyze(*(x[1] for x in captures), rates.pop(), args.expected_ms), indent=2))


if __name__ == "__main__":
    main()
