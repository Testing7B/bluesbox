# Installer page (docs/index.html)

## Sub-features
Connect to the pedal over Web MIDI, detect the running firmware (OpenPocket vN or official), Install, Rescue mode (pedal in a boot loop), return to the official firmware.

## How to get to it (user POV)
Open the page in Chrome/Edge, press Connect, plug the pedal in, press Install.

## Driving it with Playwright
`sh .claude/skills/verify/scripts/pages.sh`; `tests/test_page.mjs` serves `docs/` plus fixture packages, bridges a fake Web MIDI to `build/devsim` (the pedal's real `ota.c` and `ldr_core.c`) and clicks Connect, Install, rescue. End state: "Done: the pedal now runs PocketX_924", the simulated flash holds the new app, screenshots `build/page/*.png`.

## Gotchas
- Fixtures are version 924 packages made by `tests/fixtures.py`, not the real build.
- Nothing proves a real pedal: only the captain installs, by hand.
- The harmless `Failed to load resource` line is the page's web-font request being refused offline.
