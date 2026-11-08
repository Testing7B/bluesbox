# The stock firmware, read for OpenPocket (BlackBox_021)

What M-VAVE's own firmware for the BlackBox (`BlackBox_1.fwsc`, tag `BlackBox_021`) does, read from its code, and
what OpenPocket can take from it, especially on the audio side. The firmware was unpacked with `tools/pocketx_fw.py`
and disassembled with JieLi's own objdump (`--mcpu=r3`, which decodes the FPU instructions), with Ghidra for the
call graph. Addresses are those of the stock image: flash code at `0x2000120`, RAM code at `0x1C00000`.

Each finding says how sure it is: **read** (seen in the code), **likely** (strong evidence, one step not seen), or
**guess**.

## 1. The image

| Part | Where | What |
| --- | --- | --- |
| Flash code | `0x2000120`, 630 KB | the application, run from flash through the cache |
| RAM code | `0x1C00000`, 66 KB (`0x107E4`), copied at boot | the hot DSP: FFT, filters, resamplers, the amp's process stages |
| Overlay window | `0x1C22340` to `0x1C23F80`, 7 KB | one of two code sets copied in on demand (`f_0200774c`): 7,232 B from flash `0x2093AE8` or 6,756 B from `0x2092084` |
| Effect code | 20 blocks after the overlays (flash `0x2095728`) | float routines of the time effects (chorus, delays, reverb...), copied to RAM from `0x1C23F80` on |

**Read.** The DSP that runs every sample is not executed from flash: the library and the per-sample stages live in RAM,
and the less used code is swapped into one small RAM window. On this chip a flash fetch that misses the cache costs
many cycles and a taken branch about 8, so this is where the stock firmware gets its speed.

## 2. Tasks and interrupts

**Read** (interrupt table and `request_irq` calls):

| Source | IRQ | Priority | Core | Handler |
| --- | --- | --- | --- | --- |
| ALNK0 (the I2S audio) | 11 | 4 | CPU0 | `0x202A230` |
| BLE radio (two lines) | 45, 29 | 2 | CPU0 | the BT controller |
| SOFT3 (software interrupt) | | 6 | both | scheduler wake-up |
| TIMER4, SBC decode | | | CPU1 | |

Named tasks (table at `0x208CF00`): `wet_audio`, `tuner_task`, `USB-Audio`, `bt_sbc_dec`, `midi_route`, `sys_timer`.

What it means for OpenPocket's Bluetooth: in the stock firmware the radio's interrupts sit **below** the audio
interrupt (2 against 4) on the same core, so audio always preempts the radio and never the other way round.
**Likely:** the heavy DSP runs in the `wet_audio` task rather than in the audio interrupt itself. In our v37 attempt
the radio's work could hold up the audio block, which fits the "BYP 150%" we saw. Any return of Bluetooth should keep
that order: audio interrupt highest, radio below it, radio work on the core that
does not run the network.

## 3. Audio input and output

**Read** (ALNK ISR `0x202A230`):

- **64-frame blocks**, 24-bit samples, stereo I2S on ALNK0 (the same pins OpenPocket uses).
- The input gain is applied as an integer multiply and `>> 15`; peak meters are kept per block.
- Two counters, **early** and **late**, count blocks that arrive before or after the expected time: the stock firmware
  watches its own real-time margin the way our `LATE` counter (v37) now does.

**Likely: 48 kHz.** Every effect's coefficient code computes with `fs = 48000.0` (and `1/48000`), in ten different
setup functions (`f_0202c666`, `f_02020438`, ...). 44,100 appears only in the Bluetooth (SBC) and USB code. OpenPocket
runs at 44.1 kHz with 128-frame blocks (2.9 ms); the stock firmware at 48 kHz with 64-frame blocks (1.33 ms).

## 4. How the stock effects are built

### Module descriptors

**Read.** Each effect is a small descriptor in flash: an init function, a parameter function (UI value to
coefficients, all in float at 48 kHz, run outside the audio path), a process function, and the instance size. Tables
at `0x204F54C` (5 modules), `0x20509D4` (7 modules), `0x204FC30` (the amp family) and `0x2054F0C` (six amp
setups: `f_0202cfd0`, `cbfc`, `c962`, `ce20`, `d4d0`, `c874`).

