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

Or press **Browse TONE3000** in the same panel: TONE3000's own tone browser opens in a pop-up (sign in there; it shows
A2 captures only), and the chosen tone comes back with its creator and its A2 models; **Load** puts one into the
user slot chosen beside it. This uses TONE3000's public API ([tone3000.com/api](https://www.tone3000.com/api)), its
"Select" flow: OAuth 2.0 with PKCE and OpenPocket's publishable key (`T3K_CLIENT_ID` in `docs/pxt3k.js`), the sign-in
coming back through `docs/t3k.html`. The access token lives in the page only while it is open. It needs that key and
`https://isod89.github.io/openpocket/t3k.html` registered as a redirect URI in TONE3000's settings.

The 16 user slots are in the pedal's flash at 0xC0000..0xE0000, outside the area a firmware update rewrites: your
models stay through updates. Clear a slot from the same panel.

Most captures are made at 48 kHz. The pedal runs at 44.1 kHz, so such a model plays with its tone about 1.5
semitones lower (the amp's filters sit 8% lower). The editor says so when it loads one. The factory models are
trained at 44.1 kHz and are not affected.

## How it runs

- `engine/px_nam.c`: the network in fixed point (activations with 20 fraction bits, weights with 24, 64-bit sums),
  checked against a float reference (`tools/nam_a2.py`) for every factory model by `tests/test_nam.py`: about 99 dB
  signal to error.
- Blocks of 32 frames, layer by layer. On the CPU, each output channel of a layer is computed for 4 frames at a time
  straight from the layer's history ring (`rdot`): for each kernel tap, the 4 frames' 3 channels sit side by side in
  the ring (12 words), so a weight is read once for 4 frames and no input is copied (where a tap's 4 frames wrap
  round the ring, those 12 words are copied first). Then LeakyReLU, the head sum and the residual (`post`). On the
  pedal these are hand-written (pi32v2 assembly: multiply-adds paired with loads, 3 words moved by one instruction,
  no branch per sample), because this CPU has no data cache in front of its RAM and a taken branch costs about 8
  cycles. At start-up they are run against the C versions on a test pattern, and the C versions are used if they
  differ by a single bit. On this CPU a multiply-add paired with the load of the value it needs next waits for it
  (3.2 cycles, against 2.2 without the wait), so the routines for 6, 15 and 16 taps (`rdq`, written by
  `tools/gen_rdq.py`) are unrolled with each value loaded 2 to 4 steps before its use. The start-up timing compares
  them with the first `rdot` and with gathering each frame's inputs into one row first (then the hardware-loop dot
  routine), and keeps the fastest.
- The dot products can also run on the chip's vector unit (in its FFT block: one command computes a row of weights
  against many X rows, beside the CPU; each frame's inputs to the layer are then gathered into one X row). The block
  is cut in two halves, and half B of a layer never needs the next layer's half A: so while the unit computes one
  half, the CPU gathers or posts the other, and may compute the last 4 or 8 frames of a half itself. The unit costs
  about 4.8 cycles a multiply-add on the pedal, about as much as the CPU. Its inputs (the X rows and a copy of the
  weights, 13 KB) sit in seven of the data-cache ways turned into RAM at boot (`firmware/px_cacheram.h`, melodee's
  routine; the data cache only serves the flash), so that it does not read the RAM the CPUs work in; the start-up
  check runs the unit there too, and the ordinary RAM is used if it differs.
- Two cores: frame t of a layer needs only frames t and earlier of the layer before. So CPU1 takes the first 12 or 16
  frames of each 32-frame block through all 23 layers without ever waiting, and CPU0 the rest, waiting before each
  layer until CPU1 has posted the one before (one counter). With a NAM amp in two-core mode the audio block runs on
  CPU0 (in the audio interrupt) as a pipeline (`dsp_pipe` in `firmware/px_audio.h`, `px_engine_part`): CPU1 first runs
  the effects after the amp on the previous block's network output while CPU0 runs the effects before the amp on this
  block, then both run the network (one block, 2.9 ms, more latency; `tests/engine/pipe_test.c` checks it gives the
  one-core output one block later). In the pipeline the roles are the other way round (`pxn_serve`): CPU1 comes to
  the network after its effects, so it takes the last frames of each 32-frame block and waits per layer for CPU0,
  which takes the first frames and never waits, and up to 12 more of them in the first blocks by how late CPU1 came
  in the blocks before. Without a NAM amp the whole block runs on CPU1 as before.
- Each layer's history ring has its first 3 frames mirrored after its end, so 4 frames can be read straight from any
  position: the tap pointers just move on by 4 frames per group. On the pedal a layer's whole block of groups runs in one call
  (`pxn_rdl6/15/16_asm`, generated by `tools/gen_rdq.py --rdl`), its tap table on the stack.
- v40.2: on the pedal a multiply-add costs 2 cycles, and so does a load, unless it is paired with the multiply-add (then
  it is free); an ALU instruction costs 1 (measured). `rdl` reads 5 values for 4 multiply-adds (one weight, 4 frames).
  `rdm` (`tools/gen_rdq.py --rdm`) does a 3-row layer's rows 0 and 1 two frames at a time (2 weights, 2 frames: every load
  paired) and row 2 as `rdl`; `post4` reads the layer's input straight from its ring in a loop with no counter in memory,
  the residual's weights on the stack so that their loads pair, and LeakyReLU as `smax(z, (z * 42949673) >> 32)`, the
  same value for every z (checked over all 2^32). Both are checked against C at start-up like the others and timed against
  them; the start-up check, the timing and the dispatch also run on the PC (`tests/engine/hand_test.c`) with C stand-ins
  that read exactly what the hand code reads, under the address sanitizer, so that a check that would fault on the pedal
  (a boot loop) fails there first. `pxn_equiv` runs the whole network with the routines in use against C.
- At start-up the pedal times the ways to run it (CPU alone; the unit one command after the other; the unit and the
  CPU at once, the CPU taking 0, 4 or 8 frames; two cores, with or without the unit) on the first factory model and
  keeps the fastest (`pxn_tune`). The PC test runs every mode with a simulated unit that finishes its commands late,
  at random, and a simulated second core that moves on only while the first waits for it: the output must be exactly
  the CPU's.
- 1,730 multiply-adds per sample in 23 layers; the history buffers take 83 KB of RAM, a model 7.5 KB.
- The load shows at the bottom right of the screen (`C1 nn%`).

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
