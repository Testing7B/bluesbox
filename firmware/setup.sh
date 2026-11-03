#!/bin/sh
# Installs the JieLi toolchain (clang 4.0.1 pi32v2 + ld) in WSL, where build.sh looks for it.
set -e
echo "== Prerequisites (curl, xz, ca-certificates) =="
if ! command -v curl >/dev/null || ! command -v xz >/dev/null; then
  apt-get update -y >/dev/null 2>&1 || true
  apt-get install -y curl xz-utils ca-certificates >/dev/null 2>&1 || \
    { echo "WARNING: install them by hand: apt-get install curl xz-utils"; }
fi
DEST="$HOME/.jieli"; mkdir -p "$DEST"
echo "== Downloading the JieLi toolchain (pkgman.jieliapp.com) =="
curl -fL --retry 3 https://pkgman.jieliapp.com/s/linux-toolchain | tar -xJ -C "$DEST"
TC="$(ls -d "$DEST"/jieli-linux-toolchains-* 2>/dev/null | sort | tail -1)"
[ -n "$TC" ] || { echo "FAILED: nothing extracted."; exit 1; }
ln -sfn "$(basename "$TC")" "$DEST/toolchain"
echo "== Check =="
echo "JIELI_TOOLCHAIN=$DEST/toolchain"
[ -x "$DEST/toolchain/pi32v2/bin/clang" ] && echo "  clang   : OK" || echo "  clang   : MISSING (failed)"
[ -x "$DEST/toolchain/pi32v2/bin/ld" ]    && echo "  ld      : OK" || echo "  ld      : MISSING"
[ -x "$DEST/toolchain/common/bin/objcopy" ] && echo "  objcopy : OK" || echo "  objcopy : MISSING"
echo "Done. You can run build.bat."
