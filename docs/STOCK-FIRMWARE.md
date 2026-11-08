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
**Read (third pass, section 7):** the dry chain runs inside the audio interrupt itself; only the delay and the reverb
run in the `wet_audio` task, one block behind. In our v37 attempt
the radio's work could hold up the audio block, which fits the "BYP 150%" we saw. Any return of Bluetooth should keep
that order: audio interrupt highest, radio below it, radio work on the core that
does not run the network.

## 3. Audio input and output

**Read** (ALNK ISR `0x202A230`):

- **64-frame blocks**, 24-bit samples, stereo I2S on ALNK0 (the same pins OpenPocket uses).
- The input gain is applied as an integer multiply and `>> 15`; peak meters are kept per block.
- Two counters, **early** and **late**, count blocks that arrive before or after the expected time: the stock firmware
  watches its own real-time margin the way our `LATE` counter (v37) now does.

**Read (fourth pass, corrected): 44.1 kHz, not 48.** The ALNK0 set-up (`f_02023a20`, at `0x2024A6C`) clears bits 8..11
of CLK_CON2 (`0x10014`), which selects the 44.1 kHz clock family (the JieLi driver sets bit 8 for 48 kHz), and writes
the same dividers as the SDK's 44.1 kHz case (`ALNK0 + 0x0C`: bits [4:2] = 0, [7:5] = 4). Every model's set-up is
then called with **fs = 44118** (`0x472C5600`, eleven places: the amp, gate, compressor, drives, reverbs, delays,
modulation): the stock firmware takes its real rate to be 44,118 Hz, 0.04 % above 44,100 (**guess**: the audio clock is
not an exact 44.1 kHz multiple, as 240 MHz / 5440 = 44,117.6 Hz would be). The `48000.0` seen earlier is a fast path:
the coefficient functions accept 44,118, 44,100 or 48,000 and skip the table work when fs is 48,000. The delays and
the reverbs run at half of it, 22,059 Hz (the constant is in their set-up). So the stock firmware and OpenPocket run at
the same rate; the stock firmware uses 64-frame blocks (1.45 ms) where OpenPocket uses 128 (2.9 ms).
- The DMA: LEN = 128 words (64 frames, 24-bit stereo), two 1 KB buffers at `0x1C20E20` (out) and `0x1C21220` (in),
  channel 0 and 1 each set to 24-bit in CON1.

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

**Read** (`f_01c05350`, `f_01c0b0f6`, `f_01c0b02a`). The delays and the reverbs take the block, sum it to mono,
**decimate it 2:1** with a polyphase IIR half-band filter (two all-pass sections per branch), run the effect on
**32 samples at 24 kHz**, and **interpolate 1:2** back, with the crossfade gain applied in the interpolator. A delay or
a reverb loses nothing audible above 12 kHz, and costs half.

**Correction (third pass, read):** the modulation effects do **not** go through this wrapper. Their process functions
(`0x1C059E6`..`0x1C05A04`) all call `f_01c05924`, which runs the effect at full rate on 64 samples, once per channel
with two states (left, right), and mixes it with the dry signal (`f_01c08d08`, the mix ramped by a one-pole of
0.0183 a block).

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
A2-Lite's ~1,730. **Read (fourth pass): it is A2-Lite.** The firmware holds the A2-Lite layer table at `0x2055A74`:
kernel sizes 6 (x14), 15, 15, 6 (x7) and dilations 1, 3, 7, 17, 41, 101, 239 (twice), 1, 13, then 1..239 again,
exactly the 23 layers OpenPocket's `engine/px_nam.c` runs, and 1,871 is A2-Lite's weight count. So the stock `NAM`
type plays Neural Amp Modeler A2-Lite captures, in fixed point, at 44.1 kHz like OpenPocket.

## 5. What OpenPocket can take from it

v38 has taken items 1, 3 and 5 ([WHATS-NEW-v38.md](WHATS-NEW-v38.md)). Item 2 was measured first and left out: MOD and
DELAY cost 1 to 3 % of the NAM network, so running them at half rate would free about 1 %.

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
   semitones low (`docs/NAM.md`). (Corrected in section 8.1: the stock firmware runs at 44.1 kHz too.) Moving OpenPocket to 48 kHz fixes the
   user models without resampling, but costs 9% more CPU per second of audio and needs the factory models retrained
   (`tools/train_a2.py`). Worth it only together with items 1 to 3, which free more than that.
