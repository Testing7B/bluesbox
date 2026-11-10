# BluesBox development notes

BluesBox starts with three guitar tone references: Stevie Ray Vaughan's Fender breakup, John Mayer's clean-to-edge tones, and Albert King's bright, direct sound. The presets use shared amp and cab voices with individual gain, drive, and space settings. The main signal order is gate → drive → amp → cab → compressor → modulation → delay → reverb. Placing the compressor after the cab lets the guitar volume change the breakup before compression controls the output.

## Released experiment

- **V43:** 20 SRV, 10 Mayer, and 10 Albert King presets; three V40 references; and nine comparison presets. The new drives are **808 Circuit**, an independent feedback-diode approximation, and **Clean Boost**. V43 also restores wet gain for the 27 legacy drive recipes and adds **OPTO T4**, whose recovery changes with the duration of the input.
- **V44:** same preset bank, with drive oversampling increased from 2× to 4× ahead of the 44.1 kHz NAM amp. Across 16 synthetic stress cases, the 4× build reduced nonharmonic energy at the drive output by roughly 8–34 dB. This measures aliasing under stress; the audible benefit and real-time DSP cost on the pedal remain open.
- Both builds passed the host engine sweep, the simulated BlackBox update, and package checks. V43 was installed on a physical BlackBox, which rebooted as `BlackBox_943`. The editor protocol read the 52-slot bank and verified the new effect types. V44 has passed the physical pedal's read-only package check. [Source and packages](https://github.com/Testing7B/openpocket/releases/tag/bluesbox-v43) · [Preset catalog](https://github.com/Testing7B/openpocket/blob/experiment/blues-lab/docs/BLUES-LAB-PRESETS.md) · [Build run](https://github.com/Testing7B/openpocket/actions/runs/42793280441)

The hosted editor supports TONE3000 sign-in and selection of A2 amp and pedal captures. The deployed popup and callback page were checked through the sign-in screen. Completing the signed-in selection and model load remains a separate test. Factory presets use the built-in models. User saves take precedence over factory slots.

## Candidates for the next revision

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
