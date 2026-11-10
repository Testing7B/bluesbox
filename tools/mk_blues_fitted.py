#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Write private BluesBox amp and cab tables from locally generated fit results."""
import argparse
import json
from pathlib import Path

import numpy as np

from fit_amp import Engine, FIELDS, read_wav, table_row
from fit_cab import FIELDS as CAB_FIELDS
from mk_nam_factory import cfloat


AMPS = (
    ("SUPER CLEAN", "super-clean-a"),
    ("SUPER EDGE", "super-edge-a"),
    ("DELUXE EDGE", "deluxe-alt"),
    ("TWIN CLEAN", "twin"),
    ("TWIN EDGE", "twin-edge"),
    ("BLONDE BASS", "bassman"),
)
CABS = (
    ("4x10 CTS", "super-cab"),
    ("1x12 P12Q", "deluxe-cab"),
    ("2x12 JBL", "twin-cab"),
    ("2x12 JENSEN", "bassman-cab"),
)


def load_fit(directory, name):
    return json.loads((directory / (name + ".json")).read_text())


def c_array(values):
    return "{" + ", ".join(cfloat(x) for x in values) + "}"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fit_directory", type=Path)
    args = parser.parse_args()
    directory = args.fit_directory
    check = read_wav(str(directory / "check.wav"))
    engine = Engine()
    base_n, base_hard, base = table_row("TWIN")
    # Match the established built-in Twin at gain 50, including its calibrated output gain.
    reference = np.sqrt(np.mean(engine.run(base, base_n, base_hard, check) ** 2)) * 10 ** (26.05 / 20)
    rows, trims, cabinets = [], [], []
    for name, key in AMPS:
        fit = load_fit(directory, key)
        if fit["teacher"] != "a2-full":
            raise ValueError(f"{key} must be fitted to A2 Full")
        values = np.array([fit["values"][f] for f in FIELDS])
        values[FIELDS.index("trim")] = 1
        stages, hard = fit["stages"], fit["hard"]
        row = [f'"{name}"', str(stages), str(hard)]
        row.extend(c_array(values[i:i + 4]) for i in range(0, 16, 4))
        row.extend(cfloat(x) for x in values[16:34])
        rows.append("    {" + ", ".join(row) + "},")
        levels = []
        for gain in range(0, 101, 10):
            level = np.sqrt(np.mean(engine.run(values, stages, hard, check, gain) ** 2))
            if not np.isfinite(level) or level <= 1e-8:
                raise ValueError(f"{key} has invalid output at gain {gain}")
            levels.append(20 * np.log10(reference / level))
        trims.append("    " + c_array(levels) + ",")
        print(f"{name}: fitted from {fit['capture']}, validation ESR {fit['after']['esr_val']:.3f}")
    for name, key in CABS:
        fit = load_fit(directory, key)
        row = [f'"{name}"'] + [cfloat(fit["values"][f]) for f in CAB_FIELDS]
        cabinets.append("    {" + ", ".join(row) + "},")
        print(f"{name}: held-in spectral fit {fit['rms_db_after']:.1f} dB RMS")
    chunks = [
        "/* Generated for local use by tools/mk_blues_fitted.py. */",
        "static const amp_v BLUES_AMP_MODELS[] = {\n" + "\n".join(rows) + "\n};",
        "static const float BLUES_AMP_LEVELS[][11] = {\n" + "\n".join(trims) + "\n};",
        "static const cab_v BLUES_CAB_MODELS[] = {\n" + "\n".join(cabinets) + "\n};",
    ]
    (directory / "px_blues_fitted.h").write_text("\n\n".join(chunks) + "\n")


if __name__ == "__main__":
    main()
