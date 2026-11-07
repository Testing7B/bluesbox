#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# OpenPocket — refuses a build with a direct call between the flash code (.text*) and the RAM code (.ram_text,
# .c1_text) of the pi32v2 objects given: the call instruction (R_PI32V2_LONG_JUMP_23M2) reaches +-4 MiB, and the flash
# (0x02xxxxxx) is just out of reach of the RAM (0x01C0xxxx). Such a call must go through a pointer (FL_FAR in
# fm1_flash.h). The linker would also refuse it, but names only the first one and not why.
#   check_calls.py OBJ.o...                 (firmware/build.sh, before linking; Python standard library only)
import struct, sys

R_LONG_JUMP = 2                     # R_PI32V2_LONG_JUMP_23M2
RAM = (b".ram_text", b".c1_text")


def read(path):
    d = open(path, "rb").read()
    if d[:4] != b"\x7fELF" or d[4] != 1 or d[5] != 1:
        raise SystemExit("%s: not a 32-bit little-endian ELF" % path)
    shoff, = struct.unpack_from("<I", d, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", d, 0x2E)
    secs = [struct.unpack_from("<IIIIIIIIII", d, shoff + i * shentsize) for i in range(shnum)]
    strtab_off = secs[shstrndx][4]
    name = lambda off, base: d[base + off:d.index(b"\0", base + off)]
    names = [name(s[0], strtab_off) for s in secs]
    syms = []
    for i, s in enumerate(secs):
        if s[1] == 2:                                   # SHT_SYMTAB
            stroff = secs[s[6]][4]
            for k in range(s[5] // 16):
                st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack_from("<IIIBBH", d, s[4] + 16 * k)
                syms.append((name(st_name, stroff), st_info >> 4, st_shndx))
    relas = []
    for i, s in enumerate(secs):
        if s[1] == 4:                                   # SHT_RELA
            for k in range(s[5] // 12):
                off, info, _ = struct.unpack_from("<IIi", d, s[4] + 12 * k)
                relas.append((s[7], off, info >> 8, info & 0xFF))   # (patched section, offset, symbol, type)
    return names, syms, relas


def main():
    objs = [(p, read(p)) for p in sys.argv[1:]]
    in_ram = {}                                         # global symbol -> defined in RAM code
    for p, (names, syms, _) in objs:
        for nm, bind, shndx in syms:
            if nm and bind != 0 and 0 < shndx < 0xFF00:
                in_ram[nm] = names[shndx].startswith(RAM)
    bad = []
    for p, (names, syms, relas) in objs:
        for sec, off, si, typ in relas:
            if typ != R_LONG_JUMP or not names[sec].startswith((b".text", b".ram_text", b".c1_text")):
                continue
            nm, bind, shndx = syms[si]
            if 0 < shndx < 0xFF00:
                dst = names[shndx].startswith(RAM)
            elif nm in in_ram:
                dst = in_ram[nm]
            else:
                continue                                # defined elsewhere (not one of these objects)
            src = names[sec].startswith(RAM)
            if src != dst:
                bad.append("%s: %s+0x%x calls %s (%s)" % (p, names[sec].decode(), off,
                                                       (nm or names[shndx]).decode(), "flash -> RAM" if dst else "RAM -> flash"))
    for b in bad[:40]:
        print(b)
    if bad:
        raise SystemExit("ERROR: %d direct call(s) between the flash and the RAM code (out of the CPU's call range): "
                         "call through a pointer (FL_FAR, fm1_flash.h) or keep both sides in the same place" % len(bad))
    print("calls: no direct call between the flash and the RAM code")


if __name__ == "__main__":
    main()
