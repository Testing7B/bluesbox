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
A2-Lite's ~1,730, and at 48 kHz rather than 44.1. Which exact network the 1,871 weights describe is not settled.

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
   semitones low (`docs/NAM.md`). The stock firmware runs its effects at 48 kHz. Moving OpenPocket to 48 kHz fixes the
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

| RAM slot | Blocks | What they are |
| --- | --- | --- |
| `0x1C23F80` | 6 | modulation (**likely**: chorus, flanger and the like: a modulated line read with a cubic, a sine table) |
| `0x1C24540` | 6 | **likely** the reverbs: 16-bit lines, 10 reads and 1 write a sample, decay times worked out with `1/ln 2` |
| `0x1C24E20` | 6 | **likely** the delays and the pitch effects |
| `0x1C25200` | 2 | the amp (calls the 6x oversampling routines of section 4) |

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

1. **Take the delay and the reverb out of the critical path** (the largest gain for crackles). Today they run inside the
   block that must finish before the next interrupt, in series. As in the stock firmware: send a mono copy at their slot,
   return their wet output one block later, and let the dry path finish without them. A late wet block then costs an
   echo's 2.9 ms, not a crackle. With a NAM amp, CPU1 already runs everything after the amp; the wet part could go to
   whichever core has time left at the end of the block.
2. **Time the interrupt the stock way:** sum and maximum over 512 blocks from a free-running timer, plus the wet task's
   own times per lane. v38's footer shows the averages; the maximum is what predicts a crackle.
3. **Ramp the volume over the block** (v38 glides it already) and **crossfade on/off over ~7 ms** (v38: 23 ms).
4. **One `tanh` table for every soft clip**, as above, instead of polynomial approximations per sample.

## 8. Not done yet

- The ALNK clock registers were not traced to a written rate; 48 kHz rests on the effects' coefficient code.
- The exact network behind the stock `NAM` type and the role of overlay B (possibly the SBC decoder for A2DP).
- The 20 effect blocks after the overlays are mapped by address but not each named.
- Which core `wet_audio` runs on (the task call does not pin it); the meaning of the order bytes (slot to category or
  the reverse: the default is the identity, so it does not show).
