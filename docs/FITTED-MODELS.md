# Fitted amps, drives and cabinets (v34)

From v34, OpenPocket's amps, drive pedals and the 4x12 V30 cabinet are **fitted to captures of real gear** instead
of being voiced by ear. The engine is the same (tube stages, tone stack, power amp, clippers, cabinet filters), and so
is its CPU cost on the pedal: only the numbers in its tables change. A computer finds those numbers so that the
engine's output matches the real amp's on real guitar playing.

## The set

| Type | Style | Reference capture (author, NAM collection) |
| --- | --- | --- |
| TWIN | American clean | Fender Twin Reverb, Normal, Bright (Tim R) |
| AC15 TB | chime | Vox AC15 Top Boost (Phillipe P) |
| JCM900 | British crunch | Marshall JCM900 50 W, channel A, gain 6 (Tim R) |
| JCM2000 | British lead | Marshall JCM2000, Lead 2, gain 6 (Tim R) |
| MARK IV | American lead | Mesa Boogie Mark IV (Roman A) |
| SHIRLEY | boutique crunch | Friedman Dirty Shirley Mini, crunch, gain 6 (Sascha S) |
| PRO MOD | modern rock | Splawn Pro Mod, 2nd gear (Tim R) |
| 5150 | high gain | Peavey 5150 Block Letter (Helga B) |
| TS9 | overdrive | Ibanez TS9 (Tim R) |
| KLON | transparent overdrive | Klon clone, gain 6, treble 6 (Keith B) |
| M77 OD | boost / hot overdrive | MXR M77 Custom Badass (Tudor N) |
| SWEDE HM-2 | Swedish distortion | Boss HM-2 (Peter N) |
| 4x12 V30 cab | modern 4x12 | 8 pairs "amp" / "amp + cab" of the same amps (Phillipe P) |

The other cabinets (1x12 US OPEN, 2x12 BLUE, 4x12 GREEN, 4x12 T75, 1x12 CREAM) keep their v33 voicing: no free
measurement of them was available. Compared with the measured 4x12, their top end is already realistic.

With the knobs at noon, an amp or a drive sounds like its capture; the knobs move around that point as before.
Presets saved before v34 are converted to the nearest type of the new set (`px_type_v34`, store format 3).

## Data and licences

- **Captures**: the Neural Amp Modeler community collection, [pelennor2170/NAM_models](https://github.com/pelennor2170/NAM_models),
  GNU GPL v3. Thanks to their authors (named above). The `.nam` files themselves are not shipped with OpenPocket:
  only the numbers fitted from them.
- **Guitar**: real DI recordings from [Alec-Wright/Automated-GuitarAmpModelling](https://github.com/Alec-Wright/Automated-GuitarAmpModelling)
  (GNU GPL v3): 8 s to fit, 20 s never heard by the fit to measure.

## How it works

1. `tools/nam_a1.py` runs a NAM A1 WaveNet capture (an independent NumPy implementation: it reads the `.nam` JSON and
   checks that every weight is used) on the DI, at the capture's rate (48 kHz), DC removed.
2. `tests/fit/fitlib.c` builds the real `engine/px_dsp.c` with `PX_FIT`, which lets the fitter replace one row of the
   amp, drive or cab table; the DI goes through the engine at the pedal's level (hard playing peaks at about -17 dBFS
   at the converter).
3. `tools/fit_amp.py` / `tools/fit_drive.py` move the row's numbers with SciPy's `least_squares`:
   - clean sounds (`--loss time`): sample by sample (pre-emphasised error) plus short-term spectra; the delay between
     the two is measured from the phase of their cross-spectrum;
   - distortion (`--loss spectral`): what the ear compares, the long-term spectrum in 1/6 octaves, short-term
     spectra (46 ms and 6 ms) and the level envelope (silences excluded). Sample by sample means nothing once the
     harmonics' phases differ a little.
4. `tools/fit_cab.py` fits a cabinet row to a measured magnitude response (1/12 octave, 60 Hz to 12 kHz). The 4x12's
   response is the cross-spectrum of "amp" and "amp + cab" captures of the same amp settings, averaged over 8 pairs.
5. `tools/apply_v34.py` writes the fitted rows into the tables, renumbers the types and rewrites the factory presets;
   `tests/engine/calib3.c` sets the levels (`px_trim.h`).

Results (error on the 20 s the fit never heard) are in the table at the end of this file.

## Adding or redoing a model

```
cc -O2 -shared -fPIC -DPX_FIT -Iengine tests/fit/fitlib.c engine/px_dsp.c firmware/px_math.c -lm -o build/fitlib.so
python3 tools/fit_amp.py --capture "My amp.nam" --start "JCM900" --name myamp \
    --di train-di.wav --val test-di.wav --loss spectral --nfev 60 --out myamp.json
```

`--start` is the existing row to start from (pick the closest style), `--stages` the number of tube stages (1 for
a clean amp, 4 for high gain). A capture must be an amp alone (no cab), at the knob setting you want as noon.
