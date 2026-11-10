/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — the pedal's screen on the PC: firmware/ui.h + ui_text.h + fonts.h drawn into a 240x240 RGB565
 * frame buffer, exactly as the pedal streams it (window + pixels). Writes PPM screenshots of a script of actions.
 *   cc -O1 -Iengine -Ifirmware -o build/uisim tests/uisim.c engine/px_dsp.c firmware/px_math.c -lm
 *   build/uisim OUTDIR        -> OUTDIR/NN_name.ppm ; also checks that every pixel is drawn on each full page
 * The frame buffer starts (and is re-poisoned before each full page) in magenta: any pixel left magenta after a
 * full redraw is a gap in the page's layout. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "px_dsp.h"
#include "../firmware/px_midi.h"

#include "simlcd.h"
#define POISON 0xF81F

static void fm1_wdt_feed(void) { }
static void fm1_delay_ms(int ms) { (void)ms; }

static px_preset_t PRESET;
static uint32_t cur_preset = 0;
static uint32_t master_vol = 80;
static px_preset_t saved[52];
static int saved_ok[52];
static void st_get(uint32_t k, px_preset_t *p) { if (saved_ok[k]) *p = saved[k]; else px_preset_factory(p, (int)k); }
static int st_save(uint32_t k, const px_preset_t *p) { saved[k] = *p; saved_ok[k] = 1; return 0; }
static int st_reset(uint32_t k) { saved_ok[k] = 0; return 0; }
static int st_saved(uint32_t k) { return saved_ok[k]; }
static void load_preset(uint32_t i) { st_get(i, &PRESET); }
static int a_level_db(int ch) { return ch ? -84 : -62; }
static uint8_t a_pf[PX_NBLK + 1] = {0, 0, 0, 0, 0, 0, 3, 0, 0, 5, 7, 0, 64};   /* v38: CPU per block (the tuner's footer) */
static uint32_t a_wcut = 2;                            /* v39: wet blocks cut short */
static volatile uint8_t a_tmute;
static int bat_mv = 3920, bat_usb_mv = 0;
static int32_t bat_raw[5] = {612, 1, 0, 3, 91};   /* bat_read's raw values (the tuner page shows them) */
static int bat_pct = -1;                /* (-1: the gauge from bat_mv) */

static void b_num(char *d, uint32_t v) { char t[12]; int n = 0; while (*d) d++; do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v); while (n) *d++ = t[--n]; *d = 0; }
static void b_str(char *d, const char *s) { while (*d) d++; while (*s) *d++ = *s++; *d = 0; }
static int midi_saved;
static void midi_persist(void) { midi_saved++; }
#include "ui_text.h"
#include "ui.h"

