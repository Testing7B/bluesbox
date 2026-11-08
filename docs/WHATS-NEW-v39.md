# What's new in OpenPocket v39: the delay and the reverb off the critical path

Taken from the stock BlackBox firmware ([STOCK-FIRMWARE.md](STOCK-FIRMWARE.md), section 7.3): there, the dry sound is
made in the audio interrupt and the delay and the reverb run beside it, in a task of their own, one block late. A late
echo is never heard; a late block of the whole sound is a crackle.

## The wet lanes

- **Until v38** the delay and the reverb ran inside the chain, in series: every block had to finish them before the next
  interrupt, like the amp. On a heavy preset they could push a block past its 2.9 ms: a crackle of the whole sound.
- **Now** (with the second core on, which is always the case unless it has failed) they each have a **lane**. At its
  place in the chain the delay (or the reverb) only sends the signal to its lane and mixes in the lane's wet sound of two
  blocks before; the lane makes that wet sound **beside the chain**:
  - **without a NAM amp** the second core runs the chain, and the first core runs the lanes at the same time;
  - **with a NAM amp** (both cores share the network) the second core runs the lanes after the block, in the time left
    before the next interrupt. If they are not done when it comes, the rest of that wet block is left silent and counted,
    and the dry sound goes out on time.
- **The echoes keep their time to the sample.** The delay's taps that are heard read 256 frames (5.8 ms) less far back,
  the ones fed back read as before: the first echo and the spacing of the repeats are the same as in v38, bit for bit on
  the digital and dual delays (`tests/engine/wet_test.c`). On the modulated delays the wobble is the same, 5.8 ms
  later.
- **The reverb starts 5.8 ms later**, as if it had 5.8 ms more pre-delay: its tail is otherwise the same, sample for
  sample (also checked by the test).
- **Nothing else changes in the sound.** Without the second core (if it has failed) the delay and the reverb run in the
  chain, as before.

## On the tuner page

- The footer's CPU figures for DLY and REV are now their lanes' time, spent beside the chain (on the other core, or after
  the block).
- **`CUT n`** appears after them once a lane has had to stop short: n wet blocks cut since the start (or since the last
  reset of the engine). If it climbs while you play, the preset is at the limit: the echo or the reverb loses a few
  milliseconds instead of the whole sound crackling. A photo of that page with `LATE` and `CUT` says which.

## For developers

- Engine (`engine/px_dsp.h`): `px_wet_mode`, `px_wet_swap`, `px_wet_run`; the state in `px_engine.wl_*`, the buffers
  (2 KB of RAM) in `px_dsp.c` (`px_wl`). Off by default: the tests, the simulator and a one-core pedal run the delay and
  the reverb inline, with the same output as v38 bit for bit.
- The delay and reverb loops now write only their wet part (`dly_block`, `rv_block`); the chain adds the dry part.
- Firmware (`firmware/px_audio.h`): `a_enc` swaps the lanes after collecting the previous block, `wet_job` and `a_wstop`
  for the lanes on CPU1, `a_c1_off` turns them off with the second core.
- New test: `tests/engine/wet_test.c` (in `tests/run_tests.sh`).
