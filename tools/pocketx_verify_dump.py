#!/usr/bin/env python3
"""Verify a Pocket X flash DUMP pulled from the device over UBOOT (fm1buddy read).

A device dump is the raw flash as stored: head [0,0x4000) (bootloader/SPL/isd_config,
chip key 0x980F) + app area (SFC-encrypted) + unit data near the top (BT MAC / calib).
It is NOT a .fwsc (no UFW wrapper). This tool decrypts the app area and checks the
JLFS integrity chain, so you know the backup is complete before trusting it.

  pocketx_verify_dump.py DUMP.bin

Reuses the format code in pocketx_fw.py (same directory)."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pocketx_fw as fw

CHIPKEY = 0x980F

def verify(path):
    d = open(path, 'rb').read()
    print(f"dump: {path}")
    print(f"  size = {len(d)} (0x{len(d):X})")
    ok = {}
    # 1) plausible flash size
    ok['size is a power-of-two flash (>=512KiB)'] = len(d) in (0x80000, 0x100000, 0x200000, 0x400000)
    # 2) head present (low 0x4000 not blank)
    head = d[:0x4000]
    ok['head [0,0x4000) present (not blank)'] = any(b not in (0x00, 0xFF) for b in head)
    # (the chip key 0x980F is NOT in cleartext in the head: the head's JLFS is ENC-encoded.
    #  UBOOT confirms the key live with READ_KEY at dump time — that is the authority, not these bytes.)
    # 3) decrypt app area and parse JLFS
    try:
        dec = fw.flash_decrypt(d[:min(len(d), 0x100000)])
        ents = fw.flash_parse(dec)
        names = [e['name'] for e in ents] if ents else []
        print(f"  app entries: {names}")
        ok['app_area_head parsed'] = bool(ents)
        chk = fw.flash_checks(dec, ents)
        for k, v in chk.items():
            ok[k] = v
    except Exception as e:
        ok['app area decode'] = False
        print(f"  decode error: {e}")
    print("")
    allok = True
    for k, v in ok.items():
        print(f"  [{'OK' if v else '!!'}] {k}")
        allok = allok and v
    print("")
    print("DUMP LOOKS COMPLETE — safe to keep as your backup." if allok
          else "DUMP INCOMPLETE OR DAMAGED — re-run the extraction, do NOT flash yet.")
    return 0 if allok else 1

if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(__doc__); sys.exit(2)
    sys.exit(verify(sys.argv[1]))
