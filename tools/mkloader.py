#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# OpenPocket — wrap the update loader image (loader.bin, linked at 0x01C0A800) into ota.bin, the package entry the
# chip's SPL loads: outer JLFS head (32 B) + inner head (32 B: load address) + blocks [clen u32][dlen u32][LZ4 block],
# 4096 bytes each except the last. From Felucca's tools/build.py (ldr_wrap; GPL-3.0, Leo Kuroshita, Hügelton
# Instruments). The raw loader.bin is not a valid entry: the official updater stops on it before writing anything.
#   mkloader.py loader.bin ota.bin
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lz4blk  # noqa: E402
from pxpkg import crc16  # noqa: E402

LOADER_LOAD = 0x01C0A800
LOADER_NAME = b"usb_hid_ota.bin"     # the file name the SPL looks for
MAX_IMAGE = 0x14000                  # loader.ld: LDR region; ota.c stages at most 0x5000 - 0x200 B of ota.bin


def head(dcrc, a, b, attr, name):
    body = struct.pack("<HIIBBH16s", dcrc, a, b, attr, 0, 0, name)
    return struct.pack("<H", crc16(body)) + body


def wrap(image):
    if not image or len(image) > MAX_IMAGE:
        raise SystemExit(f"mkloader: image of {len(image)} B (1..{MAX_IMAGE})")
    if image[2:18] != b"FELUCCA-LOADER-1":
        raise SystemExit("mkloader: no FELUCCA-LOADER-1 mark after _start (crt0_ldr.S)")
    blocks = bytearray()
    for i in range(0, len(image), 4096):
        raw = image[i:i + 4096]
        c = lz4blk.compress(raw)
        if len(c) > 4096 or lz4blk.decompress(c) != raw:
            raise SystemExit(f"mkloader: block {i // 4096} does not compress below 4096 B")
        blocks += struct.pack("<II", len(c), len(raw)) + c
    inner = head(crc16(image), len(image), LOADER_LOAD, 0, LOADER_NAME)
    body = inner + bytes(blocks)
    ota = head(crc16(body), 0x20, len(body), 0x41, LOADER_NAME) + body
    if len(ota) > 0x5000 - 0x200 + 0x20:
        raise SystemExit(f"mkloader: ota.bin is {len(ota)} B, more than the 0x5000 B staging area takes")
    return ota


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__ or "usage: mkloader.py loader.bin ota.bin")
    img = Path(sys.argv[1]).read_bytes()
    ota = wrap(img)
    Path(sys.argv[2]).write_bytes(ota)
    print(f"loader: image {len(img)} B at {LOADER_LOAD:#x} -> ota.bin {len(ota)} B")
