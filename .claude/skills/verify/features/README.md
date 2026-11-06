# OpenPocket feature map

| File | Feature | Driver |
| --- | --- | --- |
| [build-and-packages.md](build-and-packages.md) | Firmware build, update loader, `.fwsc` packages, `manifest.json` | `op.mjs package` |
| [installer-page.md](installer-page.md) | Web installer `docs/index.html`: connect, detect, install, rescue | `pages.sh` |
| [editor-page.md](editor-page.md) | Web editor `docs/editor.html`: presets, blocks, import/export | `pages.sh` |
| [screen-ui.md](screen-ui.md) | Pedal screen pages (play, edit, save, tuner) | `uisim` via `op.mjs test` |
| [midi-usb.md](midi-usb.md) | USB-MIDI: update/editor SysEx today; musical MIDI with the MIDI feature | simulators; hardware by the captain |
