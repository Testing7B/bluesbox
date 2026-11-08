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

### The MW-808 drive (first taken for the amp): 6x oversampled, one division per sample

**Read** (`f_0202d236`). **Corrected in the fifth pass (section 10.2):** this is the process function of the **MW-808**
drive (category 0), not of the amp; the amp's own process is `f_0202d7d8` (section 10.1). Per 16-frame sub-block:

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
`AM3`, `AM4` (its own amp captures: **read in the fifth pass**, section 10.1, a FIR, a 16-unit GRU and a FIR), `NAM` (type 16, 1,871 float weights, 7.5 KB) and `LMT`. **Likely:**
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
| `0x1C25200` (1,172 B) | 18, 19 | the process routines of the MW-GOLD and MW-RAT drives (set up by `f_0202cbfc`, `f_0202ce20`; **corrected in the fifth pass**: these are drives, not amps, section 10.2) |

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

- `0x1C0E408`: `tanh(i / 64)` for i = 0..511, linear interpolation between entries: the soft clip of the amp's input,
  the chorus's input and one of the five LFO shapes at `0x2054EB4` (**corrected in the fifth pass**: those five functions
  turn a 32-bit LFO phase into a 0..1 wave, sine, soft square and others; they are not waveshapers);
- `0x1C0DE00` (square root of the mantissa, 0.707..1) and `0x1C0F414` (log2 of the mantissa, -1..0): together they make
  `asinh(x) = ln(|x| + sqrt(x^2 + 1))` without a library call (`f_01c05a18`), another soft clipper;
- `f_01c05b52`..`f_01c05d32` are those five LFO shapes (above), not knob mappings as first written.

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
| 18, 19 | MW-GOLD, MW-RAT drives | their oversampled clipping stages (section 10.2; first taken for amp set-ups) |

All five delays share one init (`f_0201cc56`, the type 11..15 passed in, and a maximum time 478..790, **likely** in
ms) and run at 22,059 Hz in 16-bit lines.

### 8.3 The cabinet

CAB init (`f_02009da6`): 20 models, IRs of 1,024 taps convolved in 64-frame partitions (the same sizes as OpenPocket's
USER IR, v38), low cut from 20 Hz, high cut to 18 kHz.

### 8.4 The two overlays

Overlay A (7,232 B, the fixed-point kernels) is loaded by the AMP's init: the A2-Lite network (section 4).
**Corrected in the sixth pass (section 11.4):** overlay B (6,756 B) is not the tuner. The loader `f_0200774c(n)` takes
n = 1 for A (from flash `0x2093AE8`) and n = 0 for B (`0x2092084`), both into the same window at `0x1C22340`; the AMP's
init asks for A when the model is a `NAM` file and for **B for every other model: B holds the `AM2`..`AM5` (GRU)
kernels**. The tuner page (`f_0200f0ca`) calls the loader with n = 2, which only releases the window: the 7 KB are then
free for the tuner while the amp is silent.

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
- ~~The six amp set-ups' circuits, and the `AM2` / `AM3` / `AM4` / `LMT` model formats.~~ Done in the fifth pass (sections
  10.1 and 10.2): the six "amp set-ups" are the six MW drives, and the formats are read.

## 10. Fifth pass: the amp, the model files, the drives, the mods, the flash, MIDI

Read instruction by instruction unless marked **likely**. Two earlier claims were wrong and are corrected in place: the
"6x oversampled amp" of section 4 is the **MW-808 drive**, and the "six amp set-ups" (table `0x205502C`) and code blocks
18 and 19 are the **six MW drives**.

### 10.1 The amp and its model files

**The AMP descriptor** (`0x2046D38`; knobs Gain, Level, Bass, Middle, Treble) has init `f_02008692`, set `f_02009c66`,
process `f_0202d7d8`. All 20 amps are **captured models read from the flash**: there is no circuit simulation of an amp
in the firmware.

**Process** (`f_0202d7d8`), per block:
1. input gain, smoothed (`0.97 / 0.03` one-pole);
2. a soft knee: `|x| <= 0.5` passes, above it `0.5 + 0.5 * tanh(2 * (|x| - 0.5))` (the 512-entry table), sign kept, so the
   input never exceeds 1.0;
3. three stage functions through pointers (`ctx+156/160/164`), filled by the init from the model's type; unused stages
   point at an empty routine;
4. a dry/processed blend from byte 18 of the model's header (0..100: below 50 the processed part is scaled by `2 x b/100`,
   above it the dry part by `2 - 2 x b/100`); forced to 100 for NAM files;
5. the tone stack (`f_02007d68` designs it with a shelf designer `f_02007abc` and a peak designer `f_02007c4e`, Q 0.707;
   `f_01c057e4` runs it): three biquads (**likely** low shelf, mid peak, high shelf), each described by `{frequency, min dB, max dB}` **taken from the model's header** (bytes 24..59). The default, used for NAM
   files, is `{150 Hz, -20, +20}`, `{425 Hz, -15, +15}`, `{1800 Hz, -10, +10}` (`0x205553C`);
6. Level (`f_01c0942e`).

**The model file validator** (`f_0200216e`) recognises five formats by their first four bytes; every float is checked
for NaN and infinity. All have a 60-byte header:

