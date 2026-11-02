#!/usr/bin/env python3
"""Empaquette un app .bin (ex. openpocket.bin) en image flash 1 Mo flashable via UBOOT/Recover.
   pocketx_appimage.py DUMP_1Mo.bin APP.bin OUT.bin
Remplace app.bin dans la zone app du dump par APP (complété en 0xFF à la taille d'origine),
reconstruit les CRC JLFS (app.bin + app_area_head) et ré-encrypte le SFC. Tout le reste
(bootloader, cabs, presets, données d'unité) est conservé tel quel."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pocketx_fw as fw

def build(dump_path, app_path, out_path):
    raw = bytearray(open(dump_path, 'rb').read())
    if len(raw) != 0x100000:
        raise SystemExit(f"dump doit faire 1 Mo, pas {len(raw)}")
    dec = bytearray(fw.flash_decrypt(bytes(raw)))
    dec_orig = bytes(dec)
    ents = fw.flash_parse(dec)
    app = ents[1]
    if app['name'] != 'app.bin':
        raise SystemExit(f"2e entree JLFS != app.bin ({app['name']})")
    lo, ln, o = 0x4000 + app['addr'], app['len'], app['o']
    newapp = open(app_path, 'rb').read()
    if len(newapp) > ln:
        raise SystemExit(f"app trop gros: {len(newapp)} > {ln}")
    dec[lo:lo+ln] = newapp + b'\xff' * (ln - len(newapp))
    struct.pack_into("<H", dec, o + 2, fw.crc16(dec[lo:lo+ln]))        # app.bin data crc
    struct.pack_into("<H", dec, o, fw.crc16(dec[o+2:o+32]))            # app.bin entry crc
    hl = struct.unpack_from("<I", dec, 0x4008)[0]
    struct.pack_into("<H", dec, 0x4002, fw.crc16(dec[0x4020:0x4000+hl]))   # app_area_head data crc
    struct.pack_into("<H", dec, 0x4000, fw.crc16(dec[0x4002:0x4020]))      # app_area_head entry crc
    nblk = 0
    for b in range(0x4000, len(dec), 32):
        if dec[b:b+32] != dec_orig[b:b+32]:
            raw[b:b+32] = fw.sfc_block(bytes(dec[b:b+32]), b - 0x4000)
            nblk += 1
    open(out_path, 'wb').write(raw)
    return nblk, len(newapp), ln

if __name__ == '__main__':
    if len(sys.argv) != 4:
        print(__doc__); sys.exit(2)
    nblk, used, ln = build(*sys.argv[1:4])
    print(f"OK -> {sys.argv[3]} : app {used} o (sur {ln}), {nblk} blocs SFC ré-encryptés")
    # auto-vérif
    dec = fw.flash_decrypt(open(sys.argv[3], 'rb').read())
    bad = [k for k, v in fw.flash_checks(dec, fw.flash_parse(dec)).items() if not v]
    print("auto-vérif CRC: TOUT OK" if not bad else f"ECHEC: {bad}")
