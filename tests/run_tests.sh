#!/bin/sh
# OpenPocket host tests (Linux / WSL / macOS; no pedal, no JieLi toolchain needed):
#   tests/run_tests.sh [PocketX_016.fwsc]
#  - engine: every block x model x extreme knob, signal / silence / overload, with the pedal's own libm (px_math.c):
#    no NaN; long runs (3 s pluck + 8 s silence): nothing runs away (only delays at FDBK 100 hold on, by design)
#  - preset store: save / reload / power cut at every step of a save (firmware/px_store.h on a simulated flash)
#  - packages: tools/pxpkg.py rebuilds the official PocketX_016 app area byte for byte (needs the official file)
#  - installer: docs/pxota.js + docs/pxpkg.js against a simulated pedal running the real update code (ota.c, ldr_core.c)
#  - page: docs/index.html in headless Chromium (needs Playwright: npm i playwright && npx playwright install chromium)
set -e
cd "$(dirname "$0")/.."
mkdir -p build
CC="${CC:-cc}"
echo "== engine"
$CC -O2 -Iengine tests/engine/sweep2.c engine/px_dsp.c firmware/px_math.c -lm -o build/sweep && build/sweep | tail -1
$CC -O2 -Iengine tests/engine/long.c engine/px_dsp.c firmware/px_math.c -lm -o build/long && build/long | tail -1
echo "== preset store"
$CC -O1 -Iengine tests/test_store.c engine/px_dsp.c firmware/px_math.c -lm -o build/test_store && build/test_store | tail -2
echo "== simulated pedal"
$CC -O1 -Wall -Wno-unused-function -o build/devsim tests/devsim.c
if [ -n "$1" ]; then echo "== packages"; python3 tests/test_pxpkg.py "$1"; fi
echo "== installer"
node tests/test_installer.mjs "$1"
if node -e "require('playwright')" 2>/dev/null; then echo "== page"; node tests/test_page.mjs build/page; else echo "(Playwright not installed: page test skipped)"; fi