| Magic | Type | Body after the header | Size |
| --- | --- | --- | --- |
| `AM2`, `AM3` | 2 | 1,536 floats | 6,208 B, or 6,272 with an `LMT` trailer |
| `AM4`, `AM5` | 4 | 1,952 floats | 7,872 B, or 7,936 with an `LMT` trailer |
| `NAM` | 16 | 1,871 floats (A2-Lite, section 8) | 7,548 B, or 7,836 with a `BEND` trailer |
| `CAB` | 32 | 1,024 taps in **signed 24-bit**, little endian, scaled by 2^-23, header 20 B, byte 18 = 1 or 2 | 3,092 B |

**What `AM2`/`AM3`/`AM4`/`AM5` are** (the init's type-specific set-ups): a **Wiener-Hammerstein model with a small GRU** in
the middle, run in fixed point.

| Part | `AM2` / `AM3` | `AM4` / `AM5` |
| --- | --- | --- |
| input FIR | 256 taps | 512 taps |
| GRU | 16 units, 1 input: 48 input weights, 3 x 16 x 16 recurrent, 16 output weights, **no biases** (832 floats) | the same with the two bias vectors (`b_ih`, `b_hh`, 3 x 16 each; 928 floats) |
| output FIR | 256 taps | 512 taps |
| more | three 64-tap filters (reversed, FFT'd) | — |

The FIRs are turned into 64-frame FFT partitions (`f_02008098`) like the cabinets. The GRU's weights are converted to
**Q21** (`x 2,097,151`) into rows of 17 words (`f_02008114`); the gate order is r, z, n (the n gate keeps `b_hn` apart,
as PyTorch's GRU). The `LMT` trailer is `"LMT\0"` and 16 floats: `f_02008380` stores them as a second 16-weight output
row and takes their dot product with the first (**likely** a limit or level head; its use was not followed further).
An `AM4` with `LMT` gets one more stage (`f_020302e2`) and two filters at about 15 Hz and 500 Hz (`f_020084a2`,
`f_020084f0`).

**NAM's `BEND` trailer**: `"BEND"` and 72 floats, read as 24 triplets scaled by 2^20 into each layer's record (48-byte
records). **Likely** per-layer fixed-point ranges (calibration); without it the init uses defaults.

### 10.2 The drives (category 0)

The ten models of category 0: Noise Gate++ (init `f_0200a18e`), Boost (`f_0200ae00`), Compress (`f_0200b1fa`), AI Gate
MS (`f_0200acf4`), and six **MW drives**. Each drive's init is a stub calling `f_0201da90(i)`, which checks the RAM
(module registry, below) and calls `0x205502C[i](44118.0)`; all six share the set function `f_02020790` and the process
`f_02020b24`, which calls `0x204FD70[i]`:

| i | Drive (knobs) | Set-up | Process | State |
| --- | --- | --- | --- | --- |
| 0 | MW-808 (4) | `f_0202cfd0` | `f_0202d236`: 2x then 3x up, a rational asymmetric diode curve, down (section 4) | 1,312 B |
| 1 | MW-GOLD (4) | `f_0202cbfc` | code block 18 at `0x1C25200` | 2,560 B |
| 2 | MW-OCD (6) | `f_0202c962` | `f_0202ca82` | 1,792 B |
| 3 | MW-RAT (4) | `f_0202ce20` | code block 19 at `0x1C252AE` | 2,192 B |
| 4 | MW-MUFF (4) | `f_0202d4d0` | `f_0202d60e` | 1,952 B |
| 5 | MW-FACE (3) | `f_0202c874` | `f_0202c8de` → `f_01c0556c` | 3,904 B |

GOLD, OCD, RAT and MUFF go through one oversampled shaper, `f_01c0549a`: up 2x (`f_01c0758a`) and 3x (`f_01c076d0`),
three curves (`f_01c07e10`, `f_01c07e92`, `f_01c07edc`), down 3x and 2x; around it, biquads (`f_01c0944a`) and ramped
gains (`f_01c0796c`). FACE has a chain of its own (`f_01c079c8`, `f_01c07a1e`, `f_01c07bbc`, `f_01c07d8c`). So every
stock drive clips at **6x the rate (264.7 kHz)**. The circuits' coefficients were not tabulated.

**The module registry** (`0x2050018`, 53 entries, checked by `f_0200210c` against `{44,118 Hz, 64 frames}`): the RAM
each module needs, so the firmware can refuse a model it has no room for. Gates/dynamics 400..624 B; delays (ids 11..15)
50..82 KB; reverbs (16..21) Room 56 KB, Hall 57 KB, Swell 51 KB, Spring 73 KB, Shimmer 63 KB, Cloud 79 KB; mods (38..44)
Chorus 3.7 KB, Phaser 1.1 KB, Tremolo 0.7 KB, Flanger 1.6 KB, Vibrato 4 KB, Univibe 1.1 KB, Autofilter 128 B; drives
(45..50) 1.7..4.3 KB; id 51 (**likely** NAM) 70 KB; id 24 (**likely** the cabinet) 133 KB.

### 10.3 The modulations

All seven run at full rate in stereo through one wrapper (`f_01c05924`): each channel is copied to a scratch buffer, run
through the type's kernel (code blocks 11..17), and mixed with the dry signal by `f_01c08d08`. The **Mix** knob is
smoothed (`0.0183` a block) and becomes **equal-sum gains**: below 50 % dry 1 and wet `2 x mix`; above it dry
`2 - 2 x mix` and wet 1.

**Chorus** (block 11, read in full):
- a 32-bit phase accumulator LFO into a 512-entry sine table (`0x1C0F818`, linear interpolation);
- depth smoothed per sample (`1.44e-4`), the delay position smoothed again (`0.0143`);
- the line read with a 4-point cubic interpolation;
- the input is coloured before it enters the line, as a bucket brigade would: `10 x tanh(0.1 x + 0.1) - 0.987`, a gentle
  asymmetric saturation with its DC removed, then a two-pole low-pass;
- the kernel outputs the wet signal only.

**Phaser** (block 12): works in 16-sample chunks: LFO (`f_01c05dca`), all-pass stages (`f_01c05e2c`), coefficient tables
chosen by the stage count (up to 4 tables at `0x2054870 + 1604`); a change of rate, depth or stages re-designs it
(`f_020308fc`); two output filters. **Univibe** (block 16) is the same frame with its own LFO (`f_01c05eaa`), the
classic four-stage phaser with an unequal, lamp-like sweep (**likely**). **Tremolo** (block 13) and **Autofilter**
(block 17, with the table sine and cosine `f_02007992`, `f_02007a22`) are short kernels. **Flanger** (block 14)
and **Vibrato** (block 15) are the long ones (284 and 260 instructions): modulated lines with feedback (Flanger: gains
0.6..0.95, the five LFO shapes of `0x2054EB4`), Vibrato with the `tanh` input colour of the Chorus (`6.4`) and a
512-entry table. They were not read line by line.

### 10.4 What the stock firmware keeps in the flash

**Read** (`f_02022e82`, at boot): the user data is in two files of the flash filesystem, **refused unless they sit at
these addresses**:

| File | Flash | Size | What |
| --- | --- | --- | --- |
| `EXT_RESERVED/usr` | `0xBA000` | 12 KB | not followed |
| `EXT_RESERVED/pd40j` | `0xBF000` | 256 KB | the user area, below |

Inside `pd40j`:

| Offset | Size | What |
| --- | --- | --- |
| `0x00000` | 20 x 8 KB | the 20 amp models, each validated: only types 2, 4 or 16 (`AM2/3`, `AM4/5`, `NAM`) |
| `0x28000` | 20 x 4 KB | the 20 cabinets, type 32 (`CAB`) |
| `0x3C000` | 8 KB | the preset bank: 80 presets x 92 B (section 7.4), then `PATCHEND`: 7,370 B in all |
| `0x3E000` | 4 KB | `GLB1`: the global settings, as a log |

So the stock amps and cabinets are **files the user can replace** (the editor's "PC AMP", "PC NAN" and "PC CAB" pages
name them); the firmware has no built-in amp or IR.

**`GLB1`** (`f_0200c578`): the magic, then up to 511 records of 8 bytes appended one after the other (wear levelling);
the last record that is not all `0xFF` wins. A record: byte 0 = 0; bytes 1 and 2 = two preset numbers 0..79 (**likely**
the current preset and the other one of the A/B pair); byte 3 = 0/1; byte 4 = the input gain, -10..+10 dB (shown as
`%s%d dB`); byte 5 = the MIDI channel 0..15, or 255 for OMNI; byte 6 = BT on/off; byte 7 = Loopback on/off (**corrected in the
sixth pass**: the settings page reads them so, section 11.6; the tails are not in this record).

### 10.5 MIDI

**Read** (`f_02014dfe`, the `midi_route` task's handler): incoming messages wait in a 32-entry ring (3 bytes each, the
channel already filtered by the OMNI/CH setting).
- **Program Change n**: loads preset n (`f_0200c8d0`).
- **Control Change**: the CC number indexes a 128-entry table (`0x2046704`, 12 bytes an entry: handler, name, argument),
  the same table as the footswitch functions. The value goes to the handler.

| CC | What | Value |
| --- | --- | --- |
| 0..5 | NS, AMP, MOD, DLY, REV, CAB on/off | toggles; as read, the handler acts only on value 0 |
| 20..27 | virtual keys 0..7 (the footswitches) | non-zero = press, 0 = release |
| 28 | tuner mode | |
| 40, 41, 42 | MOD tap, DLY tap, MOD+DLY tap | |
| 48..53 | NS, AMP, MOD, DLY, REV, CAB **model** | the model number, clamped to the category's count |
| 54..89 | **effect parameters**: `54 + 6 x unit + param` (units NS, AMP, MOD, DLY, REV, CAB; params 0..5) | 0..127, scaled to 0..100 with rounding |

### 10.6 Bluetooth, small things

- The advertising data (`f_020155f6`) carries the flags, the **BLE-MIDI** service UUID and the 5 bytes `sinco` (**likely**
  a maker tag: the string next to it is `sincoCAB`); the device name goes in the scan response (up to 29 bytes).
- Tasks `bt_sbc_dec` and the string `JL_A2DP`: the stock firmware also **receives Bluetooth audio** (A2DP, SBC), mixed
  into the output by the audio interrupt (section 9 of the handbook).
- OTA file names are present (`ble_app_ota.bin`, `spp_app_ota.bin`): updates over BLE and over SPP.

### 10.7 What remains not done

- Flanger, Vibrato, Tremolo, Autofilter line by line; the drives' curves and filter values; Hall's and Cloud's front ends.
- The `LMT` and `BEND` trailers' exact use; the third and fourth bytes of `GLB1`; the `usr` file.
- The tuner's pitch algorithm (`tuner_task`), the USB Audio loopback, the battery thresholds, the preset order bytes.

**Section 11 (sixth pass) takes up this list.**
- The BLE app protocol beyond BLE-MIDI (SysEx used by M-VAVE's app).

## 11. Sixth pass: the list of section 10.7, read

Two more corrections, made in place: overlay B is the `AM` models' kernels, not the tuner (8.4); bytes 6 and 7 of a
`GLB1` record are BT and Loopback, not the tails (10.4).

### 11.1 The modulations, line by line

**Tremolo** (block 13, read in full; runs in pieces of at most 64 samples):
- a 32-bit phase into the 512-step sine of `f_01c03562` (a 256-entry quarter table, `-1..+1`), linearly interpolated;
- `gain = 1 - 0.499 x depth x (1 + sin)`: from 1 down to 0.002 at full depth;
- the gain itself goes through a one-pole of 0.1 a sample (it softens the edges; the wave is a sine anyway);
- depth and level are smoothed by `0.00143` a sample; level is clamped to 0..2 and multiplies the output.

**Autofilter** (block 17, read in full): a **band-pass biquad swept by a sine LFO, its coefficients recomputed every
sample**:
- `f = fmin + (fmax - fmin) x (1 + sin) / 2`; `K = tan(pi f / fs)` from the table sine and cosine (`f_02007992`,
  `f_02007a22`);
- `b0 = K x norm`, `b2 = -b0`, `a1 = 2 Q (K^2 - 1) x norm`, `norm = 1 / (K + Q + K^2 Q)` (`Q` from the knob), direct form I;
- output = `5 x mix x band-pass + (1 - mix) x dry`: the band-pass is made up by 5x (+14 dB).

**Flanger** (block 14, read in full), per 16-sample piece then per sample:
- knobs: rate `0.17..16 Hz`; depth `0..0.8`; feedback `0.2..0.8`; base delay **2..7 ms**; one of the **five LFO shapes**
  (`0x2054EB4`, phase to 0..1);
- every value is smoothed (`0.0046`, `0.00183`, `0.0143` a sample);
- `delay = base x (1 - depth x (1 - LFO))`; the line is **16-bit**, read with the integer-difference Hermite of the
  delays (8.6);
- the line's input is `x + feedback x delayed`, then: a first-order filter; a gentle asymmetric cubic,
  `0.95 y - (y^2 + y^3) / 64`; the `tanh` table; x 0.5; two biquads; `x 32768`, `sat16`;
- the kernel outputs `line input + delayed x 0.6 (1 - feedback)` (wet only, mixed by the wrapper).

**Vibrato** (block 15, read in full): a **tape-style vibrato**, 100 % wet, with a lo-fi knob:
- the LFO is **not a sine**: a fixed 513-point waveform at `0x2053748`, smooth but irregular (two shallow dips to 0.70
  and 0.73, one deep dip to 0, flat at 1 for the last quarter), a wow curve;
- `delay = depth x (line length - 2) x LFO`, read with the 4-point cubic of the Chorus;
- before the line: the Chorus's input colour (`tanh(0.1 x + 0.1)`), then the **bit depth** cut by a knob `b` = 0..20
  (`b` least significant bits of a 23-bit word cleared), a filter, and a crossfade from clean to reduced of
  `(1 - sin(2 pi (0.25 + 0.025 b))) / 2` (0 at `b` = 0, 1 at `b` = 20), with a make-up gain of `b/3` dB (up to 5 dB at
  `b` = 15, back to 0 at 20).

**Phaser and Univibe** (blocks 12 and 16, the coefficient designer `f_01c08b7a` and the section runner `f_01c05e2c`
read): **not all-pass stages but peaking EQs**, a model of a phaser's response:
- 3 sections (a switch: 5), each an RBJ-style peaking biquad (`A = 10^(dB/40)`, `alpha` from `1 / (2Q)`);
- centres **245, 550, 1234 Hz** (5 sections: **200, 319, 550, 950, 1640 Hz**), all multiplied together by
  `1 + 3 x LFO` (a sweep of two octaves), the LFO one of the five shapes;
- gains and Qs blended by a knob between two sets: notch/peak/notch **-16 / +11.5 / -16 dB, Q 2 / 0.6 / 2** and
  **-3 / +15 / -3 dB, Q 0.4 / 4 / 0.4** (5 sections: -16, +11.5, -17, +10.5, -16 and -3, +21, -3, +20, -3);
- the coefficients glide to their new values every 16 samples;
- the Univibe uses the same tables with **the gains' sign inverted** (`f_02030986`: `-0.08305` against `+0.08305`, so
  peaks become notches) and its own LFO routine.

### 11.2 The drives' shared clipper

`f_01c0549a` (GOLD, OCD, RAT, MUFF), read in full:
1. the bias glides to its target;
2. up 2x and 3x, then `f_01c07e10` on the 96 samples, an **asymmetric rational clipper with two knees**:
   - `s = (x + bias) x g`; below the upper knee `T1` the sample passes;
   - above it `y = (x + bias) x k2 + c2 - 1 / (s x k3 + d1)`;
   - below the lower knee `-T2`, `y = (x + bias) x k2 - c3 - 1 / (y x k4 - d2)`;
   - ten constants per drive (`g, k2, k3, d1, c2, T1, k4, d2, c3, T2`), one division a sample;
3. down 3x and 2x;
4. optional, by flag: a one-pole **DC blocker** (`f_01c07e92`) and a one-pole **low-pass** (`f_01c07edc`).

The ten constants and the filters come from each drive's knob functions, which are **circuit fits**, not tables:
- MW-808: `2.6104`, `0.1149`, 17.8 kHz and 22 kHz poles, `6.94`, `189.2`, `21.07`, `210.26`;
- MW-OCD: a table of 16 numbers that read as frequency and level pairs (103, 32.69, 28.78, 257, 36.32, 132, 33.74, 89,
  27.33, 89, 22.38, 79, 32.69, 186, 28.78, 146; the pairing was not confirmed), `173.78`, `-12.99`;
- MW-RAT: `3.65`, `1.876`, `4.123`, `1.758`, `-11.52`, `27.13`, `7.45`, `13.62`;
- MW-MUFF: `2.49`, `1.061`, 250 Hz and 525 Hz (`1570.8` and `3298.7` rad/s), `40.5`, `-26.6`, `0.8`, 350 Hz;
- MW-FACE (**likely**: these two functions are called from the shared set routine): two **4th-order polynomials** of the knob (`3909 - 23084 k + 52295 k^2 - 50424 k^3 + 18304 k^4`, and
  `0.7345 - 4.364 k + 9.830 k^2 - 9.386 k^3 + 3.285 k^4`), the shape of a Fuzz Face's pots (**likely**).

### 11.3 Hall and Cloud, before the core

**Hall** (block 1 and its set-up `f_0202ec0c`, read): lengths are given at 44.1 kHz and scaled by `0.5002` to the
22,059 Hz of the reverbs.
- A line of 2,470 samples (112 ms) read at **17 taps**: 2036, 2357, 2397, 2555, 2722, 2893, 2924, 3266, 3496, 3639,
  3873, 3887, 4250, 4268, 4479, 4649, 4937 (44.1 kHz samples: 46..112 ms).
- Each tap has its own signed gain (0.022, 0.056, -0.032, 0.079, -0.089, 0.112, -0.126, -0.112, 0.112, 0.079, -0.178,
  0.126, -0.089, -0.178, -0.141, -0.089, -0.178).
- Each tap feeds **its own all-pass of 114 samples (44.1 kHz), gain 0.75**; the 17 outputs are summed into the core.
- When every tap is longer than the block, the block is done tap by tap (a fast path); otherwise sample by sample.

**Cloud** (block 5, read in outline): two sine LFOs; two modulated lines read with Hermite; cross gains in dB steps
(0.891, -0.944, 0.841, 0.798, 0.891, -0.708, 0.631); then the four modulated lines of `f_01c0804c` (fixed 4th-order
low-pass each); `x 0.707`; `asinh` on both outputs; the same post as the others. Lengths: a 5,224 line tapped at 862,
1126, 2326, 2913, 3169; a 3,969 line; all-passes of 328, 990, 1384, 2042, 2455.

### 11.4 The AM kernels (overlay B), `LMT` and `BEND`

**Overlay B** holds the `AM` models' code (section 8.4 corrected).

**The per-sample GRU** (`0x1C22340`, read):
- the input is converted to Q21;
- **three jobs on the FFT block** (`0x1EE0400`: a job word `2721`, `CON = 0x149`, wait for bit 7, clear with bit 6)
  compute the 16 x 16 recurrent products;
- four CPU routines do the gates;
- the output is converted back (`x 2^-21`).

The stock firmware uses the FFT block as a matrix-vector engine **for its GRU**, the same trick as OpenPocket's NAM.

**The AM4/AM5 stage** (`0x1C224D0`, read): the input FIR, then the GRU per sample, then the output FIR.
- **Warm-up after loading**: for the first 1,102 samples the GRU is fed a decaying exponential and its output is
  muted (`x 1e-16`). The last output is kept as the model's DC offset and subtracted from then on.
- **With `LMT`**:
  - an envelope follower on the input (smoothed by `0.0183`) sets a gain applied around the network;
  - two filters are designed at 2 kHz and 3 kHz;
  - the init adds a first-order high-pass at 15 Hz and a first-order filter at 500 Hz;
  - a set routine (`f_020302e2`) maps three knob bytes to `0..3`, `-6..0 dB` and `-24..0 dB`;
  - the `LMT` weights are a second 16-weight output row; their dot product with the first is precomputed.
  - So `LMT` is a level-dependent stage (a limiter or a level-dependent tone); its exact law was not reduced to a
    formula.

**`BEND`**:
- 72 floats: one triplet for each of the 23 A2-Lite layers and one for the head;
- each triplet is converted to Q20 and copied to **every channel of its layer**;
- **corrected in the seventh pass (12.1)**: the triplets are not activation parameters; they pre-fill each layer's history
  ring (3 channels a frame) with its steady state.

### 11.5 The tuner

`f_0200f0ca` sets it up: the overlay window released, a pitch detector with `A = 440 Hz`, 0.01 thresholds, a window
from the sine table. `tuner_task` (`f_0202bc8c`, priority 3, 512-word stack) reads the 3 kHz ring of section 7.2.
**Read in outline**:
1. a 2,048-sample frame, windowed;
2. a spectrum through the FFT helpers; the magnitude squared (`re^2 + im^2`) of each bin;
3. the peak bin, refined with a 3-point interpolation;
4. **nine refinement passes** of a single-frequency DFT (sums of `x sin` and `x cos` at the candidate frequency)
   around the peak;
5. the result folded into an octave (doubled or halved until it fits);
6. the cents: `1200 x log2(f / ref)`, with a window of `2^(+-1/24)` (a quarter tone) around the note.

### 11.6 Settings, tails, loopback, battery

**The settings page** (`f_0200de68`) reads a struct whose bytes are the `GLB1` record:
- byte 4: MIDI channel (`CH n` or `OMNI`);
- byte 5: **BT** ON/OFF;
- byte 6: **Loopback** ON/OFF;
- the **input gain** (`%s%d dB`, signed): a state byte.

**The tails** (one state byte, set by `f_0200de08`): bit 0 Delay Tail, bit 1 Reverb Tail, bits 2 and 3 their mode;
each is **Off, Auto or Force** (strings at `0x204FF24`); a mode bit is refused without its on bit.

**Loopback**: the audio interrupt has two output mixers, `f_02029ede` and `f_02029b50` (USB record and playback paths),
chosen by a switch. What Loopback routes was not followed to the register.

**Battery**: the "Low Battery" screen is built by `f_0201adb0`. The level comes from the SDK's `adc_scan` /
`sys_power` services, not from application code; no threshold was found in the app.

### 11.7 The preset's order bytes

`f_020031f0` copies the six order bytes to the slots' categories (`state + 1010 + i`), then initialises each slot from
the category it holds. So **order[i] is the category that runs in slot i**: the chain runs slot 0 to 5, and the
default `0,1,2,3,4,5` is gate/drive, amp, mod, delay, reverb, cab. A preset with `0,1,5,2,3,4` would put the cabinet
before the modulation (**likely** as the editor offers it; the check `f_02001d74` only requires a permutation).

### 11.8 M-VAVE's SysEx protocol

**Read** (`f_02025b12` assembles `F0 .. F7` from USB or BLE MIDI; `f_02025100` handles it).

A message, between `F0` and `F7`:

| Byte | What |
| --- | --- |
| 0, 1 | `00 59` (the header) |
| 2 | command |
| 3..5 | payload length, 24-bit little endian |
| 6.. | payload |
| last | checksum: `~(sum of the payload bytes)`, 8 bits |

The total must equal the length + 7. The payload is **8-bit**, not packed to 7 bits.

| Command | What |
| --- | --- |
| `0x11` | identify: the reply carries 27 bytes, the name `BlackBox_021` |
| `0x12` | a 6-byte status reply (all zero) |
| `0x15` | stream bytes into a ring buffer and wake `midi_route` (a data channel) |
| `0x21` | object `payload[0]` (0..7), 32-bit argument: its handler 0 |
| `0x22` | object, 24-bit address: its handler 2, then an 8-byte reply |
| `0x23` | object, 24-bit address, up to 1,024 bytes: its handler 1, reply with the data |
| `0x30` | address and data: copied into a buffer and **written by the flash task** (`f_0205ca82`), with a busy handshake |

Errors get an 8-byte reply with status 1. Only objects 4 and 5 are registered (section 12.2).

### 11.9 What is still not done after this pass

- The exact law of `LMT`, and which kernel reads `BEND`'s triplets.
- The coefficient formulas behind each drive's knobs (the constants are above, not the circuits).
- Cloud and the Phaser's designer in full detail; the tuner's thresholds.
- Loopback's routing; the battery threshold (in the SDK's power service); the SysEx object list.
- The 12 KB `usr` file.

## 12. Seventh pass: the five open points

### 12.1 `BEND`: the layers' resting state

**Read.** The init (`f_02008692`, from `0x2009A1E`) takes each of the 72 floats of the trailer in threes, converts them to
Q20, and **writes the triplet into every frame of the layer's history ring** (`[layer + 56]`, `count + 72` frames of
12 bytes). A2-Lite's layers have 3 channels and their rings are interleaved `[time][channel]`, 12 bytes a frame, as in
OpenPocket's `engine/px_nam.c`. The `+ 72` is the runner's largest piece: `0x1C23464` runs the network in pieces of at
most 72 samples.

So `BEND` is **each layer's output at silence, used to pre-fill its history**: 23 triplets for the 23 layers, one for
the head. A model loaded with it starts at rest, with no thump and no warm-up. Without it the init clears the rings
(`memset` of 72,000 and 30,156 bytes) and the network has to settle by itself, as the `AM` models do with their
1,102-sample warm-up (11.4).

The input of the network is also read: `x 2^20` to Q20, with saturation at `+-2^31`, and every clipped sample counted
(`0x1C2389C`).

### 12.2 The SysEx objects

**Read.** Only two objects are registered (`f_020226e4`, at boot): **4** and **5** (`[state + 459 x 4]` and
`[state + 460 x 4]`, i.e. `0x1C1B67C` and `0x1C1B680`). No instruction writes the other six slots, so commands `0x21`
to `0x23` on objects 0..3, 6 and 7 get the error reply. Each object is three handlers: `+0` for command `0x21`, `+4`
for `0x23` (read), `+8` for `0x22` (write).

**Object 5, a memory-mapped file service** (ops at `0x2054C8C`: `f_02006F16`, `f_02007400`, `f_02007534`). Reads and
writes take a 32-bit address; the top nibble picks the space.
- **`0x7xxxxxxx` = the `pd40j` file, by offset**: amp models `0x00000..0x27FFF` (8 KB slots), cabinets
  `0x28000..0x3BFFF` (4 KB slots). Reads and writes are queued to the main loop (`f_02013260`), which validates a
  model with `f_0200216e` before it is accepted.
- **`0xD0000000`** = a 32-byte descriptor of the map, `PMV1` (`0x20583E0`): version 1, then sizes `0x3000` (12 KB, the
  `usr` file), `0x2000` (an amp slot), `0x2000`, `0x1000` (a cabinet slot), and 7 (the `pd40j` nibble).
- **`0xE0000300`** = a 36-byte status block, `PDB1`: version 1, size 36, three state bytes, a counter, a time, a pointer.
- **`0xE00004xx`** = a 28-byte radio record (the advertising buffer of 10.6).
- **`0xE0000500..0xE00006FF`** = a 512-byte buffer.
- **`0xE0000700..0xE000071F`** = task statistics (`app_core`).
- Other spaces (`0x0..0x1`, `0x2`, `0x8`, `0xA0000000`, `0xF`) are handled in `f_02013260` and were not mapped one by
  one.

**Object 4** (ops at `0x2054C98`: no `0x21`, `f_02006E3E` for reads, `f_02006EAA` for writes) queues requests of types
4 and 5 to the same main-loop engine: a second stream (**likely** the live preset and settings).

### 12.3 Loopback

**Read up to the mixer.**
- The setting (`state + 1052`) is copied by the main loop (`f_02016150`) to byte 7 of a 10-byte routing struct at
  `0x1C0B510` (`0x1C0B4A0 + 112`), **inverted**: Loopback ON writes 0, OFF writes 1. Bytes 0 and 1 of that struct are
  127, two volumes.
- The audio interrupt reads the struct every block and passes byte 7 as the **mode** (0..3) to the output mixer
  `f_02029ede`. Modes 2 and 3 are set by other states (2 resets the USB rings, `f_020296bc`; 3 copies the block to a
  meter, `f_0202978c`).
- In modes 1 and 2 (Loopback OFF), with the computer's playback active, the processed block is sent to the USB record
  ring (`f_0202978c` through `f_020298ce`): **the computer records the pedal's sound**.
- In mode 0 (Loopback ON), the mixer first copies the output block (512 bytes) and then mixes the computer's playback
  into it (`f_01c071b0`).

So, **likely**: with Loopback ON the computer also records its own playback, mixed with the pedal (the usual meaning of
"loopback" on a streaming interface). The two USB rings were not followed to their DMA, so this is the reading of the
mixer's branches, not a measurement.

### 12.4 The battery

**Read.**
- **The measurement**: the SDK's `adc_scan` (`f_02020E96`, ADC at `0x13100`) samples a channel list every tick and,
  every 100 ticks, the reference and the battery. It keeps the last 20 battery samples (`state + 2020`);
  `adc_get_value(0x5000F)` returns their average (`f_0200B7D4`).
- **The percentage** (`f_0201ADB0`, the UI loop): the raw value of ADC channel 8, **0 % below 528, 100 % above 635,
  linear between**: `(raw x 100 - 52,700) / 109`, clamped to 0..100.
- **The icon** (`f_02010BB8`): green while charging, red below **21 %**, white otherwise. Charging is detected on GPIO
  151, read by `f_02016E76`.
- **At boot** (`f_02023A20`): the supply is measured in mV, `(1023 x trim + 818,400) / (average / 10)`. Below
  **3,400 mV** the firmware changes a power setting (`f_01c0128e(5, 0, 3, 7)`).
- **The hardware low-voltage reset** is `VLVD = 4` in the package's `isd_config.ini` (read from BlackBox_020's package).
- **The "Low Battery" label**: built by the UI (`state + 444`) and hidden right after it is built (`f_02004d58(obj, 1)`).
  No other instruction reaches that object: **in this build it is never shown** (**likely**: an object could still be
  reached through a path that was not found).

