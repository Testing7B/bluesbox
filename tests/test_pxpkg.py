#!/usr/bin/env python3
"""pxpkg reproduces the app area of the official PocketX_016 package byte for byte (given its app.bin, cfg_tool.bin
and eq_cfg_hw.bin), and its packages parse back with every CRC right.
  python3 tests/test_pxpkg.py PocketX_016.fwsc        (the official file: not in this repository)"""
import struct, sys, hashlib
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import pxpkg, pocketx_fw as fw

def logical(fwsc):
    out = bytearray()
    for i in range(20): out += fwsc[i * 0x30:i * 0x30 + 0x2F]
    return bytes(out + fwsc[20 * 0x30:])

def ufw_files(lg):
    hdr = bytearray(lg[:0x40]); pxpkg.enc(hdr, 0, 0x40)
    n = struct.unpack_from("<H", hdr, 8)[0]
    assert pxpkg.crc16(hdr[2:0x40]) == struct.unpack_from("<H", hdr, 0)[0], "ufw header crc"
    assert pxpkg.crc16(lg[0x40:0x40 + n * 0x50]) == struct.unpack_from("<H", hdr, 2)[0], "ufw list crc"
    files = {}
    for i in range(n):
        e = bytearray(lg[0x40 + i * 0x50:0x40 + (i + 1) * 0x50]); pxpkg.enc(e, 0, 0x50)
        typ, idx, dcrc, _, off, ln = struct.unpack_from("<HHHHII", e, 0)
        name = e[0x40:].split(b"\0")[0].decode()
        d = lg[off:off + ln]
        if name in ("flash.bin", "ota.bin"): assert pxpkg.crc16(d) == dcrc, f"{name} crc"
        files[name] = d
    return files

off = Path(sys.argv[1]).read_bytes()
fl = ufw_files(logical(off))["flash.bin"]
dec = fw.flash_decrypt(fl + b"\xFF" * (0x100000 - len(fl)))
app = dec[0x4000 + 0x120:0x4000 + 0x120 + pxpkg.APP_SLOT]
cfg_tool = dec[0x4000 + 0x9D860:0x4000 + 0x9D860 + 0x17F]
blk = 0x120 + pxpkg.APP_SLOT + 0x17F
eq_len = struct.unpack_from("<I", dec, 0x4000 + blk + 0x20 + 8)[0]
eq = dec[0x4000 + blk + 0x40:0x4000 + blk + 0x40 + eq_len]
area = pxpkg.app_area(app, cfg_tool, eq)
same = fl[0x4000:0x4000 + len(area)] == area
print(f"official app area rebuilt: {len(area)} B, identical: {same}")
assert same
# a full package from that app: parses, every CRC right, identity markers
sdk = {"uboot.boot": dec[0xA0:0xA0 + 0x3830], "cfg_tool.bin": cfg_tool, "cfg/eq_cfg_hw.bin": eq}
pkg = pxpkg.ufw(pxpkg.flash_image(app, sdk), b"OTA" * 100, "PocketX_924")
assert pxpkg.product_of(pkg) == "PocketX_924"
files = ufw_files(logical(pkg))
d2 = fw.flash_decrypt(files["flash.bin"] + b"\xFF" * (0x100000 - len(files["flash.bin"])))
bad = [k for k, v in fw.flash_checks(d2, fw.flash_parse(d2)).items() if not v]
print("built package: identity", pxpkg.product_of(pkg), "| flash checks:", "ALL OK" if not bad else bad)
assert not bad
assert d2[0x4000 + 0x120:0x4000 + 0x120 + pxpkg.APP_SLOT] == app
# the loader's chip-key check (ldr_core.c 2.): head isd_config blob -> key, first area entry decrypts with it
print("chip key from head:", hex(pxpkg.chipkey_decode(d2[0xA0 + 0x3830:0xA0 + 0x3830 + 32])))
print("OK")
