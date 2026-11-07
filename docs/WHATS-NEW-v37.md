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
- **Browse TONE3000 from the editor:** the button opens TONE3000's own tone browser (sign in there, A2 captures only),
  then the editor shows the chosen tone (title, gear, creator) and its A2 models, and loads one into a user slot. It
  uses TONE3000's public API (its "Select" flow, OAuth with PKCE); nothing is stored by the editor.

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

## Bluetooth MIDI
- **The pedal can be a Bluetooth MIDI device** named `OpenPocket`: a Mac, an iPhone or iPad, an Android phone or a
  controller such as the Chocolate Plus connects to it, and Program Change and Control Change do what they do over USB.
- **Off by default.** Hold FX for the MIDI page, BLUETOOTH ON, HOME: the radio starts and the page shows its state
  (`VISIBLE AS "OPENPOCKET"`, `DEVICE CONNECTED`); the status corner shows `BT` while a device is connected.
- A radio that does not start never makes the pedal loop: after one restart it stays off until a power cycle.
- How to connect, limits, how it fits in memory: [BLUETOOTH.md](BLUETOOTH.md).

## Battery gauge
- **The battery icon now works**: v36 read the chip's battery channel without switching on the buffer that drives it (the
  SDK's `adc_pmu_detect_en` does), so there was never a reading and the icon stayed hidden. The bias and the test-bus
  settings are now set as the SDK sets them.
- With no reading the icon shows an empty cell and `--` instead of nothing. The tuner page shows the raw readings on its
  last line (`BG` bandgap, `VB` battery, `5V` USB supply, `A7`/`A8` the spare analog pins): a photo of it tells us the rest if
  the gauge still looks wrong on your unit.

## From the community (merged into v37)
- **MIDI over USB-C** (VinSpee, #10): Program Change picks a preset, Control Change switches blocks and sets their
  models and settings (CC 8–19 toggle a block on value 0), the pedal sends its own changes, MIDI clock sets the delay
  time; a MIDI page on the pedal sets the channel. Reference: [MIDI.md](MIDI.md).
- **Button lights on the BlackBox** (steponme82, #17): AMP, MOD, DLY, REV and CAB light while their block is on, FX
  while any block of its group is on, HOME blinks in the tuner. Checked on a BlackBox; on a Pocket X the order of the
  lights still has to be checked.
- **Bluetooth MIDI** (VinSpee, #13, #16): the Bluetooth MIDI device, first as a test image (`make ble`, where a Chocolate
  Plus paired and switched effects); in v37 it is in the release, switched on from the MIDI page ([BLUETOOTH.md](BLUETOOTH.md),
  [BLE-TEST.md](BLE-TEST.md)).
- **Tools** (VinSpee, #11, #12, #14, #15): a command-line flasher with a rescue mode (`node tools/op.mjs flash`), a
  Mac-side UBOOT rescue for a BlackBox that restarts in a loop ([RESCUE.md](RESCUE.md)), a simulated pedal in the
  browser with its front panel and its sound ([SIMULATOR.md](SIMULATOR.md)), and a reader of the pedal's state
  (`node tools/op.mjs log`).

## For developers
- Editor protocol 2: `NAM_LIST`, `NAM_WRITE`, `NAM_COMMIT`, `NAM_ERASE` (19–22), and `DIAG` (23, the log channel of the
  firmware with Bluetooth) ([EDITOR-PROTOCOL.md](EDITOR-PROTOCOL.md)).
- `tools/train_a2.py` (JAX) trains an A2-Lite network from a capture; `tools/mk_nam_factory.py` builds the factory
  set; `tests/test_nam.py` checks the fixed-point engine against a float reference.
- Memory: the NAM history buffers (83 KB) sit in the POOL region beside the engine. Bluetooth takes most of the rest of
  the ordinary RAM; its task stacks and the tuner's buffers live in the data cache used as RAM (`CRAM` in `app.ld`), and
  the build prints what is left of both. The stack guard stays on (its window is raised to the controller's stacks).
- `OP_NOBLE=1 sh firmware/build.sh` builds without Bluetooth.
- `tools/check_calls.py` (run by `firmware/build.sh`): refuses a direct call between the flash code and the RAM code,
  which is out of the CPU's call range, and names it.

## Please report
The CPU load cannot be measured off the pedal. Please send the `2C nn%` shown at the bottom right on a few factory
presets (for example 02 CHORUS CLEAN, 06 AMBIENT SWELL and 23 ORGAN), and say if you hear crackles.
