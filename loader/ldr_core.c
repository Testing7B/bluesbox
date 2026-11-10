/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modified for OpenPocket: the Pocket X layout (app area to 0xA3000, update records between 0xA3000 and 0xFF000,
 * no Felucca store). The comments below that name the FM-1's addresses describe the original. */
/* Felucca update loader, the portable part (step 2 of the M-UPGRADE update
 * protocol).
 *
 * The host serves the package ("logical image") with cmd 0x30 reads (ota.c).
 * The loader:
 *   1. reads the UFW header and finds flash.bin;
 *   2. checks that the package's app area decrypts with THIS chip's key
 *      (taken from isd_config.ini in the device's own flash head), so a
 *      package for another key is refused before anything is erased;
 *   2b. reads the whole flash.bin once and checks it against the CRC16 of its
 *      UFW entry (the vendor tools' check), keeping each app sector's CRC16:
 *      a damaged or truncated package is refused before anything is erased;
 *   3. writes ONLY the app area [0x4000, 0x93000), sector by sector, skipping
 *      sectors that are already equal, and verifies each one; a sector that
 *      comes in different from the checked pass is read again, never written;
 *      the flash head (SPL, isd_config) is never written;
 *   4. asks 0xF0000000 ("success"), then invalidates the update record (RAM
 *      and flash: 4K boundary - 256 between 0x93000 and 0xFC000, outside
 *      Felucca's store, where only user data lives) so the SPL boots the new
 *      app, and resets.
 * Power loss during 3: the record is still there, the SPL runs the loader
 * again on the next power-on and the host can resume.
 *
 * Hooks from the platform (loader.c, ldr_test.c):
 *   ldr_fread(off, p, n)  ldr_erase(off)  ldr_prog(off, p, n)   flash, 0 = ok
 *   ldr_record_clear()    forget the RAM update record
 *   ldr_progress(done, total)  both passes on one scale: the check (2b, flash.bin) then the write (3, the app area)
 * plus the ota.c hooks (frames, time, idle). */
#define LDR_APP_LO 0x4000u
#define LDR_APP_HI 0xA3000u                     /* Pocket X: PRCT [0, 0xA3000) (PocketX_016) */
#define LDR_REC_LO 0xA3000u                     /* update records live above the app area ... */
#define LDR_REC_HI 0xFF000u                     /* ... and below key_mac (Pocket X); OpenPocket stages at 0xE0000 */
#define LDR_DATA_LO 0xF0000u                    /* OpenPocket's preset store (px_store.h, copies A and B): */
#define LDR_DATA_HI 0xF4000u                    /* user data, never erased even if a sector ends like a record */
#define LDR_NAM_LO 0xC0000u                     /* v37: OpenPocket's NAM user models (firmware/px_models.h): */
#define LDR_NAM_HI 0xE0000u                     /* user data as well */
#define LDR_IR_LO 0xF4000u                      /* v38: OpenPocket's cabinet IRs (firmware/px_irs.h): */
#define LDR_IR_HI 0xFC000u                      /* user data as well */
#define LDR_NSEC ((LDR_APP_HI - LDR_APP_LO) >> 12)
#define LDR_FL_MAX 0x100000u                    /* flash.bin: at most the 1 MiB part */
#ifdef LDR_BLACKBOX_V21
/* The browser builds this image from verified, user-supplied vendor files.
 * Leave the flash head, gap between the app and bank, and settings after the bank alone. */
#define V21_APP_HI  0x9F000u
#define V21_BANK_LO 0xBF000u
#define V21_BANK_HI 0xFCCCAu
#define V21_NSEC    ((V21_BANK_HI - V21_BANK_LO + 0xFFFu) >> 12)
#define V21_APP_CRC 0xB0CDu
#define V21_BANK_CRC 0xD673u
#else
#define V21_NSEC 0u
#endif

static int ldr_fread(uint32_t off, void *p, uint32_t n);
static int ldr_erase(uint32_t off);
static int ldr_prog(uint32_t off, const void *p, uint32_t n);
static void ldr_record_clear(void);
static void ldr_progress(uint32_t done, uint32_t total);

/* SFC cipher: per 32-byte block, key ^ (block offset >> 2) */
static void ldr_sfc(uint8_t *p, uint32_t n, uint32_t base_off, uint32_t key)
{
    uint32_t i, j;
    for (i = 0; i < n; i += 32u) {
        uint32_t k = key ^ ((base_off + i) >> 2);
        for (j = 0; j < 32u && i + j < n; j++) {
            p[i + j] ^= (uint8_t)k;
            k = ((k << 1) ^ (k & 0x8000u ? 0x1021u : 0u)) & 0xFFFFu;
        }
    }
}

