# BluesBox development notes


## V49

V49 bakes the 52-slot audition bank into the factory presets. Slots 40–47 are the four song pairs, and slots 48–52 are the five USER templates. Saved overrides still take priority. The fitted build retains the auditioned amp/cab types and the public build uses built-in substitutes. The SRV presets use Plate and favour Dimension for chorus, with signature rotary and vibe exceptions. The revised song presets include the timed repeat and voicing adjustments from the loopback sessions.

Hold HOME for Settings. In the preset list, press the encoder to load and keep browsing, then tap HOME to return to PLAY. The global low, mid and high output EQ acts on both guitar and USB playback. The editor adapts the [OpenPocket V42 web editor](https://github.com/isod89/openpocket/pull/23) and adds BluesBox pedal settings, per-preset stomp assignment, and the existing audio counters.
BluesBox starts with three guitar tone references: Stevie Ray Vaughan's Fender breakup, John Mayer's clean-to-edge tones, and Albert King's bright, direct sound. The presets use shared amp and cab voices with individual gain, drive, and space settings. The main signal order is gate → drive → amp → cab → compressor → modulation → delay → reverb. Placing the compressor after the cab lets the guitar volume change the breakup before compression controls the output.

## Released experiment

- **V43:** 20 SRV, 10 Mayer, and 10 Albert King presets; three V40 references; and nine comparison presets. The new drives are **808 Circuit**, an independent feedback-diode approximation, and **Clean Boost**. V43 also restores wet gain for the 27 legacy drive recipes and adds **OPTO T4**, whose recovery changes with the duration of the input.
- **V44:** same preset bank, with drive oversampling increased from 2× to 4× ahead of the 44.1 kHz NAM amp. Across 16 synthetic stress cases, the 4× build reduced nonharmonic energy at the drive output by roughly 8–34 dB. This measures aliasing under stress; the audible benefit and real-time DSP cost on the pedal remain open.
- Both builds passed the host engine sweep, the simulated BlackBox update, and package checks. V43 was installed on a physical BlackBox, which rebooted as `BlackBox_943`. The editor protocol read the 52-slot bank and verified the new effect types. V44 has passed the physical pedal's read-only package check. [Source and packages](https://github.com/Testing7B/bluesbox/releases/tag/bluesbox-v43) · [Preset catalog](https://github.com/Testing7B/bluesbox/blob/experiment/blues-lab/docs/BLUES-LAB-PRESETS.md) · [Build run](https://github.com/Testing7B/bluesbox/actions/runs/42793280441)

The hosted editor supports TONE3000 sign-in and selection of A2 amp and pedal captures. The deployed popup and callback page were checked through the sign-in screen. Completing the signed-in selection and model load remains a separate test. Factory presets use the built-in models. User saves take precedence over factory slots.

## Private BlackBox builds

- **V45** put the 44.1 kHz **TWO-ROCK TS1** capture in its own AMP type. Factory P21 Gravity uses it with OPTO T4. P22 Slow Dance adds the **TS808 JAPAN** drive. V45 was verified on a BlackBox as `BlackBox_945`.
- **V46** adds stereo 44.1 kHz USB playback mixed after the guitar effects, defaults distortion LEVEL to 15 and adjustable compressor MIX to 100, and orders block navigation from on/off through model and parameters. Drive output trims are calibrated against TS9 using lead and chord DI excerpts. TS808 Japan also has 6 dB less internal input gain than in V45. The V46 update passed in the simulator and the connected V45 pedal passed a read-only package check. USB playback and DSP load with a running stream still need a hardware test.

The V45 and V46 builds require the local Two-Rock capture header. On a machine with permission to use the converted 44.1 kHz A2 Lite capture, generate the header with `python3 tools/mk_blues_ts1.py /path/to/capture.nam`, then build with `OP_CHANNEL=blues-v46 node tools/op.mjs build`. The private capture, weights and packages stay local. For installation use **Install a local build** in the [fork updater](https://testing7b.github.io/bluesbox/) and choose the BlackBox package and its matching manifest.

## V47

V47 polls USB directly at 2 kHz, increases the USB playback buffer target to about 17 ms, and mixes stereo USB audio with fixed-point interpolation. The USB device appears as **Blackbox**. The play screen uses the cab tile's colour for the active cab name and shows **USB PLAY** when a stream is active.

The local build adds six fitted circuit amp models and four fitted cabinet responses. The A2 Full models and original IRs are reference inputs for the offline fit. Neither is needed by the pedal at runtime. Two-Rock TS1 uses the private A2 Lite capture. Slots 48–52 default to USER1–USER5. The hosted V47 build uses public voices and carries the same USB and preset editing improvements.

## Capture research

| Candidate | Result | Next decision |
| --- | --- | --- |
| **Two-Rock TS-1 Edge** | Converted from 48 kHz to 44.1 kHz A2 Lite. On held-out arpeggios at three input levels, error-to-signal ratio fell from 0.0138–0.0141 with direct import to 0.0011–0.0015 after conversion. It passed the fixed-point host render and simulated editor import. | Audition in a copy of P21 Gravity. |
| **1964 Super Reverb / Vibroverb** | The Super Reverb conversion improved two of three held-out levels and regressed on the hardest input. The Vibroverb was auditioned as an amp and cab capture. | Keep both as SRV listening references while the Super Reverb conversion is refined. |
| **TS808 Japan reissue** | A conventional drive fit trained against one local A2 Lite capture reduced spectral error versus the legacy TS808 recipe at three held-out levels (2.78–4.50 dB before, 1.59–2.71 dB after). | Compare with V43's 808 Circuit. A conventional drive can run alongside the single live A2 Lite amp. |
| **Tokyo Studio LA-2A Classic Medium** | In a 48 kHz desktop test, each A2 variant produced identical quiet-probe output by 100 ms after short versus sustained loud input. Both variants were auditioned for tone and immediate response. | Keep OPTO T4 for the longer release. Use the capture as a tone reference at its fixed setting. |

The listening shortlist came from 316 organized local A2 files plus 41 additional files in the download folder. The fitters now accept A1, A2 Lite, or A2 Full as offline references. These candidate measurements are from host renders and simulated import. Private captures and derived models are kept out of the public repository and the release packages.

## References and credit

- OpenPocket by isod89 and contributors. See the [capture fitting notes](FITTED-MODELS.md) and [NAM engine documentation](NAM.md).
- [Yeh, Abel and Smith's diode clipper analysis](https://dafx.labri.fr/main/papers/p189.pdf) informed 808 Circuit. [Yu et al.'s DAFx compressor study](https://www.dafx.de/paper-archive/2024/papers/DAFx24_paper_75.pdf) and [SignalTrain LA-2A recordings](https://zenodo.org/records/3824876) provided a desktop comparator.
- [Tokyo Studio Capture](https://www.tone3000.com/tones/tokyo-studio-capture-teletronix-la-2a-tube-compressor-69103) made the tested LA-2A capture. Thanks to the local amp and pedal capture creators and the [Guitar-TECHS DI contributors](../assets/loops/ATTRIBUTION.md).
- Full component origins and licence details are in [NOTICE.md](../NOTICE.md).

## V48 controls and signal path

- The cabinet label uses the same font size as the amp label. On Play, the encoder reaches the preset number at either end of the processor chain. Push to browse, turn to preview names, and push to load. HOME cancels and returns to the last processor. A second push confirms discarding edits when changing presets.
- Hold FX to open Settings. Input gain is digital trim from −12 to +12 dB. It and the optional 40–120 Hz high-pass filter sit before the gate and drive. Guitar Level and USB Level attenuate their own paths before mixing. The physical master still controls both. The existing preset EQ can shape the guitar without changing backing-track tone.
- In Stomp mode, left toggles group A and right toggles group B. Each processor can belong to one group. A group turns all its assigned blocks off if they were all on, or turns them all on otherwise. The assignments are saved with the preset. Hold both switches for about 0.7 seconds for the tuner. Tuner Mute offers Guitar, All or Off.
- The guitar IRQ has priority over USB MIDI and idle USB polling. A live USB playback stream raises the priority of its 2 kHz poll so the 1 ms packets arrive in time. The firmware reports guitar deadlines, wet-lane cuts, ADC clip blocks and USB underruns to the editor protocol.


V48's final per-preset trims were re-rendered against six DI performances
at nominal input trim. The fitted build spans −27.5 to −25.9 LUFS and
the public build −31.9 to −29.4 LUFS when averaging those clips per
preset. The fitted Super, Deluxe, Bassman and Twin Edge combinations
received presence and cab adjustments before this pass. The first 47
slots retain their roles and slots 48–52 are editable starters. Saved
sounds keep their own trims.

For a guitar-only click, route computer sound elsewhere first. In the web
editor, open **Audio diagnostics** and click **Start**, then play until you
hear it and choose **Read counters**. Copy the preset number and counter
readout so a click can be checked against DSP deadlines, clipping and USB
activity for that interval.
