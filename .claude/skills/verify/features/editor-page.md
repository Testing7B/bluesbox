# Editor page (docs/editor.html)

## Sub-features
52 presets (load, save, save as, move, factory reset), the current sound (blocks, types, parameters, order by dragging, name), JSON export/import, live follow of changes made on the pedal (WATCH), unsaved-change prompts, phone layout.

## How to get to it (user POV)
Open the editor from the installer page, Connect, edit.

## Driving it with Playwright
`sh .claude/skills/verify/scripts/pages.sh`; `tests/test_editor_page.mjs` talks to `build/edsim` (the real `px_editor.h` and `px_store.h`) through a fake Web MIDI. End state: every line `ok`, `editor page test passed`, `build/editor/editor_1..6*.png`. Protocol only (no browser): `node tools/op.mjs test` runs `tests/test_editor.mjs`.

## Gotchas
- Protocol is in `docs/EDITOR-PROTOCOL.md`; a change to a command needs both `px_editor.h` and `docs/pxedit.js`.
- Simulated flash only; no pedal.
