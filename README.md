# BluesBox

BluesBox is experimental guitar firmware for the **M-VAVE BlackBox**, forked from OpenPocket V40. It focuses on clean, edge-of-breakup and overdriven blues sounds inspired by Stevie Ray Vaughan, John Mayer and Albert King. The artist presets put compression after the cab, so your guitar's volume still controls how hard you drive the amp.

**[V48 web updater](https://testing7b.github.io/bluesbox/?channel=blues-v48-public)** · **[Web editor](https://testing7b.github.io/bluesbox/editor.html)** · **[Preset guide (PDF)](https://testing7b.github.io/bluesbox/BluesBox-V48-Preset-Guide.pdf)** · **[Development notes](docs/BLUESBOX.md)**

## What changed since V40

| Version | Changes |
| --- | --- |
| V42–V43 | Guitar-focused chain with the synth slot removed, 40 artist-inspired tones, three V40 reference sounds, and drive and opto comparisons. Added an 808 circuit model, clean boost, and a T4-inspired opto compressor. |
| V44–V46 | 4× drive oversampling, tighter drive-level matching, the local Two-Rock TS1 and TS808 Japan models, and stereo USB playback at 44.1 kHz. |
| V47 | Blues-focused amp and cab menus, offline fits of six amp voices and four matching cabinets for the local build, five editable USER slots, and verified recovery of stock V21 factory sounds. |
| V48 | Larger cabinet label, encoder preset browsing, separate guitar and USB levels, input trim and high-pass filter, two assignable footswitch groups, dual-switch tuner, and a level and voicing pass across the factory bank. USB playback uses a buffered 2 kHz poll which yields to the guitar path while playback is idle. |

## Where to start

- **P01–P20** cover SRV-style Fender cleans and breakup, Tube Screamer leads, rotary, and fuzz/vibe. Start with **P02 Pride & Joy**.
- **P21–P30** cover Mayer-style clean headroom, mild drives, touch filtering, and ambience. Try **P21 Gravity**, **P22 Slow Dance**, and **P25 Dont Trust**.
- **P31–P40** cover Albert King-style direct attack, sparse ambience and sustain. Start with **P31 Bad Sign** or **P32 Crosscut**.
- **P41–P43** retain three V40 reference rigs with V48 level trims. **P44–P47** compare optos and drives. **P48–P52** are USER1–USER5.

On the pedal, turn the encoder to the highlighted preset number and press to browse. **HOME** cancels. Hold **FX** for Settings. Select **Stomp** to use the left and right footswitches for groups A and B. Hold both footswitches to open the tuner. Assign groups on the pedal or in the editor and save them with the preset.

The hosted firmware uses public amp and cab voices. The local build uses the fitted models and the Two-Rock capture. Choose **Install a local build** to use its firmware file and matching manifest. Chrome or Edge on a desktop is required for Web MIDI. The editor supports local NAM A2 Lite imports and TONE3000 browsing. Existing saved presets take precedence over factory sounds. Export them before resetting individual slots.

For a stock firmware return, use the [V21 factory sound restore guide](docs/RESCUE.md#blackbox-v21-factory-sounds). BluesBox source lives on [experiment/blues-lab](https://github.com/Testing7B/bluesbox/tree/experiment/blues-lab). The V21 restore improvement was also [proposed upstream](https://github.com/isod89/openpocket/pull/26).

## Credits

BluesBox builds on OpenPocket by isod89 and contributors, including its DSP engine, editor and update tools. The board support and loader come from [Leo Kuroshita, Hügelton Instruments and the Felucca contributors](https://github.com/hugelton/felucca-bsp). Thanks to [Steven Atkinson and Neural Amp Modeler](https://github.com/sdatkinson/neural-amp-modeler), the capture authors, and [Guitar-TECHS](assets/loops/ATTRIBUTION.md) for the reference DI material. See the [component notices](NOTICE.md) and [GPL-3.0-only licence](LICENSE).