/* chip key from the device's flash head: JLFS top entries (ENC 0xFFFF) -> isd_config.ini blob */
static int ldr_chip_key(uint32_t *key)
{
    uint8_t e[32], blob[34];
    uint32_t off, i, sum, k;
    for (off = 32; off < 0x400u; off += 32u) {
        if (ldr_fread(off, e, 32))
            return -1;
        ota_jl_enc(e, 32);
        if (ota_crc16(e + 2, 30, 0) != ota_rd16(e))
            return -2;
        if (!ota_memeq(e + 16, (const uint8_t *)"isd_config.ini", 15)) {
            if (ota_rd16(e + 14))
                return -3;                               /* last entry, not found */
            continue;
        }
        if (ldr_fread(ota_rd32(e + 4), blob, 34) || ota_crc16(blob, 32, 0) != ota_rd16(blob + 32))
            return -4;
        for (i = 0, sum = 0; i < 16u; i++)
            sum += blob[i];
        sum &= 0xFFu;
        sum = sum >= 0xE0u ? 0xAAu : sum <= 0x10u ? 0x55u : sum;
        for (i = 0, k = 0; i < 16u; i++)
            if ((uint32_t)(blob[16 + i] ^ blob[15 - i]) < sum)
                k |= 1u << i;
        *key = k;
        return 0;
    }
    return -3;
}

/* 2b. flash.bin [0, fl_size) from the host, before any erase: its CRC16 must be the UFW entry's (want); sc gets
 * the CRC16 of each app sector as it came in */
static int ldr_check(uint32_t fl_off, uint32_t fl_size, uint32_t want, uint16_t *sc, uint8_t *b, uint32_t stock)
{
    uint32_t i, n, crc = 0;
#ifdef LDR_BLACKBOX_V21
    uint32_t app_crc = 0, bank_crc = 0;
#else
    (void)stock;
#endif
    for (i = 0; i < fl_size; i += n) {
        n = fl_size - i > 512u ? 512u : fl_size - i;
        if (ota_read(fl_off + i, b, n))
            return -7;
        crc = ota_crc16(b, n, crc);
#ifdef LDR_BLACKBOX_V21
        if (stock && i < V21_APP_HI) app_crc = ota_crc16(b, n, app_crc);
        if (stock && i >= V21_BANK_LO) {
            uint32_t k = LDR_NSEC + ((i - V21_BANK_LO) >> 12);
            sc[k] = (uint16_t)ota_crc16(b, n, (i & 0xFFFu) ? sc[k] : 0u);
            bank_crc = ota_crc16(b, n, bank_crc);
        }
#endif
        if (!((i + n) & 0xFFFu))
            ldr_progress(i + n, fl_size + (LDR_APP_HI - LDR_APP_LO));
        if (i >= LDR_APP_LO && i < LDR_APP_HI) {     /* (512-byte reads from 0: never across a sector) */
            uint32_t k = (i - LDR_APP_LO) >> 12;
            sc[k] = (uint16_t)ota_crc16(b, n, (i & 0xFFFu) ? sc[k] : 0u);
        }
    }
#ifdef LDR_BLACKBOX_V21
    if (stock && (app_crc != V21_APP_CRC || bank_crc != V21_BANK_CRC))
        return -14;
#endif
    return crc == want ? 0 : -12;
}

static int ldr_write_span(uint32_t fl_off, uint32_t lo, uint32_t hi, const uint16_t *sc,
                          uint8_t *sec, uint8_t *cur, uint32_t fl_size, uint32_t *done)
{
    for (uint32_t s = lo; s < hi; s += 0x1000u) {
        uint32_t length = hi - s < 0x1000u ? hi - s : 0x1000u;
        uint32_t again, k;
        for (again = 0; again < 3u; again++) {
            for (k = 0; k < length; k += 512u) {
                uint32_t n = length - k < 512u ? length - k : 512u;
                if (ota_read(fl_off + s + k, sec + k, n))
                    return -7;
            }
            if (ota_crc16(sec, length, 0) == sc[(s - lo) >> 12])
                break;
        }
        if (again == 3u) return -13;
        if (ldr_fread(s, cur, 0x1000u)) return -8;
        for (k = length; k < 0x1000u; k++) sec[k] = cur[k];
        if (!ota_memeq(sec, cur, 0x1000u)) {
            uint32_t tries;
            for (tries = 0; tries < 2u; tries++) {
                if (ldr_erase(s)) return -9;
                for (k = 0; k < 0x1000u; k += 256u)
                    if (ldr_prog(s + k, sec + k, 256)) return -10;
                if (ldr_fread(s, cur, 0x1000u)) return -8;
                if (ota_memeq(sec, cur, 0x1000u)) break;
            }
            if (tries == 2u) return -11;
        }
        *done += 0x1000u;
        ldr_progress(fl_size + *done, fl_size + (LDR_APP_HI - LDR_APP_LO));
    }
    return 0;
}

