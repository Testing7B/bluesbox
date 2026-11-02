/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — host test of the preset store (firmware/px_store.h) on a simulated NOR flash:
 * save / reload, alternate copies, a power cut at every step of a save, the bytes the update loader reads
 * (0xF00.. of each sector) kept erased, writes only inside the store.
 *   cc -O1 -Iengine tests/test_store.c engine/px_dsp.c -lm -o build/test_store && build/test_store */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "px_dsp.h"

static uint8_t nor[0x100000];
static int fails, ops, cut_at = -1, dead;          /* power cut: after cut_at flash operations nothing more happens */
#define CHECK(c, what) do { if (!(c)) { printf("FAIL %s\n", what); fails++; } } while (0)
static int in_store(uint32_t off, uint32_t n) { return off >= 0xF0000u && off + n <= 0xF4000u; }

/* ---- the firmware pieces px_store.h uses ---- */
static uint32_t ota_crc16(const uint8_t *p, uint32_t n, uint32_t c)
{
    while (n--) { c ^= (uint32_t)*p++ << 8; for (int i = 0; i < 8; i++) c = c & 0x8000u ? (c << 1) ^ 0x1021u : c << 1; c &= 0xFFFFu; }
    return c;
}
static int ota_fread(uint32_t off, void *p, uint32_t n) { memcpy(p, nor + off, n); return 0; }
static int fl_erase4k(uint32_t off, uint32_t *t)
{
    (void)t;
    if (!in_store(off, 0x1000) || (off & 0xFFF)) { printf("FAIL erase outside the store %x\n", off); fails++; return -1; }
    if (dead || (cut_at >= 0 && ops++ >= cut_at)) { dead = 1; return 0; }
    memset(nor + off, 0xFF, 0x1000);
    return 0;
}
static int fl_write(uint32_t off, const uint8_t *s, uint32_t n)
{
    if (!in_store(off, n)) { printf("FAIL write outside the store %x\n", off); fails++; return -1; }
    for (uint32_t i = 0; i < n; i++) {                 /* a cut can land in the middle of a write */
        if (dead || (cut_at >= 0 && ops++ >= cut_at)) { dead = 1; return 0; }
        nor[off + i] &= s[i];
    }
    return 0;
}
static volatile uint8_t a_mute;
static void fm1_wdt_feed(void) {}
static void fm1_delay_ms(int ms) { (void)ms; }
#define FM1_IRQ_ALNK0 11
static void fm1_irq_mask(int n) { (void)n; }
static void fm1_irq_unmask(int n) { (void)n; }
static void a_collect(void) {}
static uint8_t px_flash_ok = 1;
static uint32_t cur_preset;
static px_preset_t PRESET;
static void st_get(uint32_t k, px_preset_t *p);
static void load_preset(uint32_t i) { st_get(i, &PRESET); }
#define ST_IRQ_OFF() ((void)0)
#include "../firmware/px_store.h"

static void reboot(void) { memset(st_slot, 0, sizeof st_slot); st_cur = st_seq = 0; st_last = 0; px_store_boot(); }
static void loader_bytes_clear(void)
{
    for (uint32_t s = 0xF0000u; s < 0xF4000u; s += 0x1000u)
        for (uint32_t i = 0xF00; i < 0xF80; i++)
            if (nor[s + i] != 0xFF) { printf("FAIL byte %x not erased (update loader record zone)\n", s + i); fails++; return; }
}

int main(void)
{
    px_preset_t p, q, f;
    memset(nor, 0xFF, sizeof nor);
    nor[0xEFFFF] = 0x11; nor[0xF4000] = 0x22;          /* neighbours: must not move */
    reboot();
    CHECK(!st_saved(5) && cur_preset == 0, "empty flash: factory presets, start on slot 1");
    px_preset_factory(&f, 5);
    st_get(5, &q);
    CHECK(!memcmp(&q, &f, sizeof q), "empty flash: slot 6 is its factory preset");

    /* save, reboot, the sound is there and the pedal starts on it */
    px_preset_factory(&p, 5);
    p.p[PX_B_AMP][0] = 99; strcpy(p.name, "MY LEAD");
    CHECK(st_save(5, &p) == 0, "save slot 6");
    reboot();
    st_get(5, &q);
    CHECK(st_saved(5) && !memcmp(&q, &p, sizeof q), "after a reboot: slot 6 holds the saved sound");
    CHECK(cur_preset == 5 && !memcmp(&PRESET, &p, sizeof p), "the pedal starts on the slot saved last");
    st_get(4, &q); px_preset_factory(&f, 4);
    CHECK(!memcmp(&q, &f, sizeof q), "other slots stay factory");

    /* many saves: the copies alternate, the newest wins; the last slot (in sector 1) works too */
    for (int i = 0; i < 25; i++) {
        p.p[PX_B_DELAY][0] = (uint8_t)i;
        CHECK(st_save((uint32_t)(i % 2 ? 41 : 0), &p) == 0, "repeated save");
    }
    reboot();
    st_get(0, &q); CHECK(q.p[PX_B_DELAY][0] == 24, "slot 1: the newest of 25 saves");
    st_get(41, &q); CHECK(q.p[PX_B_DELAY][0] == 23, "slot 42 (second sector): its newest save");
    loader_bytes_clear();

    /* factory reset of a slot */
    CHECK(st_reset(41) == 0, "restore slot 42 to factory");
    reboot();
    st_get(41, &q); px_preset_factory(&f, 41);
    CHECK(!st_saved(41) && !memcmp(&q, &f, sizeof q), "slot 42 is factory again after a reboot");

    /* a power cut at every step of a save: after it, the store is the old one or the new one, never broken */
    {
        static uint8_t snap[0x4000];
        px_preset_t before, newp;
        st_get(7, &before);
        newp = before; newp.p[PX_B_REVERB][2] = 77; strcpy(newp.name, "CUT TEST");
        memcpy(snap, nor + 0xF0000, sizeof snap);
        uint32_t seq0 = st_seq, cur0 = st_cur;
        int olds = 0, news = 0, bad = 0;
        for (int cut = 0; cut < 9000; cut += 7) {
            memcpy(nor + 0xF0000, snap, sizeof snap);
            st_seq = seq0; st_cur = cur0;
            reboot();
            ops = 0; cut_at = cut; dead = 0;
            st_save(7, &newp);
            cut_at = -1; dead = 0;
            reboot();
            st_get(7, &q);
            if (!memcmp(&q, &newp, sizeof q)) news++;
            else if (!memcmp(&q, &before, sizeof q)) olds++;
            else bad++;
            st_get(0, &q);
            if (q.p[PX_B_DELAY][0] != 24) bad++;           /* other slots intact */
        }
        printf("power cut at %d points of a save: old kept %d, new %d, broken %d\n", (9000 + 6) / 7, olds, news, bad);
        CHECK(bad == 0 && olds > 0 && news > 0, "a power cut never breaks the store");
    }
    CHECK(nor[0xEFFFF] == 0x11 && nor[0xF4000] == 0x22, "flash outside the store untouched");
    loader_bytes_clear();
    printf(fails ? "%d FAILED\n" : "store tests passed\n", fails);
    return fails != 0;
}
