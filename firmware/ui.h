/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket X — UI v3: "chain + block page", anti-aliased Inter font, NO flicker. All on-screen text in English.
 *
 *  +--------------------------+  header: preset number + name (bold), volume
 *  | 05 BRIT JANGLE   VOL 80  |
 *  +--------------------------+  chain: 11 tiles (on = block colour, off = grey)
 *  | GT  CMP PIT SFX DRV AMP  |
 *  | CAB EQ  MOD DLY REV      |
 *  +--------------------------+  block page: model (bold) + ON/OFF + up to 5 knobs as bars
 *  | AMP                 ON   |
 *  | <      AC 30 TB       >  |
 *  | GAIN     ##########  45  |
 *  +--------------------------+  help line + DSP load
 *  | KNOB: BLOCK  PUSH: OPEN  |
 *  +--------------------------+
 * No flicker: the screen is never cleared. Every element is a fixed-size box redrawn in one pixel stream
 * (background included: ft_box; a bar = filled part + empty part), and only when it changed.
 *
 * Controls: KNOB = select / adjust; PUSH = open / edit / confirm; HOME = back to the chain (long press = tuner);
 * FOOTSWITCHES = next / previous preset; AMP CAB DLY REV MOD = jump to that block (press again = on/off);
 * FX = cycles DRV, SFX, PIT, CMP, GT, EQ. Hold PUSH = SAVE (the preset into one of the 42 slots, kept in flash).
 */
#pragma once

enum { M_CHAIN, M_BLOCK, M_EDIT, M_TUNER, M_SAVE };
static uint32_t st_gen;                 /* bumped on each save: the header's "*" reference reloads */
static void st_get(uint32_t k, px_preset_t *p);
static int st_save(uint32_t k, const px_preset_t *p);
static int st_reset(uint32_t k);
static int st_saved(uint32_t k);
static void save_draw(void);
static uint32_t ui_mode = M_CHAIN, ui_blk = PX_B_AMP, ui_item = 0;

#define C_OFF  0x2124
#define C_LINE 0x2945
static const uint16_t BLK_COL[PX_NBLK] = {
    0x5D9F, 0x5D9F,   /* GT, CMP: blue (dynamics) */
    0xC31F,           /* PIT: purple */
    0xFB1A,           /* SFX: pink */
    0xFE00,           /* DRV: yellow */
    0xFB20,           /* AMP: orange */
    0xCC4A,           /* CAB: beige */
    0x6FE8,           /* EQ: light green */
    0x7D5F,           /* MOD: lavender */
    0x47FF,           /* DLY: cyan */
    0x8FE0,           /* REV: green */
};

/* geometry (font heights: FT_SM 15 px, FT_MD 23 px) */
#define H_Y      4u                    /* header */
#define T_Y0     36u                   /* 1st row of tiles */
#define T_DY     32u
#define T_COLS   6
#define T_PITCH  39u
#define T_W      35u
#define T_H      26u
#define D_Y0     100u                  /* block name + ON/OFF */
#define D_TYPE   118u                  /* type (bold) */
#define D_P0     144u                  /* 1st parameter row */
#define D_ROW    16u
#define F_Y      225u                  /* footer */
#define BAR_X    78u
#define BAR_W    114u

static void num3(char *b, uint32_t v)  /* number without leading zeros */
{
    int i = 0;
    if (v >= 100u) b[i++] = (char)('0' + v / 100u);
    if (v >= 10u) b[i++] = (char)('0' + (v / 10u) % 10u);
    b[i++] = (char)('0' + v % 10u); b[i] = 0;
}

/* ---- cache of what is on screen ---- */
static struct {
    int full;
    uint32_t preset, vol; char name[14];
    uint8_t tile_on[PX_NBLK], tile_sel[PX_NBLK];
    int blk, type, np, item, mode, on;
    uint8_t val[PX_NPAR];
    int hint, dirty;
} uc;
static void ui_invalidate(void) { uc.full = 1; }