static int ldr_session(void)
{
    static uint8_t hdr[0x400], sec[0x1000], cur[0x1000];
    static uint16_t sc[LDR_NSEC + V21_NSEC];
    uint32_t i, fl_off, fl_size, ota_off, ota_len, key, s, want = ~0u, done = 0, stock = 0;   /* (no CRC16: no flash.bin entry) */
    int rc;
    /* 1. UFW header + entry list (ota.c; it leaves the entries decoded in hdr) */
    if ((rc = ota_ufw(hdr, &fl_off, &fl_size, &ota_off, &ota_len)) != 0)
        return rc;
    if (!fl_off || fl_size < LDR_APP_HI || fl_size > LDR_FL_MAX)
        return -3;
#ifdef LDR_BLACKBOX_V21
    stock = fl_size == V21_BANK_HI;
#endif
    for (i = 0; i < ota_rd16(hdr + 8); i++)
        if (ota_rd16(hdr + 0x40 + i * 0x50u) == 0)
            want = ota_rd16(hdr + 0x40 + i * 0x50u + 4u);   /* flash.bin's data CRC16 */
    /* 2. the package's app area must decrypt with this chip's key */
    if ((rc = ldr_chip_key(&key)) != 0)
        return -40 + rc;
    if (ota_read(fl_off + LDR_APP_LO, sec, 32))
        return -5;
    ldr_sfc(sec, 32, 0, key);
    if (ota_crc16(sec + 2, 30, 0) != ota_rd16(sec))
        return -6;                                       /* package for another chip key */
    /* 2b. the whole package first: nothing is erased for a damaged one */
    if ((rc = ldr_check(fl_off, fl_size, want, sc, sec, stock)) != 0)
        return rc;
#ifdef LDR_BLACKBOX_V21
    if (stock) {
        /* Keep the staged loader and its record available until the verified app is in place.
         * Then replace the five sectors where the staging area overlaps the factory bank. */
        if ((rc = ldr_write_span(fl_off, V21_BANK_LO, OTA_AREA, sc + LDR_NSEC, sec, cur, fl_size, &done)) ||
            (rc = ldr_write_span(fl_off, OTA_AREA + OTA_AREA_LEN, V21_BANK_HI,
                sc + LDR_NSEC + ((OTA_AREA + OTA_AREA_LEN - V21_BANK_LO) >> 12), sec, cur, fl_size, &done)) ||
            (rc = ldr_write_span(fl_off, LDR_APP_LO, V21_APP_HI, sc, sec, cur, fl_size, &done)) ||
            (rc = ldr_write_span(fl_off, OTA_AREA, OTA_AREA + OTA_AREA_LEN,
                sc + LDR_NSEC + ((OTA_AREA - V21_BANK_LO) >> 12), sec, cur, fl_size, &done)))
            return rc;
    } else
#endif
    if ((rc = ldr_write_span(fl_off, LDR_APP_LO, LDR_APP_HI, sc, sec, cur, fl_size, &done)) != 0)
        return rc;
    /* 4. finish: the host confirms, the record goes, the SPL boots the app */
    for (i = 0; i < 4u; i++)
        if (!ota_read(0xF0000000u, sec, 8))
            break;
    ldr_record_clear();
    if (stock) return 0;  /* the factory bank has already replaced the staging record */
    for (s = LDR_REC_HI; s > LDR_REC_LO; s -= 0x1000u) {   /* flash records: 4K boundary - 256 */
        uint8_t r[80];
        if (s - 0x1000u >= LDR_DATA_LO && s - 0x1000u < LDR_DATA_HI)
            continue;                                    /* Felucca's store: user data that may look like one */
        if (s - 0x1000u >= LDR_NAM_LO && s - 0x1000u < LDR_NAM_HI)
            continue;                                    /* the NAM models (v37) */
        if (s - 0x1000u >= LDR_IR_LO && s - 0x1000u < LDR_IR_HI)
            continue;                                    /* the cabinet IRs (v38) */
        if (ldr_fread(s - 0x100u, r, 80))
            break;
        if (ota_rd16(r + 6) == 0x5441u && ota_rd16(r) && ota_rd16(r) == ota_crc16(r + 2, 78, 0))
            ldr_erase(s - 0x1000u);
    }
    return 0;
}
