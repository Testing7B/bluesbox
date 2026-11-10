# BluesBox

BluesBox is an experimental guitar firmware for the **M-VAVE BlackBox**, forked from OpenPocket V40. The goal is a small set of responsive blues sounds inspired by Stevie Ray Vaughan, John Mayer, and Albert King. It pairs classic drives and amp voices with adjustable effects, level-matched listening comparisons, and an opto compressor after the cab so the guitar's volume still controls breakup.

**[BluesBox V43 downloads](https://github.com/Testing7B/openpocket/releases/tag/bluesbox-v43)** · **[Web updater](https://testing7b.github.io/openpocket/)** · **[Web editor](https://testing7b.github.io/openpocket/editor.html)** · **[Development notes](docs/BLUESBOX.md)** · **[Preset list](https://github.com/Testing7B/openpocket/blob/experiment/blues-lab/docs/BLUES-LAB-PRESETS.md)**

| Build | Use |
| --- | --- |
| **BluesBox V43** | BlackBox prerelease with 40 song-inspired presets, three V40 reference presets, and nine drive and opto comparison slots. Verified to install and report its preset bank on a physical BlackBox. |
| **BluesBox V44** | Same bank with 4× drive oversampling ahead of a NAM amp. Host-tested comparison build. Hardware timing is pending. |
| **OpenPocket V40** | [Browser installer](https://testing7b.github.io/openpocket/) for Donner Pocket X and BlackBox. |
| **Drive Lab V42** | [Earlier BlackBox experiment](https://testing7b.github.io/openpocket/?channel=drive-lab) based on V40. |

Start with **P02 Pride & Joy**, **P21 Gravity**, and **P31 Bad Sign**. The web editor works over USB MIDI in Chrome or Edge and reads the firmware's model catalog. It supports local NAM A2 Lite imports and browsing TONE3000 for amp and pedal captures. For stock BlackBox V21 recovery, see the [factory sound restore guide](docs/RESCUE.md#blackbox-v21-factory-sounds).

BluesBox development lives on [`experiment/blues-lab`](https://github.com/Testing7B/openpocket/tree/experiment/blues-lab). The fork's `main` serves the V40 and Drive Lab updater channels. For a local BluesBox build, choose **Install a local build** in the updater and select its `.fwsc` and matching `manifest.json`. Changes that apply to OpenPocket can be proposed upstream separately.

**Credits.** BluesBox builds on OpenPocket by isod89 and contributors, including its DSP engine, editor, and update tools. The board support and loader come from [Leo Kuroshita / Hügelton Instruments and the Felucca contributors](https://github.com/hugelton/felucca-bsp). Thanks to [Steven Atkinson and Neural Amp Modeler](https://github.com/sdatkinson/neural-amp-modeler), the capture authors, and the [Guitar-TECHS](assets/loops/ATTRIBUTION.md) team for the DI material used in testing. See [credits and third-party licences](NOTICE.md) and the [GPL-3.0-only licence](LICENSE).

For OpenPocket internals and tools, see [build instructions](docs/BUILDING.md), and [NAM documentation](docs/NAM.md).