/* ---- header ---- */
static void draw_header(int force)
{
    if (force || uc.preset != cur_preset) {
        char b[4]; uint32_t n = cur_preset + 1u;
        b[0] = (char)('0' + n / 10u); b[1] = (char)('0' + n % 10u); b[2] = 0;
        ft_box(4, H_Y, 30, b, &FT_MD, UI_ACC, UI_BG, 0);
        uc.preset = cur_preset;
    }
    int nchg = force;
    for (int i = 0; i < 14; i++) if (uc.name[i] != PRESET.name[i]) nchg = 1;
    if (nchg) {
        ft_box(34, H_Y, 144, PRESET.name, &FT_MD, UI_TXT, UI_BG, 0);
        for (int i = 0; i < 14; i++) uc.name[i] = PRESET.name[i];
    }
    {   /* "*": the preset differs from what its slot holds (hold PUSH to save) */
        static px_preset_t ref;
        static uint32_t ref_slot = ~0u, ref_gen = ~0u;
        if (ref_slot != cur_preset || ref_gen != st_gen) { st_get(cur_preset, &ref); ref_slot = cur_preset; ref_gen = st_gen; }
        int d = memcmp(&ref, &PRESET, sizeof ref) != 0;
        if (force || d != uc.dirty) { ft_box(178, H_Y, 12, d ? "*" : "", &FT_MD, UI_ACC, UI_BG, 0); uc.dirty = d; }
    }
    if (force || uc.vol != master_vol) {
        char b[10] = "VOL "; num3(b + 4, master_vol);
        ft_box(190, H_Y + 4u, 48, b, &FT_SM, UI_DIM, UI_BG, 2);
        uc.vol = master_vol;
    }
}

/* ---- chain tiles ---- */
static uint32_t tile_x(int i) { return 4u + (uint32_t)(i % T_COLS) * T_PITCH; }
static uint32_t tile_y(int i) { return T_Y0 + (uint32_t)(i / T_COLS) * T_DY; }
static void draw_tile(int i)
{
    uint32_t x = tile_x(i), y = tile_y(i);
    int on = PRESET.on[i] != 0, sel = (i == (int)ui_blk);
    uint16_t bg = on ? BLK_COL[i] : C_OFF, fg = on ? BLK : UI_DIM;
    uint16_t ol = sel ? WHT : UI_BG;
    lcd_rect(x - 1u, y - 1u, T_W + 2u, 1, ol); lcd_rect(x - 1u, y + T_H, T_W + 2u, 1, ol);   /* 1 px outline */
    lcd_rect(x - 1u, y, 1, T_H, ol);           lcd_rect(x + T_W, y, 1, T_H, ol);
    lcd_rect(x, y, T_W, 5, bg); lcd_rect(x, y + 20u, T_W, T_H - 20u, bg);                  /* background around the text */
    ft_box(x, y + 5u, T_W, px_block_label(i), &FT_SM, fg, bg, 1);
    lcd_rect(x, y + T_H + 2u, T_W, 2, sel ? WHT : UI_BG);                                  /* underline below the selected tile */
    uc.tile_on[i] = (uint8_t)on; uc.tile_sel[i] = (uint8_t)sel;
}
static void draw_tiles(int force)
{
    for (int i = 0; i < PX_NBLK; i++) {
        int on = PRESET.on[i] != 0, sel = (i == (int)ui_blk);
        if (force || uc.tile_on[i] != on || uc.tile_sel[i] != sel) draw_tile(i);
    }
}

/* ---- block page ---- */
static int ui_np(void) { return px_nparams((int)ui_blk, PRESET.type[ui_blk]); }
static int ui_nitems(void) { return 2 + ui_np(); }     /* 0 = type, 1 = on/off, 2.. = parameters */