7. **Smaller blocks later.** 64 frames at 48 kHz (1.33 ms) against our 128 at 44.1 (2.9 ms): lower latency, but the
   per-block overheads (the network's layer-by-layer pipeline, the two-core hand-off) count twice as often. Not before
   the NAM pipeline has been reworked for it.
8. **Bluetooth, if it comes back:** radio interrupts below the audio interrupt, on CPU0, and no radio work on the core
   that runs the network at the time it runs it (section 2).

## 6. Second pass (v38): how the effects' code is kept fast

**Read.** The effects' per-sample code lives in 20 blocks of flash (`0x2095728..0x209A0F4`, a table of {address, size}
at `0x2055DA0`), each linked to run at one of four RAM slots, and copied there when its type is chosen: the code that
runs is always in RAM, and only the chosen types' code is there. Found by the slot address that makes each block's calls
land on known functions:

**Corrected in the fourth pass** (section 8: the loader and each block's caller were found, so this is read, not
guessed):

| RAM slot | Blocks | What they are |
| --- | --- | --- |
| `0x1C23F80` (1,456 B) | 0..5 | the six reverbs: Room, Hall, Swell, Spring, Shimmer, Cloud |
| `0x1C24540` (2,244 B) | 6..10 | the five delays: Analog, Duck, dTape, Dual, Lofi |
| `0x1C24E20` (980 B) | 11..17 | the seven modulations: Chorus, Phaser, Tremolo, Flanger, Vibrato, Univibe, Autofilter |
| `0x1C25200` (1,172 B) | 18, 19 | two of the six amp set-ups (`f_0202cbfc`, `f_0202ce20`), with the 6x oversampling |

Each block is one type's own routine: no test of the type inside the sample loop. **Read:** their reverbs keep their
lines in 16 bits, as OpenPocket's do since v35.

