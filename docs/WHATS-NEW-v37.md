# What's new in OpenPocket v37: neural amp models (NAM A2)

## Neural amps on the pedal
- **A new AMP type, NAM**, plays Neural Amp Modeler networks of the **A2** architecture (its "Lite" size) on the pedal
  itself. The pedal no longer plays an approximation fitted to a capture: it plays the network.
- **12 factory models**, trained for the pedal from community captures of real amps (Fender Twin Reverb, Vox AC15,
  Magnatone Super 59, Friedman Dirty Shirley clean and crunch, Marshall JCM900, JCM2000 and JVM, Mesa Mark IV, Splawn
  Pro Mod, Peavey 5150 and 6505+). All at the same loudness.
- **Knobs:** MODEL, GAIN (±12 dB into the network), BASS, MID, TREBLE (an EQ after it).
- Details, accuracy and credits: [NAM.md](NAM.md).

## Load your own captures (TONE3000)
- The web editor has a **NAM models** panel: pick a user slot, choose an **A2** `.nam` file (from TONE3000, or from
  NAM's trainer), and it goes to the pedal in about 2 seconds. 16 user slots, kept through firmware updates.
- The editor checks the file, refuses A1 captures with the reason, and sets the model's level so it plays as loud as
  the others.
- On the pedal, MODEL shows the model's name; the PLAY page shows it as the amp.

## New factory presets
- **All 24 factory presets are rebuilt around the NAM models**, levelled to the same loudness as before. The 28 USER
  slots start from a clean NAM Twin.
- The 8 fitted amps of v34 stay available as AMP types, and your saved presets keep the amp they use.

## Faster engine
- The NAM network has hand-written routines for the pedal's CPU, checked against the C versions at every start-up:
  each layer is read straight from its history ring (no copy), 4 frames at a time, a weight read once for the 4,
  multiply-adds paired with loads.
- It can use the chip's **vector unit** (in its FFT block) for the dot products beside the CPU, and the **second
  core**: with a NAM amp, CPU0 runs the block and CPU1 takes the first frames of each network block (earlier frames
  never need later ones, so CPU1 never waits). At start-up the pedal times the ways it can run the network and keeps
  the fastest.
- A NAM amp replaces the fitted amp's ×4-oversampled stages, so it adds less than its own cost.
- Effects run over a whole block, one after another, instead of sample by sample (same output, about 20% cheaper).
  In front of a NAM amp the drive runs at ×2 oversampling instead of ×4.
- With a NAM amp the two cores work as a pipeline: CPU1 runs the effects after the amp on the previous block while
  CPU0 runs the effects before it, then both run the network. This adds one block of latency (2.9 ms).
- The network reads each layer's history without any per-tap position arithmetic: every ring's first frames are
  mirrored after its end, and each layer's whole block runs in one hand-written call.
- In the pipeline CPU1 goes straight from its effects to the network and takes the last frames of each network
  block; CPU0 takes more frames when CPU1 comes late, so neither core waits for the other.
- **Load on the pedal** (% of real time, measured): the network alone 66%, the 23 factory presets with a NAM amp
  69–90% (most 75–83%). The status corner shows `2C` and the load while both cores run a NAM preset. Above 93% for
  8 blocks in a row the pedal bypasses the effects (`BYP`) until the next change, as before (85% on one core).

## For developers
- Editor protocol 2: `NAM_LIST`, `NAM_WRITE`, `NAM_COMMIT`, `NAM_ERASE` ([EDITOR-PROTOCOL.md](EDITOR-PROTOCOL.md)).
- `tools/train_a2.py` (JAX) trains an A2-Lite network from a capture; `tools/mk_nam_factory.py` builds the factory
  set; `tests/test_nam.py` checks the fixed-point engine against a float reference.
- Memory: the NAM history buffers (83 KB) sit in the POOL region beside the engine, so that the ordinary RAM keeps
  about 47 KB free (for Bluetooth later).
- `tools/check_calls.py` (run by `firmware/build.sh`): refuses a direct call between the flash code and the RAM code,
  which is out of the CPU's call range, and names it.

## Please report
The CPU load cannot be measured off the pedal. Please send the `C1 nn%` shown at the bottom right on a few factory
presets (for example 14 HAIR METAL, 16 SPACE LEAD and 06 AMBIENT SWELL), and say if you hear crackles.