### Parameters glide, they never jump

**Read** (`f_01c0796c`, `f_01c07aee`). A gain is never set: it moves towards its new value by a one-pole ramp, one
step per sample (`g = g*a + target*(1-a)`), and the routine falls back to a plain multiply loop as soon as the value
has arrived (it compares the ramp's next value with the current one). Mixes (wet/dry, the two inputs of a crossfade)
move the same way, two gains at once. On the control side the setters keep a smoothed copy too
(`0.97 * old + 0.03 * new` per block, `f_0202d7d8`).

### A change of type is a crossfade

**Read** (`f_01c05350`, used by the 12 modules of the first two tables). Each slot holds an old and a new instance.
When the user changes the type or the model, both run for a while and the outputs are crossfaded with the ramped
gains above; when they are the same, only one runs. No mute, no click.

### Time effects run at half rate

**Read** (`f_01c05350`, `f_01c0b0f6`, `f_01c0b02a`). The modulation and time effects take the block, sum it to mono,
**decimate it 2:1** with a polyphase IIR half-band filter (two all-pass sections per branch), run the effect on
**32 samples at 24 kHz**, and **interpolate 1:2** back, with the crossfade gain applied in the interpolator. A chorus,
a delay or a reverb loses nothing audible above 12 kHz, and costs half.

### The amp: 6x oversampled, one division per sample

**Read** (`f_0202d236`, the process function of the amp family). Per 16-frame sub-block:

1. first-order input filter (`f_01c08cba`: `b0, b1, a1`, three multiply-adds a sample);
2. the preamp stage (`f_01c05754`): input conditioning with a soft limiter (`fmin`/`fmax`), three more first-order
   filters, a ramped gain;
3. **upsample 2x** (`f_01c0758a`: polyphase all-pass half-band, 6 coefficients:
   0.3195, 0.0958, 0.7420, 0.5567, 0.9600, 0.8695) **then 3x** (`f_01c076d0`: three all-pass branches,
   0.7396, 0.3513, 0.1013): 16 samples become 96, at 288 kHz;
4. the waveshaper at 288 kHz: bias added, then a rational sigmoid, asymmetric, with one division per sample instead
   of `tanh` (`y = c - 1/(k*x + d)` on each side of a knee, with `k = 75.86`, `d = 1.0133`, `c = 0.2163`, knee
   `-0.1014`);
5. **down 3x** (`f_01c07750`, the same three branches, `x 1/3`) and **down 2x** (`f_01c077e6`);
6. tone stages (ramped gains, a two-way mix), two more first-order filters, out.

Where a `tanh` is needed it is a **512-entry table with linear interpolation** in RAM (`0x1C0E408`, used by
`f_0202d7d8` for its input soft clip and by the effect blocks).

### Cabinets: partitioned FFT convolution, on the FFT block

**Read** (`f_0202f844`, RAM library `f_01c04f9a`...). Cabinet IRs are convolved with a uniform partitioned FFT
convolution: 64-frame partitions, FFT of 128, overlap-save, complex multiply-accumulate in float (`f_01c0a676`, two
loads per complex product, unrolled). The FFTs themselves run either in software (radix routines in RAM for 32, 64
and other sizes) or **on the chip's FFT block at `0x1EE0400`** (the same block OpenPocket's NAM uses as a vector unit):
the code writes a 3-word job (`[config | 1, in, out]` at `0x1C1001C`), points `CADR` (`0x1EE0404`) at it, starts it with
`CON = 0x101`, and later waits for bit 7 of `CON` and clears it with bit 6. The block works in fixed point, so the
code normalises the input first (it finds the block's headroom, `f_02044de2`, clamps the shift to -24..+18) and
rebuilds a float scale `2^e` for the result (`f_01c08c74`). It checks which core it runs on (`cnum`) and uses the
hardware only on CPU1.

### Captured models

**Read** (the loader). The stock firmware loads models of six formats from its own flash: `CAB` (an IR), `AM2`,
`AM3`, `AM4` (**guess**: its own amp captures, in several sizes), `NAM` (type 16, 1,871 float weights, 7.5 KB) and `LMT`. **Likely:**
the model inference uses the integer kernels of the overlay window: overlay A is built of 64-bit integer
multiply-accumulate loops with three to five accumulators, one input broadcast against a row of weights read with a
stride (`r_acc += [w ++= stride] * x`, inside hardware loops), the shape of a dense or 1-D convolution layer; the
loader that swaps A and B is called from the model module's init (`f_02008692`). So the stock firmware runs its
networks in **fixed point** from RAM, as OpenPocket does since v37. Its network is of the same order as A2-Lite: if
each weight is used once per sample, as in a WaveNet, 1,871 weights are about 1,871 multiply-adds per sample, against
A2-Lite's ~1,730, and at 48 kHz rather than 44.1. Which exact network the 1,871 weights describe is not settled.

## 5. What OpenPocket can take from it

In order of value for the sound, with what it would cost.

1. **Glide every parameter, crossfade every change.** Today `audio_apply` (`firmware/px_audio.h`) masks the audio
   interrupt, recomputes the whole engine (`px_engine_set`) and swaps the new coefficients in at once. That makes two
   kinds of crackle: a step in the sound when a coefficient jumps, and a late block if the recompute takes longer
   than the block margin (the interrupt is held off meanwhile). The stock way: compute the new set outside the
   interrupt into a second copy without masking, swap a pointer, and let gains and mixes ramp per sample; for a type
   or model change, run the old and the new instance together for ~20 ms and crossfade. For the NAM amp this means
   two network states for the fade time, or a short fade out/in of the amp block (cheaper, almost as clean).
2. **Halve the cost of the time effects.** Run MOD, DELAY and the reverb at half rate behind a polyphase half-band
   decimator and interpolator (two all-pass sections a branch, about 6 multiply-adds per output pair). That frees CPU
   for the NAM network, which is the limit today.
3. **Cheaper oversampling.** OpenPocket oversamples the drive and amp 4x with Butterworth biquads (two up, two down,
   at the oversampled rate). The stock firmware gets 6x for less with polyphase all-pass half-band filters (2x) and a
   three-branch all-pass stage (3x): each all-pass section is one multiply-add, and each branch runs at the lower
   rate. The coefficients are in section 4 and can be used as they are.
4. **Waveshapers without `tanh`.** A rational curve (one division) or the 512-entry table with linear interpolation,
   both in RAM, instead of a polynomial or a library call per oversampled sample.
5. **Real cabinet IRs.** OpenPocket's cabinets are filters and sparse taps. The stock firmware shows that a 64-frame
   uniform partitioned convolution fits beside its amp, with the FFTs on the FFT block of CPU1, with block floating
   point. OpenPocket already drives that block (`engine/px_nam.c`); the FFT mode is the same `CON`/`CADR` protocol
   with a different job word. This would let the pedal load user IRs (and the `CAB` files of the stock firmware).
6. **Sample rate 48 kHz?** Most NAM captures, TONE3000 included, are made at 48 kHz; at 44.1 kHz they play about 1.5
   semitones low (`docs/NAM.md`). The stock firmware runs its effects at 48 kHz. Moving OpenPocket to 48 kHz fixes the
   user models without resampling, but costs 9% more CPU per second of audio and needs the factory models retrained
   (`tools/train_a2.py`). Worth it only together with items 1 to 3, which free more than that.
7. **Smaller blocks later.** 64 frames at 48 kHz (1.33 ms) against our 128 at 44.1 (2.9 ms): lower latency, but the
   per-block overheads (the network's layer-by-layer pipeline, the two-core hand-off) count twice as often. Not before
   the NAM pipeline has been reworked for it.
8. **Bluetooth, if it comes back:** radio interrupts below the audio interrupt, on CPU0, and no radio work on the core
   that runs the network at the time it runs it (section 2).

## 6. Not done yet

- The ALNK clock registers were not traced to a written rate; 48 kHz rests on the effects' coefficient code.
- The exact network behind the stock `NAM` type and the role of overlay B (possibly the SBC decoder for A2DP).
- The 20 effect blocks after the overlays are mapped by address but not each named.