### 12.5 The `usr` file: a journal for model uploads

**Read.** `EXT_RESERVED/usr` (12 KB at `0xBA000`):
- **`0xBA000..0xBBFFF`** (8 KB): the staging buffer;
- **`0xBC000`**: a 20-byte journal record:

| Bytes | What |
| --- | --- |
| 0..3 | `P4JR` |
| 4, 5 | version 2 |
| 6 | type: 0 = an amp model (8 KB), 1 = a cabinet (4 KB) |
| 7 | the slot, 0..19 |
| 8..11 | the size (8,192 or 4,096) |
| 16..19 | CRC-32 (`0xEDB88320`) of bytes 0..15 |

At boot `f_02012874` (from `f_02012B2E`) checks the record and, if valid, **copies the staged model into its `pd40j`
slot**. So an upload survives a power cut: the model is written to `usr` first, the record last, and the copy into the
real slot is redone at the next boot until it completes (**likely** for the order of the writes; the boot-side commit
is read).

### 12.6 What is still not done

- The address spaces `0x0..0x2`, `0x8`, `0xA` and `0xF` of object 5, and the exact content of object 4's stream.
- Loopback's two USB rings down to their DMA (a measurement would settle it).
- The SDK's power service beyond the boot check (no other threshold was found in the application).

