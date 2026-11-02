#!/usr/bin/env python3
"""Test fixtures for tests/test_installer.mjs: a fake app (with its OP_PRODUCT field), a fake update loader wrapped
like the real one, OpenPocket packages built by tools/pxpkg.py, and a starting flash with an older OpenPocket.
  fixtures.py OUTDIR"""
import os
import random
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import mkloader  # noqa: E402
import pxpkg  # noqa: E402

out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
rnd = random.Random(7)
body = bytes(rnd.getrandbits(8) for _ in range(90000))
app = b"\x00" * 64 + b"OP-PRODUCT:PocketX_900" + b"\0" * 10 + body           # as the firmware links it
loader = b"\x00\x00FELUCCA-LOADER-1" + bytes(rnd.getrandbits(8) & 0x0F for _ in range(9000))
ota = mkloader.wrap(loader)
sdk = ROOT / "firmware" / "sdk" / "packager"
pxpkg.build(app, ota, out, 24, None, sdk)                                         # PocketX_924, BlackBox_924
files = pxpkg.sdk_files(None, sdk)
old = pxpkg.flash_image(pxpkg.with_product(app[:-500] + b"older", "PocketX_923"), files)
flash = bytearray(b"\xFF" * 0x100000)
flash[:len(old)] = old
flash[0xC0000:0xC0010] = b"STOCK USER DATA!"                                     # USR: must survive
(out / "flash_old.bin").write_bytes(bytes(flash))
(out / "app_new_px.bin").write_bytes(pxpkg.with_product(app, "PocketX_924"))
print("fixtures ok:", ", ".join(sorted(p.name for p in out.iterdir())))
