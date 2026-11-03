# Audio through the official JieLi SDK (AC79 / WL82) — development notes

Source: JieLi's official SDK `fw-AC79_AIoT_SDK` V1.2.0 (Gitee: gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK).
Apache 2.0 licence → reusable in OpenPocket (compatible with GPL-3.0 in that direction).

## What the SDK gives us
- `include_lib/driver/cpu/wl82/asm/iis.h` — I2S (ALNK) API: `iis_open`, `iis_set_sample_rate`,
  `iis_set_enc_data_handler` (input), `iis_set_dec_data_handler` (output), `iis_irq_handler`.
- `include_lib/driver/cpu/wl82/asm/WL82.h` — the official register map (ALNK0 0x12E00, ALNK1 0x15100,
  IOMAP 0x51000+, CLOCK 0x10000, ANA 0x11900).
- `cpu/wl82/liba/cpu.a` → `iis.c.o` = **LLVM bitcode** produced by clang 4.0.1 (= our toolchain).
  Compiles as is: `clang -target pi32v2 -Os -c -x ir iis.bc -o iis.o` (~5 KB).
  Dependencies we provide: malloc/free, memset, gpio_direction_input/output, gpio_set_die, gpio_set_pull_up/down,
  log_print/printf (no-op), log_tag_const_{d,e,i}_AUDIO_IIS (u8 = 0), os_current_task_rom, cpu_assert_debug,
  config_asser, P33_SYSTEM_RESET.
  The driver does not register its IRQ: we call `iis_irq_handler(0)` from the ALNK0 ISR.
- The driver sets the audio clock family (44.1k / 48k) itself through CLK_CON2 (0x10014) and ANA.

## I2S pins (AC791x datasheets in the SDK)
| Group | MCK | SCLK | LRCK | DAT0 | DAT1 | DAT2 | DAT3 |
|---|---|---|---|---|---|---|---|
| IIS_PORTA (B1) | PA4 | PA5 | PA6 | PA7 | PA8 | PA9 | PA10 |
| IIS_PORTC (A)  | PC0 | PC1 | PC2 | PC3 | PC4 | PC5 | PC6 |

Pocket X: PC0/PC1 = encoder, PC7 = screen, PA0–PA3 = buttons, PA10 = MASTER knob (analog)
→ **IIS_PORTA, data on PA7/PA8/PA9** (DAT3/PA10 not usable).

## Finding the audio path on the pedal
- **v10 (2026-11-01)**: `I2S OK`. **Guitar input = I2S ALNK0, DAT1 = PA8** (IIS_PORTA, master, MCLK on PA4,
  44.1 kHz). DAT0 (PA7) and DAT2 (PA9) idle. No sound out on DAT0/DAT2.
- Output hypothesis: **internal DAC (DACL pin)** → **SGM8905** line amp (which has a shutdown pin), like JieLi's
  QFN48 reference board (`board_7911B.c`: DACL, hw_channel 0x01, amp mute).
- **v11**: internal DAC through `sdk/dac.bc` (IRQ_AUDIO = 13) + a search for the amp enable pin (weak pull-up /
  pull-down on 23 candidate pins, one at a time).
- **v11** on the pedal: `I2S OK`, `DAC OK`, but no sound, and **the button LEDs stay off** under OpenPocket → nobody
  drives the **74HC595**. Hypothesis: it drives the LEDs **and** the SGM8905 enable.
- **v12**: automatic search for the 595's pins (DS, SHCP+STCP, OE), weak pulls only; witness = LEDs blinking.
  The Pocket X has no knob LEDs (button LEDs only); the 595 search was judged too indirect.
- **v13**: direct sweep, each candidate pin driven 3.3 V then 0 V (1.5 s each), one at a time, with a continuous
  beep on the internal DAC: listen for when the sound comes out.
- **v13** on the pedal: **a click in the output when PA9 changes state** (first sign of life of the output).
- **v14**: PA9 (AMP) and PA7 (MOD) set by hand: free / 3.3 V / 0 V / I2S data; simultaneous beep on the internal
  DAC and on I2S.

## ✅ Audio path complete (v14, 2026-11-01)
| Role | Pin | Detail |
|---|---|---|
| MCLK | PA4 | output (mclk_output = 1) |
| SCLK / LRCK | PA5 / PA6 | JieLi is master |
| **Guitar input** | **PA8 = DAT1** | I2S ALNK0, 24-bit stereo (16-bit up to v25), 44.1 kHz (external ADC); guitar on the **left** channel only |
| **Output** | **PA7 = DAT0** | I2S ALNK0 → external DAC "344C" → SGM8905 amp → OUT + headphones |
| **Amp enable** | **PA9 = 3.3 V** | GPIO output high (the click = the amp powering up) |

The internal DAC (DACL) is NOT used by the Pocket X. Guitar → output passthrough: clean.
Driver setup: `iis_open` IIS_PORTA, channel_in = BIT(1), channel_out = BIT(0), master, MCLK, then
`iis_set_sample_rate(44100)`, `iis_channel_on`, ALNK0 IRQ = 11 → `iis_irq_handler(0)`.