static void draw_onoff(void)
{
    int sel = (ui_mode != M_CHAIN && ui_item == 1u);
    uint16_t bg = sel ? UI_PANEL : UI_BG;
    int on = PRESET.on[ui_blk] != 0;
    ft_box(186, D_Y0, 50, on ? "ON" : "OFF", &FT_SM, on ? BLK_COL[ui_blk] : UI_DIM, bg, 1);
}
static void draw_type(void)
{
    int sel = (ui_mode != M_CHAIN && ui_item == 0u), ed = sel && ui_mode == M_EDIT;
    uint16_t bg = sel ? UI_PANEL : UI_BG, fg = ed ? UI_ACC : UI_TXT;
    ft_box(0, D_TYPE, 22, sel ? "<" : "", &FT_MD, UI_ACC, bg, 1);
    ft_box(22, D_TYPE, 196, px_type_name((int)ui_blk, PRESET.type[ui_blk]), &FT_MD, fg, bg, 1);
    ft_box(218, D_TYPE, 22, sel ? ">" : "", &FT_MD, UI_ACC, bg, 1);
}
static void draw_bar(int k)            /* bar + number: the only thing that moves while adjusting */
{
    uint32_t y = D_P0 + (uint32_t)k * D_ROW;
    int sel = (ui_mode != M_CHAIN && (int)ui_item == k + 2), ed = sel && ui_mode == M_EDIT;
    uint16_t bg = sel ? UI_PANEL : UI_BG;
    uint32_t v = PRESET.p[ui_blk][k]; if (v > 100u) v = 100u;
    uint32_t w = v * BAR_W / 100u;
    if (w) lcd_rect(BAR_X, y + 4u, w, 7, ed ? UI_ACC : BLK_COL[ui_blk]);
    if (w < BAR_W) lcd_rect(BAR_X + w, y + 4u, BAR_W - w, 7, C_OFF);
    char b[8];
    if (!px_param_text((int)ui_blk, PRESET.type[ui_blk], k, (int)v, b)) num3(b, v);   /* "+5TH", "440"... */
    ft_box(196, y, 40, b, &FT_SM, ed ? UI_ACC : UI_TXT, bg, 2);
    uc.val[k] = (uint8_t)v;
}
static void draw_row(int k)            /* one full parameter row, without a global clear */
{
    uint32_t y = D_P0 + (uint32_t)k * D_ROW;
    if (k >= ui_np()) { lcd_rect(0, y, 240, D_ROW, UI_BG); return; }
    int sel = (ui_mode != M_CHAIN && (int)ui_item == k + 2);
    uint16_t bg = sel ? UI_PANEL : UI_BG;
    lcd_rect(0, y, 6, 15, bg);
    ft_box(6, y, 70, px_param_name((int)ui_blk, PRESET.type[ui_blk], k), &FT_SM, sel ? WHT : UI_TXT, bg, 0);
    lcd_rect(76, y, 2, 15, bg);
    lcd_rect(BAR_X, y, BAR_W, 4, bg); lcd_rect(BAR_X, y + 11u, BAR_W, 4, bg);
    lcd_rect(BAR_X + BAR_W, y, 196u - (BAR_X + BAR_W), 15, bg);
    lcd_rect(236, y, 4, 15, bg);
    lcd_rect(0, y + 15u, 240, 1, UI_BG);
    draw_bar(k);
}
static void draw_item(int it)
{
    if (it == 0) draw_type();
    else if (it == 1) draw_onoff();
    else draw_row(it - 2);
}
static void draw_detail(int force)
{
    int type = PRESET.type[ui_blk], np = ui_np(), on = PRESET.on[ui_blk] != 0;
    if (force || uc.blk != (int)ui_blk || uc.type != type) {
        ft_box(6, D_Y0, 180, px_block_name((int)ui_blk), &FT_SM, UI_DIM, UI_BG, 0);
        draw_onoff(); draw_type();
        for (int k = 0; k < PX_NPAR; k++) draw_row(k);
        uc.blk = (int)ui_blk; uc.type = type; uc.np = np; uc.item = (int)ui_item; uc.mode = (int)ui_mode; uc.on = on;
        return;
    }
    if (uc.item != (int)ui_item || uc.mode != (int)ui_mode) {   /* highlight moved: 2 rows only */
        int old = uc.item;
        uc.item = (int)ui_item; uc.mode = (int)ui_mode;
        if (old != (int)ui_item) draw_item(old);
        draw_item((int)ui_item);
    }
    if (uc.on != on) { draw_onoff(); uc.on = on; }
    for (int k = 0; k < np; k++) if (uc.val[k] != PRESET.p[ui_blk][k]) draw_bar(k);
}

/* ---- footer: help (the DSP load is drawn by the main loop, on the right) ---- */
static void draw_hint(int force)
{
    int h = (int)ui_mode;
    if (!force && uc.hint == h) return;
    static const char *H[3] = {"KNOB: BLOCK   PUSH: OPEN", "PUSH: EDIT   HOME: BACK", "KNOB: VALUE   PUSH: OK"};
    ft_box(4, F_Y, 174, H[h < 3 ? h : 0], &FT_SM, UI_DIM, UI_PANEL, 0);
    uc.hint = h;
}

