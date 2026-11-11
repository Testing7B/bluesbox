# BluesBox

BluesBox is experimental guitar firmware for the **M-VAVE BlackBox**, forked from OpenPocket V40. It focuses on clean, edge-of-breakup and overdriven blues sounds inspired by blues guitarists including Stevie Ray Vaughan, John Mayer, Freddie King and B.B. King. The artist presets put compression after the cab, so your guitar's volume still controls how hard you drive the amp.

**[Web updater](https://testing7b.github.io/bluesbox/)** · **[Web editor](https://testing7b.github.io/bluesbox/editor.html)** · **[V52 preset guide](docs/BluesBox-V52-Preset-Guide.md)** · **[Development notes](docs/BLUESBOX.md)**

## What changed since V40

| Version | Changes |
| --- | --- |
| V42–V43 | Guitar-focused chain with the synth slot removed, 40 artist-inspired tones, three V40 reference sounds, and drive and opto comparisons. Added an 808 circuit model, clean boost, and a T4-inspired opto compressor. |
| V44–V46 | 4× drive oversampling, tighter drive-level matching, the local Two-Rock TS1 and TS808 Japan models, and stereo USB playback at 44.1 kHz. |
| V47 | Blues-focused amp and cab menus, offline fits of six amp voices and four matching cabinets for the local build, five editable USER slots, and verified recovery of stock V21 factory sounds. |
| V48 | Larger cabinet label, encoder preset browsing, separate guitar and USB levels, input trim and high-pass filter, two assignable footswitch groups, dual-switch tuner, and a level and voicing pass across the factory bank. USB playback uses a buffered 2 kHz poll which yields to the guitar path while playback is idle. |
| V49 | Revised factory bank, preset browser that stays open after loading, hold HOME for Settings, global output EQ, and a redesigned web editor with pedal settings and footswitch assignments. |
| V50 | Fixes the repeating click heard during USB playback. Shows Preset or Stomp mode on PLAY, makes delays quieter and shorter in the song bank, and simplifies the editor’s preset filters. |
| V51 | Dimension defaults to mode 3 and mix 25. Discrete controls change one setting per encoder detent. Drive and Delay default to footswitch groups A and B. The gate adds hysteresis and Patch Output moves to Settings. Love Struck is quieter, and Slow Dance, Life Lead and Mary Lead have less midrange. |
| V52 | Local audition build with 45 blues presets, a guitar input meter, LA-2A Plus alongside the earlier compressors, and separate A2 Lite amp and cabinet captures. |

## Where to start

The [V52 preset guide](docs/BluesBox-V52-Preset-Guide.md) lists every slot and the gear and songs that informed it. Start with **P01 Pride & Joy**, **P08 JM Round**, **P15 FK Hideaway**, or **P22 BB Lucille**. Use **P46–P47** to compare the two opto sounds at matched levels.

On the pedal, turn to the preset number and press the encoder to browse. Press to load and stay in the list. Tap **HOME** to return to PLAY, hold **HOME** for Settings, or hold both footswitches for the tuner. The PLAY footer shows **PRESET UP/DN** or **STOMP A/B**. In the editor, open **MIDI → Pedal settings** for input gain, footswitch mode and output EQ. Use **Sound** to assign blocks to A or B and save the preset.

Set **Patch Output** under **Settings** on the pedal or above the block editor on the **Sound** page. Save the preset to keep the level change. **Gate → Hysteresis** sets how far the signal must fall below the opening threshold before the gate starts to close. The factory default is **6 dB**.

The hosted updater offers V51 with public amp and cabinet voices. For the V52 audition build with locally sourced captures, choose **Install a local build** and supply its firmware file and matching manifest. Chrome or Edge on a desktop is required for Web MIDI. The editor supports local NAM A2 Lite imports and TONE3000 browsing. Existing saved presets take precedence over factory sounds. Export them before resetting individual slots.

For a stock firmware return, use the [V21 factory sound restore guide](docs/RESCUE.md#blackbox-v21-factory-sounds). BluesBox source lives on [experiment/blues-lab](https://github.com/Testing7B/bluesbox/tree/experiment/blues-lab). The V21 restore improvement was also [proposed upstream](https://github.com/isod89/openpocket/pull/26).

## Credits

BluesBox builds on OpenPocket by isod89 and contributors, including its DSP engine, editor and update tools. The V49 editor adapts [OpenPocket’s V42 web editor by VinSpee and contributors](https://github.com/isod89/openpocket/pull/23). The board support and loader come from [Leo Kuroshita, Hügelton Instruments and the Felucca contributors](https://github.com/hugelton/felucca-bsp). Thanks to [Steven Atkinson and Neural Amp Modeler](https://github.com/sdatkinson/neural-amp-modeler), the capture authors, and [Guitar-TECHS](assets/loops/ATTRIBUTION.md) for the reference DI material. See the [component notices](NOTICE.md) and [GPL-3.0-only licence](LICENSE).
