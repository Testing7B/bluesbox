#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# OpenPocket v40.1 — renames symbols of a compiled object (what the toolchain's objcopy cannot do): every symbol named
# OLD becomes NEW, defined or not, so the object's calls to OLD go to NEW. The Bluetooth test image uses it to send the
# controller library's memcpy and memset calls to flash copies (firmware/build.sh, firmware/ble/ble_libc.c): v40 made
# the firmware's own RAM code, out of the call range of the library's code at the end of the flash.
#
#   elfsym.py OBJ.o OLD=NEW...
# The new names go into a new symbol-name table at the end of the file; nothing else moves. Python standard library
# only; the object is rewritten in place.
import struct
import sys

SHT_SYMTAB = 2


def main():
    args = sys.argv[1:]
    if len(args) < 2 or not all("=" in a for a in args[1:]):
        raise SystemExit("usage: elfsym.py OBJ.o OLD=NEW...")
    path = args[0]
    want = {a.split("=", 1)[0].encode(): a.split("=", 1)[1].encode() for a in args[1:]}
    d = bytearray(open(path, "rb").read())
    if d[:4] != b"\x7fELF" or d[4] != 1 or d[5] != 1:
        raise SystemExit("%s: not a 32-bit little-endian ELF" % path)
    if struct.unpack_from("<H", d, 0x10)[0] != 1:
        raise SystemExit("%s: not a relocatable object" % path)
    shoff, = struct.unpack_from("<I", d, 0x20)
    shentsize, shnum = struct.unpack_from("<HH", d, 0x2E)
    hdr = lambda i: shoff + i * shentsize
    sh = [list(struct.unpack_from("<IIIIIIIIII", d, hdr(i))) for i in range(shnum)]
    renamed = 0
    for s in sh:
        if s[1] != SHT_SYMTAB:
            continue
        st = sh[s[6]]                                  # sh_link: its name table
        old = bytes(d[st[4]:st[4] + st[5]])
        new = bytearray(old)
        for off in range(s[4], s[4] + s[5], 16):       # Elf32_Sym: st_name first
            nm_off, = struct.unpack_from("<I", d, off)
            nm = old[nm_off:old.index(b"\0", nm_off)]
            if nm in want:
                struct.pack_into("<I", d, off, len(new))
                new += want[nm] + b"\0"
                renamed += 1
        if len(new) != len(old):                       # the new table at the end of the file (4-byte aligned)
            while len(d) % 4:
                d.append(0)
            st[4], st[5] = len(d), len(new)
            d += new
    for i, s in enumerate(sh):
        struct.pack_into("<IIIIIIIIII", d, hdr(i), *s)
    open(path, "wb").write(d)
    return renamed


if __name__ == "__main__":
    main()