/* ---- render: sends only what changed to the screen ---- */
static void ui_render(void)
{
    if (ui_mode == M_TUNER) return;
    if (ui_mode == M_SAVE) { save_draw(); return; }
    int f = uc.full;
    if (f) {                                                     /* page change: background drawn only once */
        lcd_rect(0, 0, 240, H_Y, UI_BG); lcd_rect(0, H_Y + 23u, 240, 3, UI_BG);
        lcd_rect(190, H_Y, 50, 4, UI_BG); lcd_rect(190, H_Y + 19u, 50, 4, UI_BG); lcd_rect(0, H_Y, 4, 23, UI_BG);
        lcd_rect(0, 30, 240, 1, C_LINE);
        lcd_rect(0, 31, 240, 4, UI_BG);
        lcd_rect(0, 35, 3, 2u * T_DY, UI_BG); lcd_rect(235, 35, 5, 2u * T_DY, UI_BG);
        lcd_rect(tile_x(PX_NBLK) - 1u, tile_y(PX_NBLK) - 1u, 240u - (tile_x(PX_NBLK) - 1u), T_H + 4u, UI_BG);   /* empty cell */
        lcd_rect(0, T_Y0 + T_DY + T_H + 1u, 240, 1, UI_BG);          /* row 95: between the tiles and their underline */
        lcd_rect(0, D_Y0 - 3u, 240, 1, C_LINE); lcd_rect(0, D_Y0 - 2u, 240, 2, UI_BG);
        lcd_rect(0, D_Y0, 6, 15, UI_BG); lcd_rect(0, D_Y0 + 15u, 240, D_TYPE - D_Y0 - 15u, UI_BG);
        lcd_rect(0, D_TYPE + 23u, 240, D_P0 - D_TYPE - 23u, UI_BG);
        lcd_rect(0, 224, 240, 1, UI_PANEL); lcd_rect(0, F_Y, 4, 15, UI_PANEL);
        uc.full = 0;
    }
    draw_header(f);
    draw_tiles(f);
    draw_detail(f);
    draw_hint(f);
}

/* ---- actions ---- */
static void ui_enter_block(uint32_t b)
{
    if (ui_blk == b && ui_mode != M_CHAIN) PRESET.on[b] ^= 1u;   /* 2nd press on the same button: on/off */
    ui_blk = b; ui_mode = M_BLOCK; ui_item = 0;
}
static void ui_knob(int dd, int fast)
{
    if (ui_mode == M_CHAIN) {
        ui_blk = (uint32_t)(((int)ui_blk + PX_NBLK + dd) % PX_NBLK);
    } else if (ui_mode == M_BLOCK) {
        int n = ui_nitems();
        ui_item = (uint32_t)(((int)ui_item + n + dd) % n);
    } else if (ui_mode == M_EDIT) {
        if (ui_item == 0u) {
            int nt = px_ntypes((int)ui_blk);
            PRESET.type[ui_blk] = (uint8_t)((PRESET.type[ui_blk] + nt + dd) % nt);
        } else if (ui_item >= 2u) {
            int k = (int)ui_item - 2, v = PRESET.p[ui_blk][k] + dd * (fast ? 5 : 1);
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            PRESET.p[ui_blk][k] = (uint8_t)v;
        }
    }
}
static void ui_push(void)
{
    if (ui_mode == M_CHAIN) { ui_mode = M_BLOCK; ui_item = 0; }
    else if (ui_mode == M_BLOCK) {
        if (ui_item == 1u) PRESET.on[ui_blk] ^= 1u;
        else ui_mode = M_EDIT;
    } else ui_mode = M_BLOCK;
}
static void ui_home(void) { ui_mode = M_CHAIN; ui_item = 0; }

/* ===================================================== save page ======== */
/* hold PUSH: SAVE. KNOB picks the slot (01..42, then "FACTORY": this slot back to its factory preset), PUSH saves,
 * HOME cancels. Saving to another slot copies the sound there and moves to it. */