## 13. Eighth pass: the SysEx address map, the USB record path, the power module

### 13.1 The full SysEx address map

**Read** (the main-loop engine `f_02013260`; request types 2..5, then a jump table on the top nibble of the address).
Object 5 queues types 2 (read) and 3 (write); object 4 queues types 4 (read) and 5 (write).

**Object 5: storage.**

| Space | Read | Write |
| --- | --- | --- |
| `0x0` | the `usr` file, 12 KB | yes (the staging buffer and journal of 12.5) |
| `0x1` | the header of the **amp model now loaded** (60 bytes: name, blend, tone-stack bands...), through `f_0201150c` | yes, applied live (the slot's set routine is called) |
| `0x2` | the header of the **cabinet now loaded** (20 bytes) | yes, applied live |
| `0x7` | the `pd40j` file by offset (12.2) | yes, each model validated |
| `0x8` | the **names of the 20 cabinets**: 20 x 14 bytes, bytes 4..17 of each `CAB` file (`f_02009cfc`: `pd40j + 0x28000 + i x 4 KB`) | — |
| `0x9` | the **names of the 20 amp models**, 14 bytes each (`f_02002d18`) | — |
| `0xA` | — | a **one-byte command**: 1, 2 or 3 starts a commit from the staging buffer (1 = an 8 KB amp model) |
| `0xD` | the `PMV1` map descriptor | — |
| `0xE` | `0x300` `PDB1` status, `0x4xx` radio record, `0x500..0x6FF` buffer, `0x700` task statistics; `0x100`, `0x200` and `0x400` are other small blocks | — |
| `0xF` | one status byte (`0x1C0B4A4`) | up to 511 bytes: **install a model into a slot** (the address's low byte is the slot 0..19, bit 8 picks amp (8 KB) or cabinet (4 KB)); the model is validated (`f_0200216e`, types 2, 4 or 16 for amps) before it is written |

So every model in the stock firmware is a **14-byte-named file**: the name sits at bytes 4..17 of the header (`AM`,
`NAM` and `CAB` alike), right after the four-byte magic.

**Object 4: the live state.**

| Space | Read | Write |
| --- | --- | --- |
| `0x0` | the **whole preset bank**, 7,370 bytes (12.5 of `pd40j` at `0x3C000`) | yes |
| `0x1` | the **current preset**, 92 bytes (`state + 4076`, or the edited copy at `+ 4260`) | yes: the preset as edited |
| `0x2` | the **settings record**, 7 bytes (the `GLB1` record of 10.4, `state + 1046`) | yes |
| `0x7` | sub-spaces by bits 20..27: `7` = the tails byte and two values (`f_02012274`); `8` = a live block (input gain...) | yes |
| `0xD` | one byte, Loopback (`state + 1052`) and other flags | yes |
| `0xE`, `0xF` | — | other live controls (not followed one by one) |

That is the whole protocol of M-VAVE's app: **every preset, setting and model is read and written as memory**, through
two objects and an address space; there is no other command set.

### 13.2 The USB record path, and Loopback

**Read.** The USB Audio class's callbacks are a table at `0x205505C`. In it, `f_0202ad78` is the **record callback**:
- it reads the **1,024-frame record ring** at `0x1C18BC0` (stereo, packed 24-bit, 6 bytes a frame; write index
  `state + 118`, read index `state + 120`);
- it resamples by a fraction to follow the host's clock: below 256 frames of fill it slows down, above 768 it speeds
  up, by at most 48 steps at a time;
- `f_0202af5e` empties the ring.

The audio interrupt fills that ring through `f_0202978c`:
- with a volume from a 101-entry table (`0x204FF30`, index 0..100);
- overruns are counted at `state + 2092`.

The output mixer `f_02029ede` sends a block to the ring at **two possible points**:
- **before** the computer's playback is mixed into the pedal's output (`0x2029FC4`, modes 1 and 2);
- or **after** the mix, from a copy (`0x202A0DA`), chosen by the mode byte (Loopback) and two stream flags (bytes 8 and
  9 of the routing struct).

So Loopback chooses **whether the computer records the pedal alone or the pedal plus its own playback**. The mixer's
code decides it. Which flag combination gives which in practice was not settled from the code alone; recording one
second each way would settle it.

### 13.3 The power module

**Read.** The SDK's module table (`0x2080A10`) pairs each module with an "idle?" query used before sleep:
- `adc_scan` → `0x2020E0E` (is a conversion running);
- `sys_power` → `0x2030E7A` (one flag).

Neither holds a voltage threshold. With 12.4 that settles the battery: the only thresholds are the boot check (3.4 V)
and the icon's 21 %; the hardware LVD (`VLVD = 4`) is the only cut-off; the "Low Battery" label is never shown.

### 13.4 What is left

- Object 4's spaces `0xE` and `0xF` and object 5's `0xE0000100/200/400`, one by one.
- Loopback's two flag bytes in practice (a recording on the pedal settles it).
- Nothing else of the list in 9, 10.7, 11.9 and 12.6 remains open.
