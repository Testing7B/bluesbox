/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — the user cabinet IRs in flash (v38).
 *
 * 8 slots of 4 KiB at 0xF4000..0xFC000 (above the preset store; the update loader leaves this range alone:
 * loader/ldr_core.c LDR_IR_LO/HI). A slot: a 64-byte header, then PX_IR_LEN (1024) 16-bit samples at 44.1 kHz:
 *   magic "OPIR", version 1, 1024, name (11 chars + 0), scale (float: the samples' full scale), CRC16 of the samples.
 * The web editor (docs/pxir.js) reads the user's WAV file, resamples it to 44.1 kHz, keeps its first 1024 samples (a
 * short fade at the end), and sends it in pieces (IR_WRITE) into the staging buffer, which is the NAM models' (one
 * upload at a time); IR_COMMIT checks it (header, CRC) and writes the slot with the audio held, as for a NAM model.
 * The names are read once at boot into RAM; the samples are read from the flash only when a preset picks the IR (a full
 * change: px_engine_set with the audio held, as the NAM weights). The engine sets the level (px_dsp.c ir_load_h).
 */
#pragma once

#define IR_BASE   0xF4000u
#define IR_SLOT   0x1000u
#define IR_MAGIC  0x5249504Fu                    /* "OPIR" */
#define IR_HDR    64u
#define IR_BYTES  (IR_HDR + PX_IR_LEN * 2u)      /* 2112 */
#define IR_CHUNK  112u                           /* bytes per IR_WRITE (7-bit packed: 128) */

typedef struct {
    uint32_t magic;
    uint16_t ver, n;
    char name[12];
    float scale;                                 /* sample k = s[k] / 32767 * scale */
    uint16_t crc;                                /* CRC16 (ota_crc16) of the samples */
    uint8_t pad[IR_HDR - 26u];
} ir_hdr_t;
typedef char ir_hdr_check[sizeof(ir_hdr_t) == IR_HDR ? 1 : -1];
typedef char ir_stage_check[sizeof nm_stage >= IR_BYTES ? 1 : -1];
#define ir_stage nm_stage                        /* (the NAM models' staging buffer: one upload at a time) */

static struct { uint8_t ok; char name[12]; } ir_cache[PX_IR_N];
static uint32_t ir_gen;

static int ir_hdr_ok(const ir_hdr_t *h)
{
    return h->magic == IR_MAGIC && h->ver == 1u && h->n == PX_IR_LEN && h->scale > 0.0f && h->scale < 1000.0f;
}
static void ir_scan(void)
{
    ir_hdr_t h;
    for (uint32_t s = 0; s < PX_IR_N; s++) {
        ir_cache[s].ok = 0;
        if (ota_fread(IR_BASE + s * IR_SLOT, &h, sizeof h) || !ir_hdr_ok(&h)) continue;
        ir_cache[s].ok = 1;
        nm_name_clean(ir_cache[s].name, h.name);
    }
    ir_gen++;
}
/* the engine's hook (engine/px_dsp.h): the name from the cache, the samples from the flash (checked) */
int px_ir_user(int slot, char *name12, float *h)
{
    if (slot < 0 || slot >= PX_IR_N || !ir_cache[slot].ok) return 0;
    memcpy(name12, ir_cache[slot].name, 12);
    if (h) {
        ir_hdr_t hd;
        const uint32_t off = IR_BASE + (uint32_t)slot * IR_SLOT;
        int16_t *s = (int16_t *)h + PX_IR_LEN;         /* the samples into the upper half of h, then spread out */
        if (ota_fread(off, &hd, sizeof hd) || !ir_hdr_ok(&hd)) return 0;
        if (ota_fread(off + IR_HDR, s, PX_IR_LEN * 2u)) return 0;
        if ((uint16_t)ota_crc16((const uint8_t *)s, PX_IR_LEN * 2u, 0) != hd.crc) return 0;
        const float k = hd.scale / 32767.0f;
        for (int i = 0; i < PX_IR_LEN; i++) h[i] = (float)s[i] * k;   /* (h[i] is past s[i / 2]: read before written) */
    }
    return 1;
}
/* IR_WRITE: piece idx of the staging buffer; 0 = ok */
static int ir_write_piece(uint32_t idx, const uint8_t *packed, uint32_t n)
{
    uint32_t off = idx * IR_CHUNK;
    if (off >= IR_BYTES) return 1;
    uint32_t want = IR_BYTES - off < IR_CHUNK ? IR_BYTES - off : IR_CHUNK;
    return nm_unpack7(packed, n, ir_stage + off, want) == want ? 0 : 1;
}
/* IR_COMMIT: the staged IR into slot s. 0 ok, 1 invalid, 2 flash */
static int ir_commit(uint32_t s)
{
    ir_hdr_t *h = (ir_hdr_t *)ir_stage;
    if (s >= PX_IR_N || !ir_hdr_ok(h)) return 1;
    if ((uint16_t)ota_crc16(ir_stage + IR_HDR, PX_IR_LEN * 2u, 0) != h->crc) return 1;
    nm_name_clean(h->name, h->name);
    const uint32_t base = IR_BASE + s * IR_SLOT;
    uint32_t k;
    int rc = 0;
    st_audio_hold(1);
    if (fl_erase4k(base, &k)) rc = 2;
    if (!rc && fl_write(base + IR_HDR, ir_stage + IR_HDR, IR_BYTES - IR_HDR)) rc = 2;
    if (!rc && fl_write(base, ir_stage, IR_HDR)) rc = 2;   /* header last: a cut write leaves no valid slot */
    if (!rc) {
        static uint8_t chk[256];
        for (uint32_t o = 0; !rc && o < IR_BYTES; o += sizeof chk) {
            uint32_t m = IR_BYTES - o < sizeof chk ? IR_BYTES - o : sizeof chk;
            if (ota_fread(base + o, chk, m) || memcmp(chk, ir_stage + o, m)) rc = 2;
        }
    }
    st_audio_hold(0);
    ir_scan();
    return rc;
}
/* IR_ERASE: slot s emptied. 0 ok, 1 invalid, 2 flash */
static int ir_erase(uint32_t s)
{
    uint32_t k;
    int rc = 0;
    if (s >= PX_IR_N) return 1;
    st_audio_hold(1);
    if (fl_erase4k(IR_BASE + s * IR_SLOT, &k)) rc = 2;
    st_audio_hold(0);
    ir_scan();
    return rc;
}
