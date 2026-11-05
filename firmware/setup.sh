#!/bin/sh
# Installs the JieLi toolchain (clang 4.0.1 pi32v2 + ld), where build.sh looks for it (~/.jieli/toolchain).
# The download is pinned: the file's SHA-256 is checked, and nothing is installed if it differs.
# The toolchain is JieLi's, not ours: it is never committed, only downloaded from JieLi's own server (see docs/BUILDING.md).
#   JIELI_DEST=/dir   install elsewhere (default ~/.jieli); the Dockerfile uses this
set -e
TC_URL="https://jl-update.oss-cn-shenzhen.aliyuncs.com/jieli-linux-toolchains-20260730.1.tar.xz"
TC_SHA256="a1545cddbc451a06bac43eb5cd96d831470c42a97c5c0b1f98879a20fc7647bd"
echo "== Prerequisites (curl, xz, ca-certificates) =="
if ! command -v curl >/dev/null || ! command -v xz >/dev/null; then
  apt-get update -y >/dev/null 2>&1 || true
  apt-get install -y curl xz-utils ca-certificates >/dev/null 2>&1 || \
    { echo "WARNING: install them by hand: apt-get install curl xz-utils"; }
fi
DEST="${JIELI_DEST:-$HOME/.jieli}"; mkdir -p "$DEST"
TMP="$(mktemp)"; trap 'rm -f "$TMP"' EXIT
echo "== Downloading the JieLi toolchain (jl-update.oss-cn-shenzhen.aliyuncs.com, JieLi's download server) =="
curl -fL --retry 3 -o "$TMP" "$TC_URL"
GOT="$(sha256sum "$TMP" 2>/dev/null | cut -d' ' -f1)"
[ -n "$GOT" ] || GOT="$(shasum -a 256 "$TMP" | cut -d' ' -f1)"
if [ "$GOT" != "$TC_SHA256" ]; then
  echo "FAILED: checksum mismatch."; echo "  expected $TC_SHA256"; echo "  got      $GOT"
  echo "  JieLi may have replaced the file: see docs/BUILDING.md before updating the pin."; exit 1
fi
tar -xJf "$TMP" -C "$DEST"
TC="$(ls -d "$DEST"/jieli-linux-toolchains-* 2>/dev/null | sort | tail -1)"
[ -n "$TC" ] || { echo "FAILED: nothing extracted."; exit 1; }
ln -sfn "$(basename "$TC")" "$DEST/toolchain"
echo "== Check =="
echo "JIELI_TOOLCHAIN=$DEST/toolchain"
[ -x "$DEST/toolchain/pi32v2/bin/clang" ] && echo "  clang   : OK" || echo "  clang   : MISSING (failed)"
[ -x "$DEST/toolchain/pi32v2/bin/ld" ]    && echo "  ld      : OK" || echo "  ld      : MISSING"
[ -x "$DEST/toolchain/common/bin/objcopy" ] && echo "  objcopy : OK" || echo "  objcopy : MISSING"
echo "Done. You can run make build (or node tools/op.mjs build)."
