#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# OpenPocket v40 — moves functions of a compiled object into the RAM code, without touching the code itself: the
# sections the compiler made for them (-ffunction-sections: .text.NAME) are renamed .ram_text.dsp.NAME, which the
# linker script (firmware/app.ld) places in the RAM code window (.ram_text), copied from the flash at boot. The stock
# BlackBox firmware runs its whole audio path from RAM the same way (docs/STOCK-FIRMWARE.md, sections 8.2 and 18.1).
#
# The code stays bit for bit what the compiler made for the flash (no attribute in the source, so no change in what the
# compiler inlines). Calls between the flash and these functions stay direct: the RAM code window sits high enough in
# the RAM (0x01C5E000..) to be inside the call instruction's +-4 MiB of all the flash code (firmware/app.ld,
# tools/check_calls.py).
#
#   ramcode.py OBJ.o [--plain] NAME...
#     NAME     .text.NAME -> .ram_text.dsp.NAME (a name the object does not have is reported and skipped: the compiler
#              inlined that function everywhere, or it is not in this object)
#     --plain  also the object's plain .text (code outside any function's own section: the hand-written routines of
#              px_dsp.c / px_nam.c, or a whole object compiled without -ffunction-sections)
# Python standard library only; the object is rewritten in place.
import struct
import sys

PREFIX = b".ram_text.dsp."


def main():
    args = sys.argv[1:]
    if not args:
        raise SystemExit(__doc__ if __doc__ else "usage: ramcode.py OBJ.o [--plain] NAME...")
    path, rest = args[0], args[1:]
    plain = "--plain" in rest
    names = [a.encode() for a in rest if a != "--plain"]
    d = bytearray(open(path, "rb").read())
    if d[:4] != b"\x7fELF" or d[4] != 1 or d[5] != 1:
        raise SystemExit("%s: not a 32-bit little-endian ELF" % path)
    if struct.unpack_from("<H", d, 0x10)[0] != 1:
        raise SystemExit("%s: not a relocatable object" % path)
    shoff, = struct.unpack_from("<I", d, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", d, 0x2E)
    hdr = lambda i: shoff + i * shentsize
    sh = [list(struct.unpack_from("<IIIIIIIIII", d, hdr(i))) for i in range(shnum)]
    so, sz = sh[shstrndx][4], sh[shstrndx][5]
    strtab = bytes(d[so:so + sz])
    name = lambda off: strtab[off:strtab.index(b"\0", off)]

    want = {b".text." + n: PREFIX + n for n in names}
    if plain:
        want[b".text"] = PREFIX + b"text"
    for k in list(want):                              # their relocations too (only for tidiness: the linker goes by
        want[b".rela" + k] = b".rela" + want[k]       # sh_info, not by the name)
        want[b".rel" + k] = b".rel" + want[k]
    new = bytearray(strtab)
    moved, size = [], 0
    for i, s in enumerate(sh):
        nm = name(s[0])
        if nm in want:
            s[0] = len(new)
            new += want[nm] + b"\0"
            if not nm.startswith(b".rel"):
                moved.append(nm)
                size += s[5]
    found = {m[len(b".text."):] for m in moved if m.startswith(b".text.")}
    missing = [n.decode() for n in names if n not in found]
    if not moved:
        print("ramcode: %s: nothing to move" % path)
        return
    # the new section-name table goes at the end of the file (4-byte aligned); its header points there
    while len(d) % 4:
        d.append(0)
    sh[shstrndx][4], sh[shstrndx][5] = len(d), len(new)
    d += new
    for i, s in enumerate(sh):
        struct.pack_into("<IIIIIIIIII", d, hdr(i), *s)
    open(path, "wb").write(d)
    print("ramcode: %s: %d section(s), %d bytes of code to RAM%s" % (
        path, len(moved), size, ("; not in the object (inlined?): " + " ".join(missing)) if missing else ""))


if __name__ == "__main__":
    main()
