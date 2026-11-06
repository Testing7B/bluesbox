# Neural amp models (NAM A2) on the pedal (v37)

Since v37 the AMP block has a ninth type, **NAM**, that plays a [Neural Amp Modeler](https://www.neuralampmodeler.com/)
network instead of an amp built from circuit stages. It plays the networks of NAM's **A2** architecture, at its
3-channel size, **A2-Lite**. That is the size the pedal's CPU can run.

## The knobs

| Knob | What it does |
| --- | --- |
| MODEL | which network: the 12 factory models, then the 16 user slots (`U1 EMPTY` ... until you load one) |
| GAIN | the level into the network, ±12 dB around the level of the captures (noon) |
| BASS, MID, TREBLE | an EQ after the network: ±10 dB at 120 Hz, ±8 dB at 800 Hz, ±10 dB above 2.5 kHz |

The models are amp captures without a cabinet, so keep a CAB after the AMP. Every model plays at the same loudness
(-20 LUFS on the reference clip `docs/nam_di.wav`), so switching models keeps the volume.

## Factory models

Twelve A2-Lite networks, trained for the pedal at 44.1 kHz (`tools/train_a2.py`). Each one learnt to sound like a
community capture of a real amp, playing real DI guitar. Accuracy is ESR, the error on 20 s of guitar the training never
heard: 0.01 is very close, and below about 0.05 the two are hard to tell apart.

| MODEL | Amp | Capture (author) | ESR |
| --- | --- | --- | --- |
| TWIN | Fender Twin Reverb (normal, bright) | Tim R Fender TwinVerb Norm Bright (Tim R) | 0.006 |
| AC15 | Vox AC15 Top Boost | Phillipe P VOXAC15-TopBoost (Phillipe P) | 0.045 |
| SUPER 59 | Magnatone Super 59 Mk II | Tim R Magnatone Super 59 Mkii Ch1 Br (Tim R) | 0.009 |
| SHIRL CL | Friedman Dirty Shirley Mini, clean | Sascha S DirtyShirleyMini_Clean_B1_M6_T7_MV10_G4 (Sascha S) | 0.014 |
| JCM900 | Marshall JCM900 50 W Dual Verb | Tim R JCM90050WDualVerbCHAG6 (Tim R) | 0.011 |
| SHIRLEY | Friedman Dirty Shirley Mini, crunch | Sascha S DirtyShirleyMini_crunch_G6 (Sascha S) | 0.033 |
| JCM2000 | Marshall JCM2000 DSL, lead | Tim R JCM2000 L2 G6 (Tim R) | 0.042 |
| MARK IV | Mesa Boogie Mark IV | Roman A LT_MESA_MARKIV_1 (Roman A) | 0.041 |
| JVM OD2 | Marshall JVM, OD2 | Phillipe P JVM-OD2-RD-NoDrive-ESR0,003 (Phillipe P) | 0.056 |
| PRO MOD | Splawn Pro Mod, 2nd gear | Tim R Splawn Pro Mod 2nd Gear - G12 (Tim R) | 0.052 |
| 5150 | Peavey 5150 (block letter), boosted | Helga B 5150 BlockLetter - Boosted (Helga B) | 0.064 |
| 6505 RED | Peavey 6505+ red channel, MXR drive | Helga B 6505+ Red ch - MXR Drive (Helga B) | 0.124 |

High-gain captures are harder for a network this small: their error is higher (the 6505+ most), while the clean
and crunch models are very close. A larger A2 would do better, but would not fit the pedal's CPU.

The factory presets use them; the fitted amps of v34 are still there as the other 8 AMP types.

## Your own models (TONE3000 and others)

1. Get a capture in the **A2** format: on [TONE3000](https://www.tone3000.com/) and in NAM's own trainer, A2 files
   are offered beside the older formats. The pedal reads the A2-Lite part of an A2 file. The older A1 files
   ("standard", "lite", "feather", "nano") do not run on the pedal and are refused with that reason.
2. Open the web editor (`editor.html`), connect the pedal, and under **NAM models** pick a free slot and the `.nam` file.
   The editor checks it, measures its level and sends it to the pedal (about 2 seconds; the sound mutes a moment
   while the flash is written).
3. In a preset, set the AMP type to **NAM** and turn **MODEL** to that slot.

The 16 user slots are in the pedal's flash at 0xC0000..0xE0000, outside the area a firmware update rewrites: your
models stay through updates. Clear a slot from the same panel.

Most captures are made at 48 kHz. The pedal runs at 44.1 kHz, so such a model plays with its tone about 1.5
semitones lower (the amp's filters sit 8% lower). The editor says so when it loads one. The factory models are
trained at 44.1 kHz and are not affected.

## How it runs

- `engine/px_nam.c`: the network in fixed point (activations with 20 fraction bits, weights with 24, 64-bit sums),
  checked against a float reference (`tools/nam_a2.py`) for every factory model by `tests/test_nam.py`: about 99 dB
  signal to error.
- Blocks of 32 frames, layer by layer: each frame's inputs to the layer are gathered into one row (3 words moved
  by one instruction), then each output is a dot product with 4 frames at a time (a weight read once for 4 frames),
  then LeakyReLU, the head sum and the residual. On the pedal these three steps are hand-written (pi32v2 assembly:
  hardware loops, multiply-adds paired with loads, no branch per sample), because this CPU has no data cache in front
  of its RAM and a taken branch costs about 8 cycles. At start-up they are run against the C versions on a test
  pattern, and the C versions are used if they differ by a single bit.
- 1,730 multiply-adds per sample in 23 layers; the history buffers take 83 KB of RAM, a model 7.5 KB.
- The second core runs it with the rest of the chain. The load shows at the bottom right of the screen (`C1 nn%`).

## Training (`tools/train_a2.py`)

`train_a2.py --capture CAPTURE.nam --di DI.wav --val DI2.wav --out MODEL.nam --name NAME` trains an A2-Lite network
(JAX) on a capture: the DI goes through the capture (resampled to its rate and back), the A2-Lite learns the result at
44.1 kHz. Loss: pre-emphasised ESR plus a multi-resolution spectral term. `tools/mk_nam_factory.py` writes the
factory models into `engine/px_nam_factory.h`, each with its level trim.

Credits: the captures are from the Neural Amp Modeler community collection
[pelennor2170/NAM_models](https://github.com/pelennor2170/NAM_models) (GNU GPL v3), by the authors named in the
table; the DI guitar is from [Alec-Wright/Automated-GuitarAmpModelling](https://github.com/Alec-Wright/Automated-GuitarAmpModelling)
(GNU GPL v3). The A2 architecture is Steven Atkinson's (Neural Amp Modeler, MIT); the pedal's code is written for
OpenPocket from the published architecture.
