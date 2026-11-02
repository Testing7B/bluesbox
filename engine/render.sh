#!/usr/bin/env bash
# Passe un WAV guitare DI dans le moteur OpenPocket -> WAV traité.
#   render.sh  in.wav  out.wav  [preset 1..42]  [amp 1..29]  [gain 0..100]
# Presets et amplis : docs/SONS-ET-COMMANDES.md (ex. 6 = BRIT JANGLE, 16 = GROOVE METAL, 21 = SWELL PAD)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"
CC=""
for c in cc gcc clang; do command -v $c >/dev/null 2>&1 && { CC=$c; break; }; done
if [ -z "$CC" ]; then
  echo "Pas de compilateur C dans WSL. Installe-le :  sudo apt-get update && sudo apt-get install -y build-essential"
  exit 1
fi
if [ ! -x ./px_render ] || [ px_render.c -nt px_render ] || [ px_dsp.c -nt px_render ]; then
  echo "Compilation du moteur..."; $CC -O2 -std=c11 -I. px_render.c px_dsp.c -lm -o px_render
fi
if [ $# -lt 2 ]; then echo "usage: render.sh in.wav out.wav [preset 1..42] [amp 1..29] [gain 0..100]"; exit 1; fi
./px_render "$@"
