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
 * FX = cycles DRV, SFX, PIT, CMP, GT, EQ.
 */
#pragma once

enum { M_CHAIN, M_BLOCK, M_EDIT, M_TUNER };
static uint32_t ui_mode = M_CHAIN, ui_blk = PX_B_AMP, ui_item = 0;

#define C_OFF  0x2124
#define C_LINE 0x2945
static const uint16_t BLK_COL[PX_NBLK] = {
    0x5D9F, 0x5D9F,   /* GT, CMP : bleu (dynamique) */
    0xC31F,           /* PIT : violet */
    0xFB1A,           /* SFX : rose */
    0xFE00,           /* DRV : jaune */
    0xFB20,           /* AMP : orange */
    0xCC4A,           /* CAB : beige */
    0x6FE8,           /* EQ : vert clair */
    0x7D5F,           /* MOD : lavande */
    0x47FF,           /* DLY : cyan */
    0x8FE0,           /* REV : vert */
};

/* géométrie (hauteurs des polices : FT_SM 15 px, FT_MD 23 px) */
#define H_Y      4u                    /* en-tête */
#define T_Y0     36u                   /* 1re rangée de tuiles */
#define T_DY     32u
#define T_COLS   6
#define T_PITCH  39u
#define T_W      35u
#define T_H      26u
#define D_Y0     100u                  /* nom du bloc + ON/OFF */
#define D_TYPE   118u                  /* type (gras) */
#define D_P0     144u                  /* 1re ligne de réglage */
#define D_ROW    16u
#define F_Y      225u                  /* pied de page */
#define BAR_X    78u
#define BAR_W    114u

static void num3(char *b, uint32_t v)  /* nombre sans zéros inutiles */
{
    int i = 0;
    if (v >= 100u) b[i++] = (char)('0' + v / 100u);
    if (v >= 10u) b[i++] = (char)('0' + (v / 10u) % 10u);
    b[i++] = (char)('0' + v % 10u); b[i] = 0;
}

/* ---- cache de ce qui est affiché ---- */
static struct {
    int full;
    uint32_t preset, vol; char name[14];
    uint8_t tile_on[PX_NBLK], tile_sel[PX_NBLK];
    int blk, type, np, item, mode, on;
    uint8_t val[PX_NPAR];
    int hint;
} uc;
static void ui_invalidate(void) { uc.full = 1; }

/* ---- en-tête ---- */
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
        ft_box(34, H_Y, 156, PRESET.name, &FT_MD, UI_TXT, UI_BG, 0);
        for (int i = 0; i < 14; i++) uc.name[i] = PRESET.name[i];
    }
    if (force || uc.vol != master_vol) {
        char b[10] = "VOL "; num3(b + 4, master_vol);
        ft_box(190, H_Y + 4u, 48, b, &FT_SM, UI_DIM, UI_BG, 2);
        uc.vol = master_vol;
    }
}

/* ---- tuiles de la chaîne ---- */
static uint32_t tile_x(int i) { return 4u + (uint32_t)(i % T_COLS) * T_PITCH; }
static uint32_t tile_y(int i) { return T_Y0 + (uint32_t)(i / T_COLS) * T_DY; }
static void draw_tile(int i)
{
    uint32_t x = tile_x(i), y = tile_y(i);
    int on = PRESET.on[i] != 0, sel = (i == (int)ui_blk);
    uint16_t bg = on ? BLK_COL[i] : C_OFF, fg = on ? BLK : UI_DIM;
    uint16_t ol = sel ? WHT : UI_BG;
    lcd_rect(x - 1u, y - 1u, T_W + 2u, 1, ol); lcd_rect(x - 1u, y + T_H, T_W + 2u, 1, ol);   /* contour 1 px */
    lcd_rect(x - 1u, y, 1, T_H, ol);           lcd_rect(x + T_W, y, 1, T_H, ol);
    lcd_rect(x, y, T_W, 5, bg); lcd_rect(x, y + 20u, T_W, T_H - 20u, bg);                  /* fond autour du texte */
    ft_box(x, y + 5u, T_W, px_block_label(i), &FT_SM, fg, bg, 1);
    lcd_rect(x, y + T_H + 2u, T_W, 2, sel ? WHT : UI_BG);                                  /* trait sous la tuile choisie */
    uc.tile_on[i] = (uint8_t)on; uc.tile_sel[i] = (uint8_t)sel;
}
static void draw_tiles(int force)
{
    for (int i = 0; i < PX_NBLK; i++) {
        int on = PRESET.on[i] != 0, sel = (i == (int)ui_blk);
        if (force || uc.tile_on[i] != on || uc.tile_sel[i] != sel) draw_tile(i);
    }
}

