# Pedal screen

## Sub-features
Play page (preset, tiles of the 12 blocks), block page, edit page, save page, tuner, boot and error screens.

## How to get to it (user POV)
Knob, PUSH, HOME, footswitches and block buttons on the pedal.

## Driving it with uisim
`node tools/op.mjs test` builds and runs `tests/uisim.c` (the real `ui.h`), writing every page to `build/ui/*.ppm` and a log in which `never drawn` must not appear. View a PPM by converting it (`sips -s format png file.ppm --out file.png` on macOS).

## Gotchas
- Colours and fonts are checked as drawn pixels, not on the real LCD.
- New screens must be added to `tests/uisim.c` or they are never checked.