#define SV_FACTORY 42u
static uint32_t sv_dest, sv_shown = ~0u;
static void save_open(void)
{
    ui_mode = M_SAVE; sv_dest = cur_preset; sv_shown = ~0u;
    lcd_rect(0, 0, 240, 240, UI_BG);
    ft_box(0, 14, 240, "SAVE PRESET", &FT_MD, UI_ACC, UI_BG, 1);
    ft_box(0, 46, 240, PRESET.name, &FT_SM, UI_DIM, UI_BG, 1);
    lcd_rect(20, 76, 200, 1, C_LINE);
    ft_box(0, 196, 240, "KNOB: SLOT   PUSH: SAVE", &FT_SM, UI_DIM, UI_BG, 1);
    ft_box(0, 214, 240, "HOME: CANCEL", &FT_SM, UI_DIM, UI_BG, 1);
}
static void save_draw(void)
{
    if (sv_shown == sv_dest) return;
    sv_shown = sv_dest;
    char b[20];
    if (sv_dest == SV_FACTORY) {
        ft_box(0, 92, 240, "RESTORE FACTORY", &FT_MD, UI_TXT, UI_PANEL, 1);
        b[0] = 'S'; b[1] = 'L'; b[2] = 'O'; b[3] = 'T'; b[4] = ' ';
        b[5] = (char)('0' + (cur_preset + 1u) / 10u); b[6] = (char)('0' + (cur_preset + 1u) % 10u); b[7] = 0;
        ft_box(0, 124, 240, b, &FT_SM, UI_DIM, UI_BG, 1);
        ft_box(0, 146, 240, st_saved(cur_preset) ? "BACK TO ITS FACTORY SOUND" : "(ALREADY THE FACTORY SOUND)", &FT_SM, UI_DIM, UI_BG, 1);
        return;
    }
    px_preset_t d;
    st_get(sv_dest, &d);
    b[0] = 'T'; b[1] = 'O'; b[2] = ' '; b[3] = (char)('0' + (sv_dest + 1u) / 10u); b[4] = (char)('0' + (sv_dest + 1u) % 10u); b[5] = 0;
    ft_box(0, 92, 240, b, &FT_MD, UI_TXT, UI_PANEL, 1);
    ft_box(0, 124, 240, sv_dest == cur_preset ? "(THIS SLOT)" : "REPLACES:", &FT_SM, UI_DIM, UI_BG, 1);
    ft_box(0, 146, 240, sv_dest == cur_preset ? "" : d.name, &FT_SM, UI_TXT, UI_BG, 1);
}
static void save_close(void)
{
    ui_mode = M_CHAIN; ui_item = 0;
    lcd_rect(0, 0, 240, 240, UI_BG); lcd_rect(0, 224, 240, 16, UI_PANEL);
    ui_invalidate();
}
static void save_knob(int dd)
{
    sv_dest = (sv_dest + SV_FACTORY + 1u + (uint32_t)(dd > 0 ? 1 : SV_FACTORY)) % (SV_FACTORY + 1u);
}
static void save_confirm(void)
{
    int rc;
    ft_box(0, 168, 240, "SAVING...", &FT_SM, UI_ACC, UI_BG, 1);
    if (sv_dest == SV_FACTORY) { rc = st_reset(cur_preset); }
    else { rc = st_save(sv_dest, &PRESET); if (!rc) cur_preset = sv_dest; }
    st_gen++;
    if (!rc) load_preset(cur_preset);
    {
        char b[16] = "ERROR -";
        if (rc) { b[7] = (char)('0' + (uint32_t)(-rc) % 10u); b[8] = 0; }
        ft_box(0, 168, 240, rc ? b : "SAVED", &FT_MD, rc ? RED : GRN, UI_BG, 1);
    }
    for (int i = 0; i < 7; i++) { fm1_wdt_feed(); fm1_delay_ms(100); }
    save_close();
}
static void ui_preset_step(int dd)
{
    int n = px_preset_count();
    cur_preset = (uint32_t)(((int)cur_preset + n + dd) % n);
    load_preset(cur_preset);
    if (ui_mode == M_EDIT) ui_mode = M_BLOCK;
    if ((int)ui_item >= ui_nitems()) ui_item = 0;
}
static void ui_fx_cycle(void)          /* FX: DRV -> SFX -> PIT -> CMP -> GT -> EQ */
{
    static const uint8_t FXB[6] = {PX_B_DRIVE, PX_B_SFX, PX_B_PITCH, PX_B_COMP, PX_B_GATE, PX_B_EQ};
    uint32_t nx = PX_B_DRIVE;
    for (int i = 0; i < 6; i++) if (FXB[i] == ui_blk) nx = FXB[(i + 1) % 6];
    ui_blk = nx; ui_mode = M_BLOCK; ui_item = 0;
}