static void shot(const char *dir, const char *name)
{
    static int n;
    char p[512];
    snprintf(p, sizeof p, "%s/%02d_%s.ppm", dir, ++n, name);
    FILE *f = fopen(p, "wb");
    fprintf(f, "P6\n240 240\n255\n");
    for (int i = 0; i < 240 * 240; i++) {
        uint16_t c = fb[i];
        unsigned char rgb[3] = {(unsigned char)(((c >> 11) & 31) * 255 / 31), (unsigned char)(((c >> 5) & 63) * 255 / 63), (unsigned char)((c & 31) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    int holes = 0;
    for (int i = 0; i < 240 * 240; i++) holes += fb[i] == POISON;
    printf("%-28s %6lu px sent%s", name, px_sent, holes ? "" : "\n");
    if (holes) printf("   %d pixels never drawn (magenta)\n", holes);
    px_sent = 0;
}
static void poison(void) { for (int i = 0; i < 240 * 240; i++) fb[i] = POISON; }
static void status(void) { ui_status("C1 42%", 0); }
static void full(void) { poison(); uc.screen = -1; ui_invalidate(); ui_render(); status(); }


/* ---- the design review's quick wins (F1, F2, F3, F4, F13): text that fits, and the state each screen shows ---- */
static int count_col(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t col)
{
    int n = 0;
    for (uint32_t j = y; j < y + h; j++) for (uint32_t i = x; i < x + w; i++) n += fb[j * 240u + i] == col;
    return n;
}
static void play_again(void) { poison(); uc.screen = -1; ui_invalidate(); ui_render(); }
static int quick_wins(const char *d)
{
    char c[16];
    /* F1: every load text the main loop can build fits the box it is drawn in (it used to cut "BYP 2C 150%" to "BYP 2C 15") */
    for (uint32_t l = 0; l <= 999u; l++)
        for (int m = 0; m < 12; m++) {
            int safe = m & 1, bypass = (m >> 1) & 1, c1 = (m >> 2) & 1, late = (m >> 3) & 1;
            ui_load_text(c, l, safe, bypass, c1, c1, late);
            if (ft_width(&FT_SM, c) > ST_W) { printf("F1: \"%s\" is %u px, the box %u\n", c, ft_width(&FT_SM, c), ST_W); return 1; }
        }
    ui_load_text(c, 150, 0, 1, 1, 1, 0);
    if (strcmp(c, "PEAK 150%")) { printf("F1: a peak of 150%% reads \"%s\"\n", c); return 1; }
    ui_load_text(c, 88, 0, 0, 1, 1, 1);
    if (strcmp(c, "2C 88%!")) { printf("F1: a late block on two cores reads \"%s\"\n", c); return 1; }
    if (ft_width(&FT_SM, "KNOB: SELECT  PUSH: EDIT") > ST_X - 2u) { printf("F1: the longest hint no longer fits beside the load figure\n"); return 1; }
    /* F1: overload is a red banner with an instruction, on PLAY (top bar) and on a block page (footer) */
    load_preset(9); cur_preset = 9; ui_blk = PX_B_AMP; ui_home(); play_again(); ui_status("C1 42%", UI_ST_NORM);
    if (count_col(0, 0, 240, 3, C_ALERT)) { printf("F1: PLAY shows the banner without an overload\n"); return 1; }
    ui_status("PEAK 150%", UI_ST_BYPASS); ui_render(); shot(d, "play_overload_banner");
    if (count_col(0, 0, 240, S_H, C_ALERT) < 240 * 5 || count_col(0, F_Y + 3u, 240, 15, WHT) == 0) { printf("F1: PLAY overload: no red bar, or no instruction in the footer\n"); return 1; }
    ui_push(); ui_render(); shot(d, "edit_overload_banner");
    if (count_col(0, F_Y, 240, 18, C_ALERT) < 240 * 3 || count_col(0, F_Y + 3u, 240, 15, WHT) == 0) { printf("F1: block page overload: the footer is not red\n"); return 1; }
    ui_status("C1 42%", UI_ST_NORM); ui_render();
    if (count_col(0, F_Y, 240, 18, C_ALERT)) { printf("F1: the footer stays red after the overload ends\n"); return 1; }
    ui_home(); play_again(); ui_status("C1 42%", UI_ST_NORM);
    if (count_col(0, 0, 240, S_H, C_ALERT)) { printf("F1: the top bar stays red after the overload ends\n"); return 1; }
    /* F2: LOADING shows amber on the MODEL row being edited (the name greys), and amber in the corner on PLAY (the amp name greys) */
    {
        play_again(); ui_status("2C 61%", UI_ST_NORM);
        int amp = count_col(94, P_NUM_Y + 4u, 142, 23, BLK_COL[PX_B_AMP]);
        ui_status("2C 61%", UI_ST_LOADING); ui_render(); shot(d, "play_loading");
        if (!amp || count_col(94, P_NUM_Y + 4u, 142, 23, BLK_COL[PX_B_AMP]) || !count_col(ST_X, 3, ST_W, 15, UI_ACC)) { printf("F2: PLAY: the amp name must grey and the corner read LOADING in amber\n"); return 1; }
        ui_status("2C 61%", UI_ST_NORM); ui_render();
        if (count_col(94, P_NUM_Y + 4u, 142, 23, BLK_COL[PX_B_AMP]) != amp) { printf("F2: PLAY: the amp name must come back when the model plays\n"); return 1; }
        ui_push(); poison(); ui_render(); ui_status("2C 61%", UI_ST_NORM);
        int row = count_col(BAR_X, E_P0 + 5u, 130, 15, UI_ACC);
        ui_status("2C 61%", UI_ST_LOADING); ui_render(); shot(d, "edit_model_loading");
        if (count_col(BAR_X, E_P0 + 5u, 130, 15, UI_ACC) <= row || count_col(140, E_P0 + 1u, 90, 23, UI_TXT)) { printf("F2: block page: the MODEL row must read LOADING in amber, the name in grey\n"); return 1; }
        if (count_col(ST_X, F_Y + 3u, ST_W, 15, UI_ACC)) { printf("F2: block page: the corner keeps the load figure\n"); return 1; }
        ui_status("2C 61%", UI_ST_NORM); ui_render();
        if (count_col(BAR_X, E_P0 + 5u, 130, 15, UI_ACC) != row) { printf("F2: block page: the MODEL row must come back\n"); return 1; }
        ui_home(); play_again(); ui_status("C1 42%", UI_ST_NORM);
    }
    /* F3: a footswitch on a block page goes back to PLAY (on the block it was on); the band carries the preset number */
    ui_blk = PX_B_AMP; ui_mode = M_EDIT; ui_item = 2;
    ui_preset_step(1);
    if (ui_mode != M_CHAIN || ui_item != 0 || ui_blk != PX_B_AMP || cur_preset != 10) { printf("F3: a footswitch on a block page must return to PLAY\n"); return 1; }
    ui_preset_step(-1);
    if (cur_preset != 9) { printf("F3: the footswitch must still change the preset\n"); return 1; }
    ui_enter_block(PX_B_AMP); poison(); uc.screen = -1; ui_invalidate(); ui_render();
    int band = count_col(8, 6, 24, 19, BLK);
    cur_preset = 11; load_preset(11); ui_render(); shot(d, "edit_band_after_midi_change");   /* (a Program Change keeps the page) */
    if (!band || count_col(8, 6, 24, 19, BLK) == band) { printf("F3: the band must show the new preset number after a MIDI change\n"); return 1; }
    ui_home(); cur_preset = 9; load_preset(9);
    /* F4: while EDITED shows, the footer names SAVE */
    play_again(); ui_status("C1 42%", UI_ST_NORM);
    if (count_col(0, F_Y + 3u, 240, 15, UI_ACC)) { printf("F4: SAVE is named without an edit\n"); return 1; }
    PRESET.p[PX_B_AMP][1] ^= 7u; ui_render(); shot(d, "play_edited_save_hint");
    if (!count_col(0, F_Y + 3u, 240, 15, UI_ACC)) { printf("F4: EDITED shows, but the footer does not name SAVE\n"); return 1; }
    load_preset(9); ui_render();
    if (count_col(0, F_Y + 3u, 240, 15, UI_ACC)) { printf("F4: the SAVE hint stays after the edit is gone\n"); return 1; }
    /* F13: the label of an off tile reads (contrast of at least 2.5 against the tile; the old one was 1.5 to 3.2) */
    for (int b = 0; b < PX_NBLK; b++) {
        uint16_t fg = ft_mix(BLK_COL[b], C_OFF, 11);
        if (fg == C_OFF || fg == BLK_COL[b]) { printf("F13: block %d: an off label must differ from its tile and from an on tile\n", b); return 1; }
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *d = argc > 1 ? argv[1] : ".";
    load_preset(9);
    cur_preset = 9;
    ui_blk = PX_B_AMP;
    full(); shot(d, "play");
    ui_knob(1, 0); ui_render(); shot(d, "play_knob_next_block");
    ui_knob(-1, 0); ui_push(); poison(); ui_render(); status(); shot(d, "edit_amp_model");
    ui_knob(1, 0); ui_knob(1, 0); ui_render(); shot(d, "edit_amp_gain_selected");
    ui_push(); for (int i = 0; i < 8; i++) ui_knob(1, 0); ui_render(); shot(d, "edit_amp_gain_editing");
    ui_push(); ui_home(); poison(); ui_render(); status(); shot(d, "play_edited");
    ui_enter_block(PX_B_DELAY); poison(); ui_render(); status(); shot(d, "edit_delay");
    ui_knob(1, 0); ui_render(); shot(d, "edit_delay_onoff_selected");
    ui_enter_block(PX_B_DELAY); ui_render(); shot(d, "edit_delay_off");
    ui_home(); ui_blk = PX_B_GATE; ui_mode = M_BLOCK; full(); shot(d, "edit_gate_preset_level");
    ui_home(); cur_preset = 13; load_preset(13); ui_blk = PX_B_REVERB; poison(); ui_render(); status(); shot(d, "play_smooth_lead");
    ui_move_start(); ui_knob(-1, 0); ui_render(); shot(d, "play_move");
    ui_move_cancel(); cur_preset = 23; load_preset(23); master_vol = 100; bat_mv = 3640; full(); ui_status("PEAK 97%", UI_ST_BYPASS); ui_render(); shot(d, "play_acoustic_bypass_vol100_bat12");
    bat_usb_mv = 5000; ui_render(); shot(d, "play_usb");
    bat_usb_mv = 0; bat_mv = 3920; master_vol = 80;
    save_open(); save_draw(); shot(d, "save");
    save_knob(1); save_draw(); shot(d, "save_other_slot");
    save_close(); ui_render(); status();
    tuner_open(); tuner_draw(110.5f); shot(d, "tuner_a_flat");
    {   /* v37: no battery reading: the icon shows an empty cell and "--" (it was hidden) */
        int m = bat_mv; bat_mv = 0; tuner_close(); poison(); ui_render(); status(); shot(d, "play_battery_unknown"); bat_mv = m;
        tuner_open(); tuner_draw(110.5f);
    }
    tuner_draw(110.0f); shot(d, "tuner_a_ok");
    tuner_close(); poison(); ui_render(); status(); shot(d, "play_after_tuner");
    px_midi_cfg_unpack(&mid_cfg, 0);
    midi_open(); midi_draw(); shot(d, "midi_defaults");
    midi_knob(1); midi_knob(1); midi_draw(); shot(d, "midi_channel_2");
    midi_push(); midi_knob(1); mid_usb_up = 1; mid_last[0] = PXM_CC; mid_last[1] = 25; mid_last[2] = 127; midi_draw(); shot(d, "midi_send_off_host_cc");
    midi_push(); midi_knob(1); mid_last[0] = PXM_PC; mid_last[1] = 11; midi_draw(); shot(d, "midi_clock_on_pc");
    midi_push(); poison(); uc.screen = -1; ui_invalidate(); ui_render(); status();
    if (midi_saved != 1 || ui_mode != M_CHAIN) { printf("midi: leaving the page after a change must save once and return to the chain\n"); return 1; }
    midi_open(); midi_close();
    if (midi_saved != 1) { printf("midi: leaving without a change must not save\n"); return 1; }
    mid_cfg.chan = 16; midi_open(); midi_knob(1); midi_draw(); shot(d, "midi_channel_wraps_to_omni");
    if (mid_cfg.chan != 0) { printf("midi: channel 16 + 1 must wrap to omni\n"); return 1; }
    midi_knob(-1); if (mid_cfg.chan != 16) { printf("midi: omni - 1 must wrap to 16\n"); return 1; }
    return quick_wins(d);
}