## v15: the DSP engine in the audio path
- `firmware/px_audio.h`: I2S (in PA8 / out PA7), amp PA9, engine `engine/px_dsp.c` in the ALNK0 IRQ (blocks of 128
  frames), guard: > 85 % load for 8 blocks in a row → BYPASS. Load shown as "CPU xx%".
- Float on the hardware FPU: `-mcpu=r3 -mfprev1` (the JieLi SDK's options for the WL82). Without them clang emits
  software calls (`__addsf3`...). `firmware/px_math.c` = a small libm (sin/cos/exp/log/pow/sqrt, error ~1e-6).
- Engine (~250 KB) in the RAM area `POOL` 0x01C20000 (0x54000 bytes), like CLIPPER. PX_OVERSAMPLE = 2 to begin with.
- **v15** on the pedal: **BYPASS** (engine > 85 % of real time).
- **v16**: engine optimised without changing the sound (−120 dB difference checked on the PC, 8 presets): cabinet as
  a linear dot product (doubled history, unrolled ×4), no more modulo in delay/reverb, anti-denormal ±1e-15, plus a
  measurement screen at boot (INT/FLOAT speed, % of real time per engine configuration).
- **v16** on the pedal: **CPU 52 %**, real time OK. But the sound was too bright, lacking lows.
- **v17** (sound): measured on the PC (plucked string, octave bands) → the 100–200 Hz band at −20 dB on 5150/Djent.
  Causes: (1) cumulated low cuts before the distortion (200 Hz tightness + 90/110/130 Hz stages); (2) cabinet = the
  response of 6 filters TRUNCATED at 256 taps (5.8 ms) → the ~100 Hz low bump disappeared.
  Fixes: cabinet run directly as 6 biquads (exact response, ~8× less work), lower stage low cuts and tightness
  (5150: 35/50/70 Hz, tightness 100 Hz), 5150/Uber low shelf raised to 170–180 Hz, resonance ×10 dB, presence
  ×6 dB, cabinet presence peaks reduced. Result: +10 dB at 100–200 Hz. Amp oversampling back to ×4 (CPU freed by
  the cabinet).

## v18–v20: hardware SPI screen + DSP on the second core
- **Screen**: hardware SPI1, group A (PB6 = CLK, PB7 = DO, IOMAP_CON1 bit 4 = 0, from the SDK's `spi.h`), registers
  and CON = 0x4021 taken from `fm1_lcd_hw.h` (Felucca), 20 MHz clock (BAUD 2), pixels sent by DMA with a double
  buffer of 1024 pixels. `LCD_HW 0` in openpocket.c = back to software SPI.
- **Second core**: Felucca's `fm1_cpu1.h`, started at the beginning of app_main. Block N goes to CPU1 at the end of
  IRQ N, IRQ N+1 collects it (same latency). `FM1_CPU1_FALLBACK 1`: if it fails, CPU0 takes over alone.
  Bottom right: `C1 xx%` (second core) or `C0 xx%` (fallback). `DSP_CPU1 0` = everything on CPU0.
- The SDK's CORDIC (`tanh_float`) was studied: a `copex` instruction + a division → no faster than our rational tanh.
- **v17 on the pedal: NO SOUND.** Cause found by simulating on the PC with OUR libm (px_math.c): `cosf` = sin(x+pi/2)
  with ~4e-6 error; the lowered low cuts (30–35 Hz) computed at 176.4 kHz (×4) need cos(w) with w ~ 1e-3: unstable
  biquads → NaN → permanent silence (no BYPASS: the computing time stays normal). Invisible on the PC with the
  standard libm.
- **v19**: sin/cos rewritten (reduction to [−pi/4, pi/4], pi/2 in two parts, separate polynomials); 4,860
  amp × cabinet × extreme-setting combinations tested with px_math.c: 0 NaN. Safety net: NaN/Inf detected at the
  output → silence + engine reset by the main loop. LESSON: always simulate with px_math.c, not the system libm.
- **v20** = v19 + hardware SPI screen (LCD_HW 1) + DSP on the second core (DSP_CPU1 1). Fallback switches in
  openpocket.c: `LCD_HW 0` (software screen), `DSP_CPU1 0` (everything on CPU0); build.sh only assembles fm1_cpu1.S
  when DSP_CPU1 is 1.

## v21–v23: content
- **v21**: engine v2 (10 blocks, 13 amps), "chain + block page" UI without flicker. **v22**: Inter font, no more
  measurement screen at boot.
- **v23**: engine v3, 11 blocks (PITCH and SPECIAL FX added), 118 models voiced from circuit analyses; English UI;
  readable values (`+5TH`, ms, dB). Fixes found on the PC: denormal numbers in digital silence (the amp stages' bias
  swallowed the anti-denormal offset), phase-aligned pitch shifters (SOLA), normalised saturating delays
  (self-oscillation only above FDBK ≈ 90). Benchmark (`BENCH_AT_BOOT 1`) updated: GROOVE METAL preset (5150 III,
  4 stages) and the heaviest blocks.

## v24: USB update
- USB-MIDI (felucca-bsp `chip/usb.c`, polled by TIMER5 at 2 kHz) and the M-UPGRADE update entry (`chip/ota.c`): the
  package's loader is staged at 0xE0000 (5 × 4 KiB), then the pedal restarts into it.
- Loader (`loader/`): felucca-bsp adapted to the Pocket X: app area [0x4000, 0xA3000) like PocketX_016, flash
  header never written, update records erased between 0xA3000 and 0xFF000.
- Packages (`tools/pxpkg.py`): rebuild the official PocketX_016 app area byte for byte.

## v25: presets in flash, finer cabinets and amps
- Presets (`firmware/px_store.h`): 42 slots of 96 bytes, two A/B copies (0xF0000 / 0xF2000, two 4 KiB sectors
  each), "OPPS" header written last (sequence + CRC16), read back after writing. Bytes 0xF00..0xF7F of each sector
  always left erased (the loader reads that area for its records). While writing: output muted, ALNK0 IRQ masked,
  second core between two blocks (its loop runs from RAM).
- Cabinet: 128-tap FIR after the biquads (cone resonances 1.2–6 kHz drawn per model, off-axis mic comb, rear wave of
  open-back cabinets), normalised to unity at low frequencies. (Replaced in v27, see below.)
- Amp: asymmetric triode `u≥0 ? 0.7·tanh(u/0.7) : tanh(u)`, bias shift driven by an envelope (100 ms, 0.45 for
  ≥4 stages, 0.35 otherwise), sag on the preamp too (×0.3). `px_trim.h` recalibrated (29 amps at 0.160 RMS).
- Memory: 327,724 of 344,064 bytes in POOL.

## v26: 24-bit input and output
- Symptom: constant hiss ("shhh") on high-gain amps, even with no cable plugged in, absent with the official
  firmware. First hypothesis: the 16-bit input (quantization noise × the amp's ~60 dB of gain). It did not fix the
  hiss (the real cause was found in v28), but 24 bits is what the official firmware uses and is kept.
- Official firmware (PocketX_016, disassembled): same ALNK0, same pins, master, 64 SCLK per frame, but
  - every channel is set to **24 bits** (function 0x0200664A: `CON1 |= 1 << (4*ch + 2)`, the SDK's `data_width` bit);
  - LEN = 128 and a 512-byte half-buffer processed as 64 frames of 8 bytes: in 24-bit mode LEN counts 32-bit words;
  - input read as `(word << 8) >> 7`: 24 bits right-justified (bit 23 = sign), **left channel only**, ×2;
  - output written as 24-bit integers clipped at ±8,388,600 into both words of the frame.
- OpenPocket v26: `data_width = BIT(0) | BIT(1)`, `sr_points = 256` (still 128 frames per block), 4,096-byte heap for
  the driver's buffers (2 channels × 8 × 256). Output in 24 bits (the MASTER volume no longer costs resolution).
- Diagnostic: the tuner screen shows `IN L -xx R -xx dB`, the level of each input channel (dBFS).

## v27: CPU load (DOOM SLUDGE in BYP at 89 % on v26)
- v25's 128-tap cabinet FIR was expensive on the pedal's FPU (128 multiplications per sample). Replaced by the same
  response computed another way: a few sparse taps (direct sound, mic comb, rear wave) + the cone's resonances as
  2-pole resonators (h[k] = A·dec^k·sin(kw), exactly what the FIR summed), the 6 strongest of the 10 draws:
  0.5 dB RMS from v26, 1.9 dB at worst on the small peaks.
- Amp: the triode curve folded into a single rational function (identical to 2e-7), the bias envelope pre-scaled
  by its depth, and the two supply-sag divisions computed once per 64-sample block instead of every sample.
- Instructions per sample (PC, no vectorisation): DOOM SLUDGE v24 1,936 / v26 2,655 / v27 2,240;
  GROOVE METAL 1,430 / 2,151 / 1,744.
- Engine: 326,612 bytes in POOL.

## v28: the guitar from the left channel only (the hiss came from the right channel)
- Measured on the pedal (tuner screen, v26): nothing plugged in, L −76 to −91 dBFS, R −77 dBFS steady; strumming
  hard, L −17 dBFS, R still −77 dBFS. The guitar only reaches the left channel; the right one only carries a
  constant noise, 14 dB above the left channel's floor.
- The (L + R) / 2 mix put that noise into the sound: signal-to-noise 60 dB (−17 vs −77). Left channel only, like the
  official firmware: 74 dB (−17 vs −91), up to 14 dB less hiss, amplified just as much by the high-gain amps.
  Input = L × 0.5: same level as before, settings unchanged. Confirmed on the pedal: the hiss is gone.
