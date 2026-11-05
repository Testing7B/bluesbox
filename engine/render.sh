#!/usr/bin/env bash
# Runs a DI guitar WAV through the OpenPocket engine -> processed WAV.
#   render.sh  in.wav  out.wav  [preset 1..52]  [amp 1..8]  [gain 0..100]
# Presets and amps: docs/SOUNDS-AND-CONTROLS.md (e.g. 6 = BRIT JANGLE, 16 = GROOVE METAL, 21 = SWELL PAD)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"
CC=""
for c in cc gcc clang; do command -v $c >/dev/null 2>&1 && { CC=$c; break; }; done
if [ -z "$CC" ]; then
  echo "No C compiler found. Install one:  sudo apt-get update && sudo apt-get install -y build-essential"
  exit 1
fi
if [ ! -x ./px_render ] || [ px_render.c -nt px_render ] || [ px_dsp.c -nt px_render ]; then
  echo "Building the engine..."; $CC -O2 -std=c11 -I. px_render.c px_dsp.c -lm -o px_render
fi
if [ $# -lt 2 ]; then echo "usage: render.sh in.wav out.wav [preset 1..52] [amp 1..8] [gain 0..100]"; exit 1; fi
./px_render "$@"