/* ===================================================== tuner ======== */
static const char *NOTE_N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static int tn_last_note = -2, tn_last_x = -1, tn_last_c = 1000, tn_last_lv = 0x7FFF;
/* "IN L -82  R -84 dB": input level of each I2S channel, dB below full scale (24-bit, v26 diagnostic) */
static int lv_str(char *b, int v)
{
    int i = 0;
    if (v <= -199) { b[0] = '-'; b[1] = '-'; b[2] = '-'; return 3; }
    if (v < 0) { b[i++] = '-'; v = -v; }
    char t[4]; num3(t, (uint32_t)v);
    for (int k = 0; t[k]; k++) b[i++] = t[k];
    return i;
}
static void tuner_level(void)
{
    int l = a_level_db(0), r = a_level_db(1);
    if (((l & 0xFF) | ((r & 0xFF) << 8)) == tn_last_lv) return;
    tn_last_lv = (l & 0xFF) | ((r & 0xFF) << 8);
    char b[28]; int i = 0;
    b[i++] = 'I'; b[i++] = 'N'; b[i++] = ' '; b[i++] = ' '; b[i++] = 'L'; b[i++] = ' '; i += lv_str(b + i, l);
    b[i++] = ' '; b[i++] = ' '; b[i++] = ' '; b[i++] = 'R'; b[i++] = ' '; i += lv_str(b + i, r);
    b[i++] = ' '; b[i++] = 'd'; b[i++] = 'B'; b[i] = 0;
    ft_box(0, 32, 240, b, &FT_SM, UI_DIM, UI_BG, 1);
}
#define TN_BAR_Y 170u
static void tuner_open(void)
{
    ui_mode = M_TUNER;
    lcd_rect(0, 0, 240, 240, UI_BG);
    ft_box(0, 8, 240, "TUNER", &FT_MD, UI_ACC, UI_BG, 1);
    lcd_rect(20, TN_BAR_Y, 200, 10, C_OFF);
    lcd_rect(119, TN_BAR_Y - 8u, 2, 26, UI_TXT);
    ft_box(10, TN_BAR_Y + 22u, 40, "-50", &FT_SM, UI_DIM, UI_BG, 1);
    ft_box(100, TN_BAR_Y + 22u, 40, "0", &FT_SM, UI_DIM, UI_BG, 1);
    ft_box(190, TN_BAR_Y + 22u, 40, "+50", &FT_SM, UI_DIM, UI_BG, 1);
    ft_box(0, F_Y, 240, "ANY BUTTON: EXIT", &FT_SM, UI_DIM, UI_BG, 1);
    tn_last_note = -2; tn_last_x = -1; tn_last_c = 1000; tn_last_lv = 0x7FFF;
}
static void tuner_close(void) { ui_mode = M_CHAIN; ui_invalidate(); lcd_rect(0, 0, 240, 240, UI_BG); lcd_rect(0, 224, 240, 16, UI_PANEL); }
static void tuner_draw(float hz)       /* hz <= 0: no note */
{
    int note = -1, c = 0; float cents = 0.0f;
    tuner_level();
    if (hz > 20.0f) {
        float st = 12.0f * logf(hz / 440.0f) * 1.44269504f;   /* semitones from A4 = 440 Hz */
        int r = (int)(st + (st >= 0 ? 0.5f : -0.5f));
        cents = (st - (float)r) * 100.0f;
        note = ((r % 12) + 12 + 9) % 12;                      /* A = 9 */
        c = (int)(cents + (cents >= 0 ? 0.5f : -0.5f));
    }
    int ok = (note >= 0 && c > -5 && c < 5);
    if (note != tn_last_note || (note >= 0 && (ok != (tn_last_c > -5 && tn_last_c < 5)))) {
        ft_box(0, 52, 240, note < 0 ? "-" : NOTE_N[note], &FT_LG, note < 0 ? UI_DIM : (ok ? GRN : UI_TXT), UI_BG, 1);
        tn_last_note = note;
    }
    if (c != tn_last_c || note < 0) {
        char b[8] = "";
        if (note >= 0) { int a = c < 0 ? -c : c; b[0] = (char)(c < 0 ? '-' : '+'); num3(b + 1, (uint32_t)a); }
        int i = 0; while (b[i]) i++;
        if (note >= 0) { b[i++] = ' '; b[i++] = 'c'; b[i++] = 't'; b[i] = 0; }
        ft_box(60, 124, 120, b, &FT_MD, ok ? GRN : UI_TXT, UI_BG, 1);
        tn_last_c = note < 0 ? 1000 : c;
    }
    int x = -1;
    if (note >= 0) { x = 120 + c * 2; if (x < 22) x = 22; if (x > 216) x = 216; }
    if (x != tn_last_x) {                                     /* needle: erase the old one, draw the new one */
        if (tn_last_x >= 0) {
            lcd_rect((uint32_t)tn_last_x - 2u, TN_BAR_Y, 5, 10, C_OFF);
            if (tn_last_x >= 116 && tn_last_x <= 124) lcd_rect(119, TN_BAR_Y - 8u, 2, 26, UI_TXT);
        }
        if (x >= 0) lcd_rect((uint32_t)x - 2u, TN_BAR_Y, 5, 10, (x >= 110 && x <= 130) ? GRN : UI_ACC);
        tn_last_x = x;
    }
}
