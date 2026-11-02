#!/bin/sh
# Installe le toolchain JieLi (clang 4.0.1 pi32v2 + ld) dans WSL, où build.sh le cherche.
set -e
echo "== Prerequis (curl, xz, ca-certificates) =="
if ! command -v curl >/dev/null || ! command -v xz >/dev/null; then
  apt-get update -y >/dev/null 2>&1 || true
  apt-get install -y curl xz-utils ca-certificates >/dev/null 2>&1 || \
    { echo "ATTENTION: installe-les a la main: apt-get install curl xz-utils"; }
fi
DEST="$HOME/.jieli"; mkdir -p "$DEST"
echo "== Telechargement du toolchain JieLi (pkgman.jieliapp.com) =="
curl -fL --retry 3 https://pkgman.jieliapp.com/s/linux-toolchain | tar -xJ -C "$DEST"
TC="$(ls -d "$DEST"/jieli-linux-toolchains-* 2>/dev/null | sort | tail -1)"
[ -n "$TC" ] || { echo "ECHEC: rien extrait."; exit 1; }
ln -sfn "$(basename "$TC")" "$DEST/toolchain"
echo "== Verification =="
echo "JIELI_TOOLCHAIN=$DEST/toolchain"
[ -x "$DEST/toolchain/pi32v2/bin/clang" ] && echo "  clang   : OK" || echo "  clang   : ABSENT (echec)"
[ -x "$DEST/toolchain/pi32v2/bin/ld" ]    && echo "  ld      : OK" || echo "  ld      : ABSENT"
[ -x "$DEST/toolchain/common/bin/objcopy" ] && echo "  objcopy : OK" || echo "  objcopy : ABSENT"
echo "Termine. Tu peux lancer build.bat."