/* ---- page du bloc ---- */
static int ui_np(void) { return px_nparams((int)ui_blk, PRESET.type[ui_blk]); }
static int ui_nitems(void) { return 2 + ui_np(); }     /* 0 = type, 1 = on/off, 2.. = réglages */

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
static void draw_bar(int k)            /* jauge + chiffre : c'est la seule chose qui bouge en réglage */
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
static void draw_row(int k)            /* une ligne de réglage complète, sans effacement global */
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
    if (uc.item != (int)ui_item || uc.mode != (int)ui_mode) {   /* surbrillance déplacée : 2 lignes seulement */
        int old = uc.item;
        uc.item = (int)ui_item; uc.mode = (int)ui_mode;
        if (old != (int)ui_item) draw_item(old);
        draw_item((int)ui_item);
    }
    if (uc.on != on) { draw_onoff(); uc.on = on; }
    for (int k = 0; k < np; k++) if (uc.val[k] != PRESET.p[ui_blk][k]) draw_bar(k);
}

/* ---- pied de page : aide (la charge DSP est dessinée par la boucle principale, à droite) ---- */
static void draw_hint(int force)
{
    int h = (int)ui_mode;
    if (!force && uc.hint == h) return;
    static const char *H[3] = {"KNOB: BLOCK   PUSH: OPEN", "PUSH: EDIT   HOME: BACK", "KNOB: VALUE   PUSH: OK"};
    ft_box(4, F_Y, 174, H[h < 3 ? h : 0], &FT_SM, UI_DIM, UI_PANEL, 0);
    uc.hint = h;
}

/* ---- rendu : n'envoie à l'écran que ce qui a changé ---- */
static void ui_render(void)
{
    if (ui_mode == M_TUNER) return;
    int f = uc.full;
    if (f) {                                                     /* changement de page : fond posé une seule fois */
        lcd_rect(0, 0, 240, H_Y, UI_BG); lcd_rect(0, H_Y + 23u, 240, 3, UI_BG);
        lcd_rect(190, H_Y, 50, 4, UI_BG); lcd_rect(190, H_Y + 19u, 50, 4, UI_BG); lcd_rect(0, H_Y, 4, 23, UI_BG);
        lcd_rect(0, 30, 240, 1, C_LINE);
        lcd_rect(0, 31, 240, 4, UI_BG);
        lcd_rect(0, 35, 3, 2u * T_DY, UI_BG); lcd_rect(235, 35, 5, 2u * T_DY, UI_BG);
        lcd_rect(tile_x(PX_NBLK) - 1u, tile_y(PX_NBLK) - 1u, 240u - (tile_x(PX_NBLK) - 1u), T_H + 4u, UI_BG);   /* case vide */
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
    if (ui_blk == b && ui_mode != M_CHAIN) PRESET.on[b] ^= 1u;   /* 2e appui sur le même bouton : on/off */
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
static void ui_preset_step(int dd)
{
    int n = px_preset_count();
    cur_preset = (uint32_t)(((int)cur_preset + n + dd) % n);
    load_preset(cur_preset);
    if (ui_mode == M_EDIT) ui_mode = M_BLOCK;
    if ((int)ui_item >= ui_nitems()) ui_item = 0;
}
static void ui_fx_cycle(void)          /* FX : DRV -> SFX -> PIT -> CMP -> GT -> EQ */
{
    static const uint8_t FXB[6] = {PX_B_DRIVE, PX_B_SFX, PX_B_PITCH, PX_B_COMP, PX_B_GATE, PX_B_EQ};
    uint32_t nx = PX_B_DRIVE;
    for (int i = 0; i < 6; i++) if (FXB[i] == ui_blk) nx = FXB[(i + 1) % 6];
    ui_blk = nx; ui_mode = M_BLOCK; ui_item = 0;
}

/* ===================================================== accordeur ======== */
static const char *NOTE_N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static int tn_last_note = -2, tn_last_x = -1, tn_last_c = 1000;
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
    tn_last_note = -2; tn_last_x = -1; tn_last_c = 1000;
}
static void tuner_close(void) { ui_mode = M_CHAIN; ui_invalidate(); lcd_rect(0, 0, 240, 240, UI_BG); lcd_rect(0, 224, 240, 16, UI_PANEL); }
static void tuner_draw(float hz)       /* hz <= 0 : pas de note */
{
    int note = -1, c = 0; float cents = 0.0f;
    if (hz > 20.0f) {
        float st = 12.0f * logf(hz / 440.0f) * 1.44269504f;   /* demi-tons depuis A4 = 440 Hz */
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
    if (x != tn_last_x) {                                     /* aiguille : effacer l'ancienne, poser la nouvelle */
        if (tn_last_x >= 0) {
            lcd_rect((uint32_t)tn_last_x - 2u, TN_BAR_Y, 5, 10, C_OFF);
            if (tn_last_x >= 116 && tn_last_x <= 124) lcd_rect(119, TN_BAR_Y - 8u, 2, 26, UI_TXT);
        }
        if (x >= 0) lcd_rect((uint32_t)x - 2u, TN_BAR_Y, 5, 10, (x >= 110 && x <= 130) ? GRN : UI_ACC);
        tn_last_x = x;
    }
}
