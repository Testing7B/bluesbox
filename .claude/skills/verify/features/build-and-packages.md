# Build and packages

## Sub-features
- `firmware/openpocket.bin` (the app), the update loader (`build/loader/ota.bin`), `PocketX_9NN.fwsc` and `BlackBox_9NN.fwsc`, `manifest.json`.
- Deterministic output: `build/SHA256SUMS`.

## How to get to it (user POV)
A maintainer runs `build.bat` / `make package`; a user downloads the packages and the installer page uses them.

## Driving it with op.mjs
`node tools/op.mjs clean && node tools/op.mjs package`. Twice in a row must print identical checksums. Version = `OP_VERSION_NUM` in `firmware/openpocket.c`.

## Gotchas
- Needs Docker and the pinned image; first run downloads the toolchain.
- Outputs are gitignored; `docs/firmware/` holds only a README in git.
- A test package must never become the default in `manifest.json`.