What OpenPocket took from it in v38: the delay and the reverb run over the block in a loop made for their type (the
type's tests done once a block, not once a sample: on this CPU a taken branch costs about 8 cycles), with the output
unchanged bit for bit. Moving the effects' code into RAM as well would need the engine split so that no call crosses
between flash and RAM code (out of the CPU's call range): left for when the per-block times on the pedal (the tuner
page's footer, v38) show the effects are worth it.

## 7. Third pass: from the audio interrupt to the effects

This pass followed the audio from the I2S interrupt to each effect, instruction by instruction, instead of starting
from the effects. It corrects section 2 (where the DSP runs) and section 4 (the modulation's rate).

### 7.1 The whole dry chain runs in the audio interrupt

**Read.** `request_irq(11, 4, 0x202A230, CPU0)` is the only audio interrupt (`f_02023a20`, at `0x2024B62`). Its
handler, for each 64-frame block:

1. copies the 128 words out of ALNK0 (`f_01c06f1e`), sign-extends the 24-bit samples with an input shift (x1, x2 or x4)
   and keeps peak meters (`f_01c06f2c`, `f_01c06f6e`, `f_01c06fba`);
2. calls `f_01c01364(in, out, 64)`, which is either a plain copy (effects off) or **`f_02001326`, the effect engine**;
3. mixes in the computer's and the phone's audio: USB playback comes in through a ring of 1,024 packed 24-bit frames
   at `0x1C17FC0`, written by the USB side with a linear-interpolation resampler (`f_0202ab58`) and read in the
   interrupt (`f_020296bc`, `f_020298d8`); Bluetooth (A2DP) through a ring of 6,144 16-bit samples at `0x1F29EE0`
   (`f_02029a6e`), each with its own gain; several weak hooks in this path are linked to address 0 (unused here);
4. times itself: it reads the free-running timer at `0x1EEF0F0` at the start and at the end, and keeps the sum and the
   maximum over 512 blocks (`0x1C1AF50 + 620..660`, the average at `+2060`, the maximum at `+2064`), plus the early and
   late counters of section 3.

`f_02001326`, inside the interrupt:

- converts the block to float, **two planar channels of 64** (`0x1C14320`: left, then right at `+256`, `f_01c0b2e2`);
- applies the volume with a **linear ramp over the block** when it changes (step `(new - old) / 64` a sample);
- runs a small state machine for mute, bypass and the tuner (states 0..6 at `0x1C1AF50 + 53/54`), whose fades follow a
  256-entry curve (`0x1C0EC0C`, 0 to 1);
- runs the chain in two calls, `f_0200126a` (the slots before a split point) and `f_0200127e` (the rest);
- converts back to 24-bit with round-to-even (`f_01c0b31c`, a hardware loop of 24 cycles per stereo pair).

### 7.2 The chain runner

**Read** (`f_02000d4c`, data at `0x1C1AF50`):

- six slots, each with a category (`+1010 + i`), an on flag (`+1004 + i`), an instance (`+1428 + 4i`), a descriptor
  (`+1084 + 4i`) and its parameters (`+2528 + 12i`, six 16-bit values); the active slots are listed in order at
  `+1016` before each block;
- each slot runs through `f_01c0130e(instance, buffer, 64, process, ...)`, in place;
- when a slot is switched on or off, its output is **crossfaded over 320 samples** (`f_0200088a`: a linear ramp of
  `1/320` a sample, about 7 ms), never cut;
- the slots of category 3 (delay) and 4 (reverb) are not run here (section 7.3);
- the same function also feeds the tuner: the block is decimated by 16 with a 26-tap symmetric FIR (13 coefficient
  pairs) into a 1,024-sample ring, about 3 kHz, read by `tuner_task`.

### 7.3 The delay and the reverb run beside the chain, one block late

**Read.** This is how the stock firmware keeps its time effects from ever making the dry sound late:

- at a delay or reverb slot, the interrupt calls `f_02000c36(lane, ...)`: lane 0 is the delay, lane 1 the reverb;
- `f_01c00e32` does the exchange in one pass: the output is `dry x gain + the lane's wet output of the previous block`
  (stereo, left at `+48` and right at `+304` of the lane's buffer), and the lane's next input is the **mono sum**
  `(L + R) / 2` of this block. Two buffers a lane, used in turn;
- then it posts a semaphore (`ctx + 920`, `f_0205ca82`) to wake **`wet_audio`**, a task at priority 24 with a
  1.5 KB stack (`f_0205d870` at `0x2023606`, entry `0x202A910`);
- `wet_audio` waits on that semaphore, runs each lane that has a block (`process(handle, out, in)`), swaps the buffers
  (`f_02001b10`), and times each lane with the same timer (`ctx + 2700 + 4 x lane`: count, last, maximum);
- **likely:** if a lane is not ready when the interrupt needs it, the block passes with the dry part only and a
  counter goes up (`[lane + 28]`), instead of the whole output being late.

So the wet sound comes 64 samples (1.3 ms) late, which no one hears in an echo or a reverb tail, and the delays and
reverbs (at half rate, section 4) run in a task that the interrupt never waits for. The dry path (gate, drive, amp,
modulation, cabinet) is the only thing that must finish inside the block.

### 7.4 Presets and the effect catalogue

**Read** (`f_02001efc` checks a preset, `f_02001e04` repairs one):

| Offset | Size | What |
| --- | --- | --- |
| 0 | 2 | volume, 0..100 |
| 2 | 6 | the order: a permutation of 0..5 (checked bit by bit, `f_02001d74`); the default is 0,1,2,3,4,5 |
| 8 | 6 | on / off, one byte a slot |
| 14 | 6 | the model in each slot |
| 20 | 72 | six 16-bit parameters a slot, each clamped to its descriptor's min and max (0..100 in the UI) |

92 bytes, then a "changed" flag. The categories (table of six pointers at `0x2054EDC`, entry `{index, count, list,
single descriptor}`):

| Category | Models | Descriptor(s) |
| --- | --- | --- |
| 0 | 10 | Noise Gate++, Boost, Compress, AI Gate MS, MW-808, MW-GOLD, MW-OCD, MW-RAT, MW-MUFF, MW-FACE (list `0x2046E30`) |
| 1 | 20 | one AMP descriptor (`0x2046D38`) for all 20 amps, the model passed to its init |
| 2 | 7 | Chorus, Phaser, Tremolo, Flanger, Vibrato, Univibe, Autofilter (`0x204F81C`) |
| 3 | 5 | Analog, Duck, dTape, Dual, Lofi (`0x204F5E8`) |
| 4 | 6 | Room, Hall, Swell, Spring, Shimmer, Cloud (`0x2046FEC`) |
| 5 | 20 | one CAB descriptor (`0x2046DB0`) for all 20 cabinets (Lvl, Lcut, Hcut) |

### 7.5 Shared tables and curves

**Read** (RAM data, values checked):

- `0x1C0E408`: `tanh(i / 64)` for i = 0..511, linear interpolation between entries: the soft clip of the amp's input
  and of five waveshaper curves (a table of five functions at `0x2054EB4`);
- `0x1C0DE00` (square root of the mantissa, 0.707..1) and `0x1C0F414` (log2 of the mantissa, -1..0): together they make
  `asinh(x) = ln(|x| + sqrt(x^2 + 1))` without a library call (`f_01c05a18`), another soft clipper;
- the knobs go through small mapping functions (`f_01c05b52`..`f_01c05d32`), not straight multiplications.

### 7.6 Every interrupt

**Read** (all 20 `request_irq` calls; number, priority, core):

| IRQ | Priority | Core | Handler | What, where read |
| --- | --- | --- | --- | --- |
| 11 | 4 | CPU0 | `0x202A230` | ALNK0, the audio (7.1) |
| 9 | 6 | CPU0 | `0x20294EC` | USB device (works on `0x51000`) |
| 62 | 5 | CPU1 | `0x2030AA8` | completion of a hardware job: clears `0x10800` bit 14, calls the job's callback |
| 46 | 3 | CPU1 | `0x204303A` | a peripheral at `0x13D00` that posts a semaphore |
| 123 | 6 | both | | software interrupt (scheduler) |
| 124, 125 | 7 | CPU1, CPU0 | | software interrupts between the cores |
| 44 | 2 | CPU0 | `0x2030DC2` | radio (section 2) |
| 3, 5 | 1 | CPU0 | `0x205DF6E`, `0x2030C02` | timers |
| 2, 6, 16, 24, 28, 49, 50 | 0..7 | CPU0 | | not traced |

The audio interrupt is above everything that does real work on CPU0 except the timers (1) and the radio (2): the
radio interrupt is short and its work is done in tasks.

### 7.7 What OpenPocket can take from this

1. **Take the delay and the reverb out of the critical path** (the largest gain for crackles; **done in v39**,
   [WHATS-NEW-v39.md](WHATS-NEW-v39.md), with a lag of two 128-frame blocks that the delay's taps make up for). Today they run inside the
   block that must finish before the next interrupt, in series. As in the stock firmware: send a mono copy at their slot,
   return their wet output one block later, and let the dry path finish without them. A late wet block then costs an
   echo's 2.9 ms, not a crackle. With a NAM amp, CPU1 already runs everything after the amp; the wet part could go to
   whichever core has time left at the end of the block.
2. **Time the interrupt the stock way:** sum and maximum over 512 blocks from a free-running timer, plus the wet task's
   own times per lane. v38's footer shows the averages; the maximum is what predicts a crackle.
3. **Ramp the volume over the block** (v38 glides it already) and **crossfade on/off over ~7 ms** (v38: 23 ms).
4. **One `tanh` table for every soft clip**, as above, instead of polynomial approximations per sample.

## 8. Fourth pass: the rate, the code blocks, the network, the overlays

### 8.1 The sample rate

44.1 kHz (fs = 44,118 in the code), not 48: section 3. This corrects section 5's item 6, which rested on the 48,000
constant: the stock firmware plays its captures at 44.1 kHz as OpenPocket does.

### 8.2 How an effect's code gets into RAM (read)

`f_0201c0a8(block)` loads block 0..20 into its slot: blocks 0..5 into slot 0, 6..10 into slot 1, 11..17 into slot 2,
18..20 into slot 3. The slot table `{start, end}` is at `0x2055150` and the block table `{flash address, size}` at
`0x2055DA0`, both reached from one base pointer (`0x2054870`), which is why no code holds their addresses. It skips the
copy when the slot already holds that block (`0x1C0B3A8`, one byte a slot), checks the sizes and alignment, copies,
`csync; ssync`, and compares the copy with the flash before it records the block (`memcmp`). Each effect's init
calls it with its own block, so only the four chosen types' code is ever in RAM. The process function then runs the
slot's address through a pointer (the modulation's `f_01c05924` calls `[instance + 8]`, set to `0x1C24E20`).

| Block | Effect (init) | What its set-up shows |
| --- | --- | --- |
| 0 | Room (`f_0202e9cc`) | 8 all-passes of 29, 113, 209, 308, 68, 156, 254, 363 samples, gains alternately -0.6 and +0.6; one 5,132-sample line read at 16 taps (1,386..4,790), at 22,059 Hz (63..217 ms) |
| 1 | Hall (`f_0202ec0c`) | **likely** the same structure: a line of 2,470 and 16 taps from 2,036 to 4,937 |
| 2 | Swell (`f_0202eed8`) | an 8,196-sample buffer and the RiseT envelope (rates 22,059 / 441 / 17,647) |
| 3 | Spring (`f_0202f080`) | lines of 5,440 and 12,592, two of 640, a 9.6 kHz filter: the "Combs" knob |
| 4 | Shimmer (`f_0202f2a8`) | six short lines (111..220) and a 4,241-sample pitch-shifter line |
| 5 | Cloud (`f_0202f46e`) | a 5,224 line tapped at 862..3,969, a 3,969 line, all-passes of 328, 990, 1,384, 2,042, 2,455 |
| 6 | Analog (`f_0201d7e6`, type 11, max 781) | 32 Hz, 300 Hz, 1.2 kHz and 4 kHz filters (the bucket brigade's tone) |
| 7 | Duck (type 12, max 789) | envelope times from `ln(0.001)` (the ducker) |
| 8 | dTape (type 13, max 790) | wow and flutter (`Speed`, `Depth`), `Grit` |
| 9 | Dual (type 14, max 478) | two heads (20 16-bit reads a pass) |
| 10 | Lofi (type 15, max 782) | an 8-band EQ curve (615 Hz -8.4 dB, 738 -6.2, 749 +3.1, 1.6 k +2.4, 4.1 k -1.0, 6.2 k -5.5, 6.7 k -3.8, 7.2 k ...) |
| 11..17 | Chorus, Phaser, Tremolo, Flanger, Vibrato, Univibe, Autofilter | full rate, two states (left, right), mixed by `f_01c08d08` |
| 18, 19 | amp set-ups | the 6x oversampled stages (section 4) |

All five delays share one init (`f_0201cc56`, the type 11..15 passed in, and a maximum time 478..790, **likely** in
ms) and run at 22,059 Hz in 16-bit lines.

### 8.3 The cabinet

CAB init (`f_02009da6`): 20 models, IRs of 1,024 taps convolved in 64-frame partitions (the same sizes as OpenPocket's
USER IR, v38), low cut from 20 Hz, high cut to 18 kHz.

### 8.4 The two overlays

Overlay A (7,232 B, the fixed-point kernels) is loaded by the AMP's init: the A2-Lite network (section 4). Overlay
B (6,756 B) is loaded by `f_0200f0ca`, called from a screen handler (`f_0200f534`, in the table of screen handlers at
`0x204F590`) that sets up a pitch detector with A = 440 Hz (`-440.0`), 0.01 thresholds and a decimation by 8,192 /
44,118: **likely** the tuner page. The NAM kernels and the tuner then share the same 7 KB of RAM, swapped when the
tuner opens (the output is muted there anyway).

### 8.5 Which core runs `wet_audio`

The task is created without a core: JieLi's task creation pins a task only when its name starts with `#C0` or `#C1`
(`f_0205d0f6` reads the digit), and no task name in the firmware does (only `IDLE0` / `IDLE1` are per core). So
`wet_audio` runs on whichever core is free: with the audio interrupt on CPU0, **likely** mostly CPU1.

### 8.6 The reverbs and the delays, read instruction by instruction

**The reverb core, shared by five of the six reverbs** (`f_01c08260` in the RAM library, called through `f_01c05ae4`;
set up by `f_0202e826`). Per sample, at 22,059 Hz, in float:

1. one LFO: a 32-bit phase accumulator read through a 256-entry quarter-wave sine table (`0x1C0E204`), interpolated;
   its depth is smoothed every sample (one-pole, 1.44e-4). Two offsets come out of it, `(1 + sin) x depth` and
   `(1 - sin) x depth`;
2. **eight delay lines**, each read at its length plus one of the two offsets (alternately), with a 4-point Hermite
   (cubic) interpolation, and each followed by **its own Schroeder all-pass** (a gain per line): every line is
   modulated and diffused, not only four;
3. an optional per-sample hook on the eight outputs (`[ctx + 404]`): the Shimmer passes its pitch shifter here
   (`f_02030500`), the others pass nothing;
4. an **8 x 8 Hadamard** mix (fast Walsh-Hadamard butterflies, x 0.35355 = 1/sqrt 8);
5. the outputs, **in stereo**: left = the sum of the eight, right = (first four) - (last four), both x 0.35355;
6. each line is written with `input x its input gain + its mixed output x its feedback gain` (the decay).

Each type puts its own front end before it and the same post after it (two one-pole filters a channel, high-pass then
low-pass, `f_01c07f22`, set by HPass and LPass):

| Type | Before the core |
| --- | --- |
| Room | a 5,132-sample line read at 16 taps with their own gains: 12 taps make one signal, 4 another; each goes through 4 all-passes (29, 113, 209, 308 and 68, 156, 254, 363, gains +-0.6, `f_01c07f8e`); their sum feeds the core, and is also added to both outputs (the early reflections) |
| Hall | a pre-stage of its own (`f_01c081ca`, `f_020303c6`), then the core |
| Swell | an input envelope (RiseT, `f_02030850`) and **two** cores |
| Spring | the input **convolved with a spring impulse response** by the same partitioned FFT convolution as the cabinets (`f_01c04f9a`), then the core |
| Shimmer | the core with the pitch shifter in its hook (above), three all-pass pairs |
| Cloud | no core: four modulated lines read with a Hermite and a fixed 4th-order low-pass each (`f_01c0804c`), with `asinh` saturation (`f_01c05a18`) in the loop |

**The delays** (block 9, Dual, read in full; the others call the same LFO, `f_01c05098`). Per sample at 22,059 Hz:

1. each head's length moves to its target by a one-pole (1.44e-4 a sample): turning TIME never jumps;
2. each head is read from a **16-bit** line with a 4-point Hermite whose differences are taken **in integers**
   (`3 x (b - c) - ...` on the int16 samples) and only then converted to float; a read on a whole sample skips it;
3. the heads are summed (0.9 and a set gain, x 1/32768) and **soft-clipped through the `tanh` table** (x 64.7, the
   512-entry table of section 7.5): that is the wet output;
4. the feedback goes through **two biquads** (the tone) and a short **modulated float delay** (Speed, Depth: the
   LFO above, Hermite read): the wow sits inside the loop, so it builds up on each repeat;
5. the line is written with `input x gain + feedback x gain`, scaled by 32768 and **saturated to 16 bits** (`sat16`).

So the stock delays and reverbs are float DSP on 16-bit (delays) or float (reverbs) memory, at half rate, with
every time and depth smoothed per sample and every read a cubic. OpenPocket's (v35) are of the same family: an 8-line
Hadamard FDN at half rate, a 16-bit delay line, Hermite reads. What the stock ones do that OpenPocket's do not:

- all eight lines modulated, each with a nested all-pass (OpenPocket: four modulated, diffusion only before the tank);
- **stereo** reverb output (sum and difference); OpenPocket's output is mono on both channels;
- the delay's output soft-clipped by `tanh` and a modulated delay inside the feedback loop;
- the Spring as a convolution with a real spring's impulse response, ahead of the tank.

## 9. Not done yet

- The meaning of the order bytes in a preset (slot to category or the reverse: the default is the identity).
- The modulation blocks' per-sample routines (8.6 covers the reverbs and the delays), and Hall's and Cloud's front ends in
  full.
- The six amp set-ups' circuits, and the `AM2` / `AM3` / `AM4` / `LMT` model formats.
