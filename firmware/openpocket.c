/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket X — Jalon 1 (boot + ecran) — Donner Pocket X.
 * ECRAN (reverse-engineere du firmware d'origine) : SPI LOGICIEL sur le PORT B
 *   CS = PB1, D/C = PB5, CLK = PB6, DO = PB7   (+ PC7 pilote, reset/retroeclairage ?)
 * (la FM-1 utilisait le port C : c'etait la cause de l'ecran noir.)
 * Sequence d'init + offset 40 lignes : extraits exacts du firmware Pocket X.
 * Filet : 2 boots rates -> UBOOT (re-flashable), + backup fm1-1.bin.
 */
#include <stdint.h>
#define AUDIO_PROBE 0      /* 1 = test audio (trouver IN/OUT sur l'I2S) au lieu de l'UI */
#define DSP_CPU1 1         /* 1 = moteur DSP sur le 2e cœur (repli automatique sur CPU0 en cas de souci) */
#if DSP_CPU1 && !AUDIO_PROBE
#define FM1_CPU1 1         /* fm1_irq.h confie les fautes du 2e cœur à fm1_cpu1.h */
#define FM1_CPU1_FALLBACK 1
#else
#undef DSP_CPU1
#define DSP_CPU1 0
#endif
#define FELUCCA_ID "FM-1_900"
#include "fm1_time.h"
#include "fm1_sys.h"
#include "fm1_irq.h"
#include "fm1_guard.h"
#if DSP_CPU1
#include "fm1_cpu1.h"
#endif
#include "font8x8.h"
#include "libc.c"           /* memset / memcpy / memcmp (felucca-bsp, MPL-2.0): also used by the JieLi I2S driver */

/* ---- GPIO port B/C (base 0x50000 + port*0x40 ; OUT +0x00, DIR +0x08 ; DIR 0=sortie) ---- */
#define PB_OUT (*(volatile uint32_t *)0x50040u)
#define PB_DIR (*(volatile uint32_t *)0x50048u)
#define PC_OUT (*(volatile uint32_t *)0x50080u)
#define PC_DIR (*(volatile uint32_t *)0x50088u)
#define CS  (1u << 1)   /* PB1 */
#define DC  (1u << 5)   /* PB5 */
#define CLK (1u << 6)   /* PB6 */
#define DO  (1u << 7)   /* PB7 */
#define PC7 (1u << 7)

#define PX_ROW_OFFSET 0u
#define C(c) ((uint16_t)(((c) >> 8) | ((c) << 8)))
enum { RED=0xF800, GRN=0x07E0, BLU=0x001F, WHT=0xFFFF, BLK=0x0000, CYN=0x07FF, YEL=0xFFE0, ORG=0xFD20 };

extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
extern uint32_t _rt_start[], _rt_end[], _rt_load[];
static volatile struct { uint32_t magic, pending, failed; } guard __attribute__((section(".noinit")));

static uint32_t ms_now(void) { return (uint32_t)(fm1_ticks() / (FM1_TICKS_PER_US * 1000u)); }

/* ---- liaison écran : SPI1 MATERIEL + DMA (LCD_HW 1) ou SPI logiciel (LCD_HW 0, l'ancien code, filet) ----
 * PB6 = SPI1_CLK(A), PB7 = SPI1_DO(A) d'après les datasheets AC791x du SDK JieLi ; IOMAP_CON1 bit 4 = 0 -> groupe A
 * (comme SPI1_PLATFORM_DATA du SDK). PB5 (D/C) reste un GPIO, comme le D/C de la FM-1 sur la broche DI du groupe B.
 * Registres SPI1 et réglage CON = 0x4021 : pilote écran de Felucca (bsp/boards/fm1/fm1_lcd_hw.h). */
#ifndef LCD_HW
#define LCD_HW 1
#endif
#define LCD_BAUD 2u                     /* horloge SPI1 = LSB / (BAUD + 1) : 60 MHz / 3 = 20 MHz */
#if LCD_HW
#define LSPI_CON  (*(volatile uint32_t *)0x11D00u)
#define LSPI_BAUD (*(volatile uint32_t *)0x11D04u)
#define LSPI_BUF  (*(volatile uint32_t *)0x11D08u)
#define LSPI_ADR  (*(volatile uint32_t *)0x11D0Cu)
#define LSPI_CNT  (*(volatile uint32_t *)0x11D10u)
#define IOMAP_CON1 (*(volatile uint32_t *)0x51020u)
static uint32_t lcd_timeouts;
static int lspi_busy;                   /* un transfert DMA est en cours */
static void lspi_wait(void)
{
    uint32_t n;
    if (!lspi_busy) return;
    for (n = 0; n < 2000000u && !(LSPI_CON & 0x8000u); n++) ;
    if (n == 2000000u) lcd_timeouts++;
    LSPI_CON |= 0x4000u;                /* efface le drapeau de fin */
    lspi_busy = 0;
}
static void lspi_init(void)
{
    IOMAP_CON1 &= ~0x10u;               /* SPI1 sur le groupe A : PB6/PB7 */
    LSPI_CON = 0x4021u;
    LSPI_BAUD = LCD_BAUD;
    lspi_busy = 0;
}
static inline void spi_byte(uint8_t b)  /* un octet, attendu */
{
    lspi_wait();
    LSPI_CON |= 0x4000u;
    LSPI_BUF = b;
    lspi_busy = 1;
    lspi_wait();
}
static void lspi_dma(const void *p, uint32_t n)   /* lance n octets depuis la RAM (ne pas attendre) */
{
    lspi_wait();
    LSPI_CON |= 0x4000u;
    LSPI_ADR = (uint32_t)(uintptr_t)p;
    LSPI_CNT = n;
    lspi_busy = 1;
}
#else
static inline void spi_byte(uint8_t b)
{
    int i;
    for (i = 0; i < 8; i++) {
        if (b & 0x80u) PB_OUT |= DO; else PB_OUT &= ~DO;
        PB_OUT &= ~CLK;
        PB_OUT |= CLK;             /* front montant */
        b = (uint8_t)(b << 1);
    }
    PB_OUT &= ~CLK;
}
#endif
static void lcd_cmd(uint8_t c, const uint8_t *data, uint32_t n)
{
    PB_OUT &= ~CS;                 /* select */
    PB_OUT &= ~DC;                 /* commande */
    spi_byte(c);
    if (n) {
        uint32_t i;
        PB_OUT |= DC;              /* donnees */
        for (i = 0; i < n; i++) spi_byte(data[i]);
    }
    PB_OUT |= CS;                  /* deselect */
}

static const uint8_t PX_INIT[] = {   /* sequence EXACTE du firmware Pocket X (21 commandes) */
    0x01,0, 0x11,0,
    0x2A,4, 0x00,0x00,0x00,0xEF,
    0x2B,4, 0x00,0x28,0x01,0x17,
    0xB2,5, 0x0C,0x0C,0x0C,0x00,0x33,
    0x20,0,
    0xB7,1, 0x56, 0xBB,1, 0x18, 0xC0,1, 0x2C,
    0xC2,1, 0x01, 0xC3,1, 0x1F, 0xC4,1, 0x20, 0xC6,1, 0x0F,
    0xD0,2, 0xA6,0xA1,
    0xE0,14, 0xD0,0x0D,0x14,0x0B,0x0B,0x07,0x3A,0x44,0x50,0x08,0x13,0x13,0x2D,0x32,
    0xE1,14, 0xD0,0x0D,0x14,0x0B,0x0B,0x07,0x3A,0x44,0x50,0x08,0x13,0x13,0x2D,0x32,
    0x36,1, 0x00, 0x3A,1, 0x55, 0xE7,1, 0x00,
    0x51,1, 0xFF,                  /* luminosite max */
    0x21,0, 0x29,0,                /* INVON, DISPON */
};
static void lcd_init(void)
{
    const uint8_t *p = PX_INIT, *end = PX_INIT + sizeof PX_INIT;
    PB_DIR &= ~(CS | DC | CLK | DO);   /* sorties */
    PC_DIR &= ~PC7;
    PB_OUT |= CS; PB_OUT &= ~CLK;      /* CS haut, CLK bas */
    PC_OUT |= PC7;                     /* PC7 haut (reset relache / retroeclairage ?) */
#if LCD_HW
    lspi_init();
#endif
    fm1_delay_ms(50);
    while (p < end) {
        uint8_t cmd = *p++, len = *p++;
        lcd_cmd(cmd, p, len);
        p += len;
        if (cmd == 0x01) fm1_delay_ms(150);
        else if (cmd == 0x11) fm1_delay_ms(120);
    }
}
/* tampons de pixels pour le DMA (2 : on remplit l'un pendant que l'autre part) */
#define LBUF_PX 1024u
#if LCD_HW
static uint8_t lbuf[2][LBUF_PX * 2u] __attribute__((aligned(4)));
static uint32_t lb_i;
#endif
static uint32_t lb_n;
/* rectangle plein (coord 16 bits + offset 40 lignes) */
static void lcd_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t rgb)
{
    if (w == 0u || h == 0u || x >= 240u || y >= 240u) return;   /* v23: a negative size (huge unsigned) used to */
    if (x + w > 240u) w = 240u - x;                            /* stream 4 billion pixels = black screen forever */
    if (y + h > 240u) h = 240u - y;
    uint8_t a[4]; uint32_t i, yo = y + PX_ROW_OFFSET, x1 = x + w - 1u, y1 = yo + h - 1u, px = w * h;
    uint16_t sw = rgb;   /* envoi high-byte first, pas de swap */
    a[0]=(uint8_t)(x>>8);  a[1]=(uint8_t)x;  a[2]=(uint8_t)(x1>>8); a[3]=(uint8_t)x1;  lcd_cmd(0x2A,a,4);
    a[0]=(uint8_t)(yo>>8); a[1]=(uint8_t)yo; a[2]=(uint8_t)(y1>>8); a[3]=(uint8_t)y1;  lcd_cmd(0x2B,a,4);
    PB_OUT &= ~CS; PB_OUT &= ~DC; spi_byte(0x2C); PB_OUT |= DC;   /* RAMWR, puis donnees */
#if LCD_HW
    {   /* un tampon rempli de la couleur, renvoyé autant de fois que nécessaire (contenu constant) */
        uint8_t *b = lbuf[0];
        uint32_t fill = px < LBUF_PX ? px : LBUF_PX;
        for (i = 0; i < fill; i++) { b[2u * i] = (uint8_t)(sw >> 8); b[2u * i + 1u] = (uint8_t)sw; }
        while (px) {
            uint32_t n = px < LBUF_PX ? px : LBUF_PX;
            lspi_dma(b, n * 2u);
            px -= n;
            fm1_wdt_feed();
        }
        lspi_wait();
    }
#else
    for (i = 0; i < px; i++) {
        spi_byte((uint8_t)(sw >> 8));
        spi_byte((uint8_t)sw);
        if ((i & 0x3FFu) == 0) fm1_wdt_feed();   /* nourrir le watchdog pendant les gros remplissages */
    }
#endif
    PB_OUT |= CS;
}

/* ================= UI OpenPocket (éditeur de son embarqué) ================= */
/* palette (RGB565) façon simulateur : fond sombre + accent orange */
enum { UI_BG=0x0841, UI_PANEL=0x18E3, UI_SEL=0xFB00, UI_ACC=0xFD20,
       UI_TXT=0xEF7D, UI_DIM=0x8410, UI_OK=0x4E6B, UI_OFF=0x2124 };

/* fenêtre d'écriture + flux de pixels (CS reste bas entre les pixels) */
static void lcd_win(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    uint8_t a[4]; uint32_t yo=y+PX_ROW_OFFSET, x1=x+w-1u, y1=yo+h-1u;
    a[0]=(uint8_t)(x>>8);a[1]=(uint8_t)x;a[2]=(uint8_t)(x1>>8);a[3]=(uint8_t)x1; lcd_cmd(0x2A,a,4);
    a[0]=(uint8_t)(yo>>8);a[1]=(uint8_t)yo;a[2]=(uint8_t)(y1>>8);a[3]=(uint8_t)y1; lcd_cmd(0x2B,a,4);
    PB_OUT &= ~CS; PB_OUT &= ~DC; spi_byte(0x2C); PB_OUT |= DC;
    lb_n = 0;
}
#if LCD_HW
static void lb_flush(void)
{
    if (!lb_n) return;
    lspi_dma(lbuf[lb_i], lb_n * 2u);    /* attend le transfert précédent (l'autre tampon), puis lance celui-ci */
    lb_i ^= 1u; lb_n = 0;
}
static inline void lcd_px(uint16_t c)
{
    uint8_t *p = &lbuf[lb_i][lb_n * 2u];
    p[0] = (uint8_t)(c >> 8); p[1] = (uint8_t)c;
    if (++lb_n == LBUF_PX) lb_flush();
}
static inline void lcd_end(void){ lb_flush(); lspi_wait(); PB_OUT |= CS; }
#else
static inline void lcd_px(uint16_t c){ spi_byte((uint8_t)(c>>8)); spi_byte((uint8_t)c); }
static inline void lcd_end(void){ PB_OUT |= CS; }
#endif

/* ================= texte LISSÉ (police Inter, alpha 4 bits) =================
 * ft_box(x, y, w, ...) dessine une boîte de largeur fixe w et de la hauteur de la police : marge + texte + marge,
 * en un seul flux de pixels (fond compris). Rien n'est effacé avant : pas de clignotement. align 0 = gauche,
 * 1 = centré, 2 = droite. Le texte trop long est coupé. */
#include "fonts.h"
static int ft_glyph(const op_font *f, char c)
{
    for (int i = 0; i < f->n; i++) if (f->chars[i] == c) return i;
    return 0;                                          /* inconnu -> 1er caractère (espace) */
}
static uint32_t ft_width(const op_font *f, const char *s)
{
    uint32_t w = 0;
    while (*s) w += f->adv[ft_glyph(f, *s++)];
    return w;
}
static uint16_t ft_mix(uint16_t fg, uint16_t bg, uint32_t a)   /* a = 0..15 */
{
    uint32_t r = (((fg >> 11) & 31u) * a + ((bg >> 11) & 31u) * (15u - a)) / 15u;
    uint32_t g = (((fg >> 5) & 63u) * a + ((bg >> 5) & 63u) * (15u - a)) / 15u;
    uint32_t b = ((fg & 31u) * a + (bg & 31u) * (15u - a)) / 15u;
    return (uint16_t)((r << 11) | (g << 5) | b);
}
static uint32_t ft_box(uint32_t x, uint32_t y, uint32_t w, const char *s, const op_font *f,
                       uint16_t fg, uint16_t bg, int align)
{
    uint8_t gi[40]; uint32_t n = 0, tw = 0;
    if (x >= 240u || w == 0u) return 0;
    if (x + w > 240u) w = 240u - x;
    while (s[n] && n < 40u) {                          /* caractères qui tiennent dans la boîte */
        int g = ft_glyph(f, s[n]);
        if (tw + f->adv[g] > w) break;
        gi[n] = (uint8_t)g; tw += f->adv[g]; n++;
    }
    uint32_t lpad = align == 1 ? (w - tw) / 2u : (align == 2 ? w - tw : 0u), rpad = w - tw - lpad;
    uint16_t pal[16];
    for (uint32_t a = 0; a < 16u; a++) pal[a] = ft_mix(fg, bg, a);
    lcd_win(x, y, w, f->h);
    for (uint32_t r = 0; r < f->h; r++) {
        for (uint32_t i = 0; i < lpad; i++) lcd_px(bg);
        for (uint32_t k = 0; k < n; k++) {
            uint32_t a = f->adv[gi[k]];
            const uint8_t *b = f->bits + f->off[gi[k]];
            uint32_t p = r * a;
            for (uint32_t c = 0; c < a; c++, p++) lcd_px(pal[(b[p >> 1] >> ((p & 1u) ? 0 : 4)) & 15u]);
        }
        for (uint32_t i = 0; i < rpad; i++) lcd_px(bg);
        if ((r & 7u) == 7u) fm1_wdt_feed();
    }
    lcd_end();
    return tw;
}

/* un caractère (échelle sx,sy), fg sur bg */
static void lcd_char(uint32_t x, uint32_t y, char ch, uint16_t fg, uint16_t bg, uint32_t s)
{
    if (ch < 0x20 || ch > 0x7E) ch = ' ';
    const uint8_t *g = FONT8X8[ch - 0x20];
    lcd_win(x, y, 8u*s, 8u*s);
    for (uint32_t row=0; row<8u; row++)
        for (uint32_t ry=0; ry<s; ry++)
            for (uint32_t col=0; col<8u; col++) {
                uint16_t c = (g[row] & (0x80u>>col)) ? fg : bg;
                for (uint32_t rx=0; rx<s; rx++) lcd_px(c);
            }
    lcd_end();
}
static void lcd_text(uint32_t x, uint32_t y, const char *str, uint16_t fg, uint16_t bg, uint32_t s)
{
    while (*str) { lcd_char(x, y, *str++, fg, bg, s); x += 8u*s; }
}
/* ---- preset : la structure du moteur v2 (engine/px_dsp.h), éditée directement par l'UI ---- */
#include "px_dsp.h"
static px_preset_t PRESET;
static uint32_t cur_preset = 0;
static uint32_t master_vol = 80;        /* 0..100, suit le knob MASTER (ADC canal 2) */
static void st_get(uint32_t k, px_preset_t *p);   /* px_store.h: the saved preset of slot k, else the factory one */
static void load_preset(uint32_t i) { st_get(i, &PRESET); }

#if AUDIO_PROBE
void fm1_timer5_irq(void) { }
#endif
#if !AUDIO_PROBE
void op_audio_irq(void) { }           /* DAC interne : pas utilisé par la Pocket X */
#endif
/* écran de crash lisible : vecteur, PC, retour, registre d'erreur bus — puis reboot */
static void hex8(char *b, uint32_t v){ static const char H[]="0123456789ABCDEF";
    for (int i=0;i<8;i++) b[i]=H[(v>>(28-4*i))&15u]; b[8]=0; }
static void fm1_fault(const fm1_crash_t *c)
{
    char h[9];
    lcd_rect(0,0,240,240,RED);
    lcd_text(8,8,"CRASH",WHT,RED,2);
    lcd_text(8,40,"VEC",WHT,RED,1); hex8(h,c->vec);  lcd_text(64,40,h,WHT,RED,1);
    lcd_text(8,56,"PC", WHT,RED,1); hex8(h,c->pc);   lcd_text(64,56,h,WHT,RED,1);
    lcd_text(8,72,"RETS",WHT,RED,1);hex8(h,c->rets); lcd_text(64,72,h,WHT,RED,1);
    lcd_text(8,88,"DBG",WHT,RED,1); hex8(h,c->dbg);  lcd_text(64,88,h,WHT,RED,1);
    lcd_text(8,104,"EMU",WHT,RED,1);hex8(h,c->emu);  lcd_text(64,104,h,WHT,RED,1);
    lcd_text(8,136,"Take a photo :)",WHT,RED,1);
    for (int i=0;i<6;i++){ fm1_wdt_feed(); fm1_delay_ms(1000); }
    fm1_reboot();
}

/* ---- Entrées : encodeur rotatif (PC0/PC1) + bouton poussoir (PA2) ----
 * Reverse-engineeré du firmware d'origine (ISR timer lisant PC_IN & 0x3). */
#define PA_IN   (*(volatile uint32_t *)0x50004u)
#define PA_DIR  (*(volatile uint32_t *)0x50008u)
#define PA_DIE  (*(volatile uint32_t *)0x5000Cu)
#define PA_PU   (*(volatile uint32_t *)0x50010u)
#define PB_IN   (*(volatile uint32_t *)0x50044u)
#define PB_DIE  (*(volatile uint32_t *)0x5004Cu)
#define PB_PU   (*(volatile uint32_t *)0x50050u)
#define PC_IN   (*(volatile uint32_t *)0x50084u)
#define PC_DIR  (*(volatile uint32_t *)0x50088u)
#define PC_DIE  (*(volatile uint32_t *)0x5008Cu)
#define PC_PU   (*(volatile uint32_t *)0x50090u)
/* port H (index 7) : déclaré par Felucca et lu par le firmware d'origine. PAS de port D sur cette puce. */
#define PH_IN   (*(volatile uint32_t *)0x501C4u)
#define PH_DIE  (*(volatile uint32_t *)0x501CCu)
#define PH_PU   (*(volatile uint32_t *)0x501D0u)
#define PH_DIR  (*(volatile uint32_t *)0x501C8u)
#define ENC_MASK 0x3u      /* PC0=A, PC1=B */
#define OP_VERSION_NUM "28"
#define OP_VERSION "v" OP_VERSION_NUM   /* affiché au démarrage : change à chaque build livré */
#define PROBE 0            /* 1 = diagnostic GPIO : affiche en bas les broches qui changent */
#define ADC_PROBE 0        /* 1 = diagnostic ADC : affiche en bas les canaux qui bougent (C2:512) */
#define PA_PD   (*(volatile uint32_t *)0x50014u)

/* ---- Contrôles mappés sur la vraie pédale (diagnostic) ----
 * "appuyé" = différent de l'état au repos mémorisé au démarrage (marche quelle que soit la polarité) */
#define PA_FX     (1u<<0)
#define PA_HOME   (1u<<1)
#define PA_FS_UP  (1u<<2)              /* footswitch droit ▲ */
#define PA_FS_DN  (1u<<3)              /* footswitch gauche ▼ */
#define PH_PUSH   ((1u<<4)|(1u<<6))    /* push du knob PARA */
#define PH_MOD    ((1u<<7)|(1u<<8))
#define PH_AMP    (1u<<9)
#define PA_KNOWN  (PA_FX|PA_HOME|PA_FS_UP|PA_FS_DN)
#define PH_KNOWN  (PH_PUSH|PH_MOD|PH_AMP)
#define PA_MASTER (1u<<10)             /* knob MASTER = potentiomètre analogique sur PA10 */

static uint32_t base_pa, base_ph;      /* état au repos */

static void input_init(void)
{
    PC_DIR |= ENC_MASK; PC_DIE |= ENC_MASK; PC_PU |= ENC_MASK;   /* encodeur */
    PA_DIR |= PA_KNOWN; PA_DIE |= PA_KNOWN; PA_PU |= PA_KNOWN;   /* boutons confirmés */
    PH_DIR |= PH_KNOWN; PH_DIE |= PH_KNOWN; PH_PU |= PH_KNOWN;
#if PROBE
    /* broches inconnues : lecture numérique + pull-up seulement (on ne touche pas au sens) */
    PA_DIE |= 0xFFFFu; PA_PU |= 0xFFFFu;
    PC_DIE |= 0xFF7Cu; PC_PU |= 0xFF7Cu;                       /* hors PC0/1 (encodeur), PC7 (écran) */
    PH_DIE |= 0xFFFFu; PH_PU |= 0xFFFFu;
    PB_DIE |= 0xFF1Du; PB_PU |= 0xFF1Du;                       /* hors PB1/5/6/7 (écran) */
#endif
    PC_DIR &= ~PC7; PC_OUT |= PC7;      /* PC7 pilote l'écran : toujours sortie à 1 */
}
static void input_baseline(void)
{
    fm1_delay_ms(30);                   /* laisser les pull-ups se stabiliser */
    base_pa = PA_IN & 0xFFFFu; base_ph = PH_IN & 0xFFFFu;
}

#if PROBE
/* Diagnostic 16 broches/port : affiche le NOM des broches qui changent (ex. "PA12 PH4"). "--" = rien. */
static uint32_t probe_base[4], probe_prev[4], probe_t = 0;
static const uint32_t PROBE_MASK[4] = { 0xFFFFu, 0xFF1Du, 0xFF7Cu, 0xFFFFu };
static void probe_capture(void)
{
    probe_base[0]=PA_IN&0xFFFFu; probe_base[1]=PB_IN&0xFFFFu; probe_base[2]=PC_IN&0xFFFFu; probe_base[3]=PH_IN&0xFFFFu;
    for (int i=0;i<4;i++) probe_prev[i]=0xFFFFFFFFu;
}
static void probe_line(uint32_t now)
{
    uint32_t v[4] = { PA_IN&0xFFFFu, PB_IN&0xFFFFu, PC_IN&0xFFFFu, PH_IN&0xFFFFu };
    int same = 1; for (int i=0;i<4;i++) if (v[i]!=probe_prev[i]) same = 0;
    if (same && (now - probe_t) < 400u) return;
    for (int i=0;i<4;i++) probe_prev[i]=v[i];
    probe_t = now;
    char s[40]; int n = 0; static const char PN[4] = {'A','B','C','H'};
    for (int p = 0; p < 4 && n < 33; p++) {
        uint32_t d = (v[p] ^ probe_base[p]) & PROBE_MASK[p];
        for (int b = 0; b < 16 && n < 33; b++)
            if (d & (1u << b)) {
                s[n++]='P'; s[n++]=PN[p];
                if (b >= 10) { s[n++]='1'; s[n++]=(char)('0'+b-10); } else s[n++]=(char)('0'+b);
                s[n++]=' ';
            }
    }
    if (!n) { s[n++]='-'; s[n++]='-'; }
    s[n] = 0;
    lcd_rect(0,224,240,16,UI_PANEL);
    lcd_text(6,228,s,UI_ACC,UI_PANEL,1);
}
#endif

/* ---- Entrées analogiques (SARADC) ----
 * Driver de Felucca (MPL-2.0, même puce). NE PAS appeler fm1_adc_init() :
 * il passe PB1/PB6 en analogique, et sur la Pocket X ce sont des broches de l'écran. */
#include "fm1_adc.h"
#define ADC_CH_MASTER 2      /* knob MASTER (PA10) : 0..1023 */
#define ADC_CH_KEYS   9      /* clavier analogique : CAB ~617, DLY ~706, REV ~788 (mesuré) */
enum { KEY_NONE = 0, KEY_CAB, KEY_DLY, KEY_REV };
static int32_t keys_idle = -1;
static void adc_pins_analog(void)
{   /* PA7/PA8 (clavier analogique possible), PA10 (MASTER) : entrées analogiques, sans pull */
    uint32_t m = (1u<<7) | (1u<<8) | PA_MASTER;
    PA_DIE &= ~m; PA_PU &= ~m; PA_PD &= ~m; PA_DIR |= m;
}
/* lecture ADC "propre" : une 1re conversion jetée (le condensateur d'échantillonnage garde
 * la tension du canal précédent — effet fantôme), puis la vraie */
static int32_t adc_read2(uint32_t ch)
{
    (void)fm1_adc_read(ch);
    return fm1_adc_read(ch);
}
#define KEY_DEBUG 0          /* 1 = affiche la valeur brute du canal 9 en bas à droite (K####) */
/* reconnaît le bouton du clavier analogique par la valeur la plus proche (fenêtre ±42),
 * sans dépendre de l'état au repos */
static int keys_decode(int32_t v)
{
    static const int32_t REF[3] = { 617, 706, 788 };   /* CAB, DLY, REV */
    if (v < 0) return KEY_NONE;
    int best = -1; int32_t bd = 100000;
    for (int i = 0; i < 3; i++) { int32_t e = v - REF[i]; if (e < 0) e = -e; if (e < bd) { bd = e; best = i; } }
    return (bd <= 42) ? best + 1 : KEY_NONE;
}
static void keys_capture_idle(void)
{
    fm1_delay_ms(20);
    keys_idle = fm1_adc_read(ADC_CH_KEYS);
    /* si un bouton était tenu au démarrage (valeur proche d'un bouton), on suppose le repos à 1023 */
    static const int32_t REF[3] = { 617, 706, 788 };
    for (int i = 0; i < 3 && keys_idle >= 0; i++) {
        int32_t e = keys_idle - REF[i]; if (e < 0) e = -e;
        if (e <= 45) { keys_idle = 1023; break; }
    }
}
#if ADC_PROBE
#define ADC_NCH 16
static int32_t adc_base[ADC_NCH];
static uint32_t adc_t = 0;
static char adc_last[40];
static int adc_skip(uint32_t ch) { return ch == 3u || ch == 4u; }   /* PB1/PB6 = écran */
static void adc_capture(void)
{
    fm1_delay_ms(20);
    for (uint32_t ch = 0; ch < ADC_NCH; ch++) adc_base[ch] = adc_skip(ch) ? -1 : fm1_adc_read(ch);
    adc_last[0] = 0;
}
static void adc_line(uint32_t now)
{
    if (now - adc_t < 150u) return;
    adc_t = now;
    char s[40]; int n = 0;
    for (uint32_t ch = 0; ch < ADC_NCH && n <= 20; ch++) {
        if (adc_skip(ch) || adc_base[ch] < 0) continue;
        int32_t v = fm1_adc_read(ch);
        if (v < 0) continue;
        int32_t d = v - adc_base[ch]; if (d < 0) d = -d;
        if (d > 60) {
            s[n++]='C';
            if (ch >= 10u) { s[n++]='1'; s[n++]=(char)('0'+ch-10u); } else s[n++]=(char)('0'+ch);
            s[n++]=':';
            s[n++]=(char)('0'+(v/1000)%10); s[n++]=(char)('0'+(v/100)%10);
            s[n++]=(char)('0'+(v/10)%10);   s[n++]=(char)('0'+v%10);
            s[n++]=' ';
        }
    }
    if (!n) { s[n++]='-'; s[n++]='-'; }
    s[n] = 0;
    static uint32_t drawn_t = 0;
    int same = 1; for (int i = 0; i <= n; i++) if (s[i] != adc_last[i]) { same = 0; break; }
    if (same && (now - drawn_t) < 1000u) return;   /* redessine si changement, ou chaque seconde */
    drawn_t = now;
    for (int i = 0; i <= n; i++) adc_last[i] = s[i];
    lcd_rect(0,224,240,16,UI_PANEL);
    lcd_text(6,228,s,UI_ACC,UI_PANEL,1);
}
#endif

/* anti-rebond générique : renvoie 1 au front d'appui */
typedef struct { uint8_t stab, last, cnt; } dbn_t;
static int dbn_press(dbn_t *d, int raw)
{
    if (raw == d->last) { if (d->cnt < 5u) d->cnt++; }
    else { d->cnt = 0; d->last = (uint8_t)raw; }
    if (d->cnt >= 3u && raw != d->stab) { d->stab = (uint8_t)raw; return raw; }
    return 0;
}
/* ===== boot diagnostics (v23b) : survive a reset in .noinit, shown at the next boot =====
 * stage = last boot step reached; blocks/load/peak/nans/c1/loops = audio state seen by the main loop. */
#define OPD_MAGIC 0x4F504442u
enum { ST_NONE, ST_INPUTS, ST_PRESET, ST_ENG_INIT, ST_ENG_SET, ST_I2S, ST_UI, ST_RUN, ST_OK, ST_N };
static const char *const ST_NAME[ST_N] = {"?", "INPUTS", "PRESET", "ENGINE INIT", "ENGINE SET", "I2S START",
                                          "UI DRAW", "FIRST 2 S", "OK"};
volatile struct { uint32_t magic, stage, blocks, load, peak, nans, c1, loops, crash_seen, safe; } op_diag
    __attribute__((section(".noinit")));
static void op_stage(uint32_t s, const char *txt)
{
    op_diag.stage = s;
    fm1_wdt_feed();
    (void)txt;
}

/* table quadrature standard : index = (ancien<<2)|nouveau -> -1/0/+1 */
static const int8_t QDEC[16] = {0,-1,1,0, 1,0,0,-1, -1,0,0,1, 0,1,-1,0};
#if AUDIO_PROBE
#include "audio_probe.h"
#else
#include "px_audio.h"
#endif

#if !AUDIO_PROBE
#include "ui.h"
#include "px_usb.h"         /* USB-MIDI + update entry (web installer) */
#include "px_store.h"       /* the user's presets in flash */
#endif

static void app_main(void)
{
    uint32_t boots_ok = 0;
    int32_t accum = 0;
    uint8_t enc_last;
    enum { B_PUSH, B_FSUP, B_FSDN, B_FX, B_MOD, B_AMP, B_CAB, B_DLY, B_REV, NBTN };
    dbn_t btn[NBTN] = {{0,0,0}};
    dbn_t bhome = {0,0,0};
    uint32_t adc_tick = 0, home_t = 0, t_knob = 0, t_tuner = 0;
    int home_down = 0, home_long = 0, push_down = 0, push_long = 0;
    uint32_t push_t = 0;
    int32_t master_raw = -100;
#if DSP_CPU1
    fm1_cpu1_bar_build(27u);             /* un nouveau build redonne sa chance au 2e cœur */
    fm1_cpu1_start();                    /* au démarrage, avant l'audio et les interruptions (Felucca) */
#endif
    lcd_init();
    /* écran de démarrage avec la version : permet de vérifier quelle image tourne */
    lcd_rect(0,0,240,240,UI_BG);
    ft_box(0, 98, 240, "OPENPOCKET", &FT_MD, UI_ACC, UI_BG, 1);
    ft_box(0, 126, 240, OP_VERSION, &FT_SM, UI_DIM, UI_BG, 1);
    for (int i = 0; i < 6; i++) { fm1_wdt_feed(); fm1_delay_ms(100); }
    /* --- report of the previous boot, if it did not finish (or crashed) --- */
    int safe = 0;
    {
        uint32_t cc = (fm1_crash.magic == FM1_CRASH_MAGIC) ? fm1_crash.count : 0u;
        if (op_diag.magic != OPD_MAGIC) {
            op_diag.magic = OPD_MAGIC; op_diag.stage = ST_OK; op_diag.crash_seen = 0; op_diag.safe = 0;   /* a crash left by an older build is shown once */
        }
        int crashed = cc != op_diag.crash_seen;
        uint32_t st = op_diag.stage < ST_N ? op_diag.stage : 0u;
        if ((st != ST_OK || crashed) && !op_diag.safe) {
            char l[48], h[9];
            safe = 1;                    /* this boot: DSP bypassed, so the pedal stays usable */
            lcd_rect(0, 0, 240, 240, UI_BG);
            ft_box(0, 6, 240, "LAST BOOT FAILED", &FT_MD, RED, UI_BG, 1);
            l[0] = 0; b_str(l, "STOPPED AT: "); b_str(l, ST_NAME[st]);
            ft_box(0, 40, 240, l, &FT_SM, UI_TXT, UI_BG, 1);
            if (crashed) {
                l[0] = 0; b_str(l, "CRASH  PC "); hex8(h, fm1_crash.pc); b_str(l, h);
                ft_box(0, 62, 240, l, &FT_SM, UI_ACC, UI_BG, 1);
                l[0] = 0; b_str(l, "RETS "); hex8(h, fm1_crash.rets); b_str(l, h); b_str(l, "  VEC "); b_num(l, fm1_crash.vec);
                ft_box(0, 80, 240, l, &FT_SM, UI_ACC, UI_BG, 1);
                l[0] = 0; b_str(l, "DSP CODE AT "); hex8(h, (uint32_t)(uintptr_t)px_block_label); b_str(l, h);
                ft_box(0, 98, 240, l, &FT_SM, UI_DIM, UI_BG, 1);
                l[0] = 0; b_str(l, "UPTIME "); b_num(l, fm1_crash.uptime_ms); b_str(l, " MS  DBG "); hex8(h, fm1_crash.dbg); b_str(l, h);
                ft_box(0, 116, 240, l, &FT_SM, UI_DIM, UI_BG, 1);
            }
            l[0] = 0; b_str(l, "BLOCKS "); b_num(l, op_diag.blocks); b_str(l, "  LOAD "); b_num(l, op_diag.load);
            b_str(l, "%  PEAK "); b_num(l, op_diag.peak); b_str(l, "%");
            ft_box(0, 140, 240, l, &FT_SM, UI_TXT, UI_BG, 1);
            l[0] = 0; b_str(l, "NAN "); b_num(l, op_diag.nans); b_str(l, "  CPU1 "); b_num(l, op_diag.c1);
            b_str(l, "  LOOPS "); b_num(l, op_diag.loops);
            ft_box(0, 158, 240, l, &FT_SM, UI_TXT, UI_BG, 1);
            ft_box(0, 184, 240, "PLEASE TAKE A PHOTO", &FT_MD, UI_ACC, UI_BG, 1);
            ft_box(0, 212, 240, "SAFE MODE: DSP OFF THIS BOOT", &FT_SM, UI_DIM, UI_BG, 1);
            for (int i = 0; i < 120; i++) { fm1_wdt_feed(); fm1_delay_ms(100); }   /* 12 s */
            lcd_rect(0, 0, 240, 240, UI_BG);
            ft_box(0, 98, 240, "OPENPOCKET", &FT_MD, UI_ACC, UI_BG, 1);
            ft_box(0, 126, 240, OP_VERSION " SAFE MODE", &FT_SM, UI_DIM, UI_BG, 1);
        }
        op_diag.crash_seen = cc; op_diag.safe = (uint32_t)safe;
        op_diag.blocks = op_diag.load = op_diag.peak = op_diag.nans = op_diag.c1 = op_diag.loops = 0;
    }
    op_stage(ST_INPUTS, "INPUTS...");
    input_init();
    adc_pins_analog();
    input_baseline();
    keys_capture_idle();
    op_stage(ST_PRESET, "PRESET...");
    load_preset(cur_preset);
#if AUDIO_PROBE
    audio_probe_run();
#else
    a_hold = 1;                          /* DSP bypassed until the UI is up and 1.5 s have passed */
    px_usb_start();                      /* USB-MIDI device + update entry (web installer) */
    px_store_boot();                     /* the saved presets; start on the last one saved */
    audio_start();                       /* guitare -> moteur DSP -> sortie, ampli activé */
    op_stage(ST_UI, "SCREEN...");
    lcd_rect(0, 0, 240, 240, UI_BG);
    lcd_rect(0, 224, 240, 16, UI_PANEL);
    ui_invalidate();
    ui_render();
    op_stage(ST_RUN, 0);
#endif
    enc_last = (uint8_t)(PC_IN & ENC_MASK);
    for (;;) {
        uint32_t now = ms_now();
        fm1_wdt_feed();
#if !AUDIO_PROBE
        px_usb_service(now);             /* web installer: handshake, update, UBOOT soft key */
        audio_poll(now);                 /* le son suit l'UI (preset, réglages, volume) */
        if (ui_mode != M_TUNER) {        /* charge du DSP en bas à droite : « C1 42% » (2e cœur), « C0 » ou BYP */
            static uint32_t t_cpu;
            if (now - t_cpu >= 500u) {
                t_cpu = now;
                char c[10];
                uint32_t l = a_bypass ? a_peak : a_load; int i = 0;
                if (l > 999u) l = 999u;
                if (safe) { c[i++]='S'; c[i++]='A'; c[i++]='F'; c[i++]='E'; c[i++]=' '; }
                else if (a_bypass) { c[i++]='B'; c[i++]='Y'; c[i++]='P'; c[i++]=' '; }
                else { c[i++]='C'; c[i++]=(char)(a_c1 ? '1' : '0'); c[i++]=' '; }
                if (l >= 100u) c[i++] = (char)('0' + l / 100u);
                if (l >= 10u) c[i++] = (char)('0' + (l / 10u) % 10u);
                c[i++] = (char)('0' + l % 10u); c[i++] = '%'; c[i] = 0;
                ft_box(178, 225, 60, c, &FT_SM, a_bypass ? RED : UI_DIM, UI_PANEL, 2);
            }
        } else if (now - t_tuner >= 100u) { t_tuner = now; tuner_draw(tuner_estimate()); }
#endif
        if (!boots_ok && now > 2000u) { boots_ok = 1; guard.pending = 0; guard.failed = 0; op_diag.stage = ST_OK; }
#if !AUDIO_PROBE
        if (a_hold && !safe && now > 1500u) a_hold = 0;   /* DSP on */
        {
            static uint32_t t_diag;
            op_diag.loops++;
            if (now - t_diag >= 100u) {
                t_diag = now;
                op_diag.blocks = a_blocks; op_diag.load = a_load; op_diag.peak = a_peak;
                op_diag.nans = a_nan_count; op_diag.c1 = a_c1;
            }
        }
#endif

        int any = 0;                     /* une commande a été utilisée (sert aussi à quitter l'accordeur) */
        /* --- encodeur (sens inversé : on soustrait) ; rotation rapide = pas de 5 --- */
        uint8_t s = (uint8_t)(PC_IN & ENC_MASK);
        if (s != enc_last) {
            accum -= QDEC[(enc_last << 2) | s];
            enc_last = s;
            int dd = 0;
            if (accum >= 4) { accum -= 4; dd = 1; } else if (accum <= -4) { accum += 4; dd = -1; }
            if (dd) {
                int fast = (now - t_knob) < 45u; t_knob = now;
                if (ui_mode == M_SAVE) save_knob(dd); else if (ui_mode != M_TUNER) ui_knob(dd, fast);
                any = 1;
            }
        }
        /* --- boutons (appuyé = différent de l'état au repos) --- */
        uint32_t pa = (PA_IN & 0xFFFFu) ^ base_pa, ph = (PH_IN & 0xFFFFu) ^ base_ph;
        /* PUSH : appui court = action habituelle (au relâchement), appui long (0,8 s) = SAVE */
        dbn_press(&btn[B_PUSH], (ph & PH_PUSH) != 0);
        int push_now = btn[B_PUSH].stab != 0, p_push = 0, p_save = 0;
        if (push_now && !push_down) { push_down = 1; push_long = 0; push_t = now; }
        if (push_now && push_down && !push_long && now - push_t >= 800u) { push_long = 1; p_save = 1; }
        if (!push_now && push_down) { push_down = 0; if (!push_long) p_push = 1; }
        int p_up = dbn_press(&btn[B_FSUP], (pa & PA_FS_UP) != 0);
        int p_dn = dbn_press(&btn[B_FSDN], (pa & PA_FS_DN) != 0);
        int p_fx = dbn_press(&btn[B_FX], (pa & PA_FX) != 0);
        int p_mod = dbn_press(&btn[B_MOD], (ph & PH_MOD) != 0);
        int p_amp = dbn_press(&btn[B_AMP], (ph & PH_AMP) != 0);
        /* HOME : appui court = retour à la chaîne, appui long (0,7 s) = accordeur */
        dbn_press(&bhome, (pa & PA_HOME) != 0);
        int home_now = bhome.stab != 0, p_home = 0, p_tuner = 0;
        if (home_now && !home_down) { home_down = 1; home_long = 0; home_t = now; }
        if (home_now && home_down && !home_long && now - home_t >= 700u) { home_long = 1; p_tuner = 1; }
        if (!home_now && home_down) { home_down = 0; if (!home_long) p_home = 1; }

        int p_cab = 0, p_dly = 0, p_rev = 0;
        int32_t mv = -1;
        if (now - adc_tick >= 10u) {     /* clavier analogique CAB/DLY/REV + knob MASTER, toutes les 10 ms */
            adc_tick = now;
            int k = keys_decode(adc_read2(ADC_CH_KEYS));
            p_cab = dbn_press(&btn[B_CAB], k == KEY_CAB);
            p_dly = dbn_press(&btn[B_DLY], k == KEY_DLY);
            p_rev = dbn_press(&btn[B_REV], k == KEY_REV);
            mv = adc_read2(ADC_CH_MASTER);
        }
#if !AUDIO_PROBE
        if (ui_mode == M_SAVE) {
            if (p_push) save_confirm();
            else if (p_home) save_close();
        } else if (ui_mode == M_TUNER) {
            if (p_push || p_save || p_up || p_dn || p_fx || p_mod || p_amp || p_cab || p_dly || p_rev || p_home) tuner_close();
        } else if (p_save) {
            save_open();
        } else {
            if (p_tuner) tuner_open();
            else {
                if (p_push) ui_push();
                if (p_up) ui_preset_step(+1);
                if (p_dn) ui_preset_step(-1);
                if (p_home) ui_home();
                if (p_fx) ui_fx_cycle();
                if (p_mod) ui_enter_block(PX_B_MOD);
                if (p_amp) ui_enter_block(PX_B_AMP);
                if (p_cab) ui_enter_block(PX_B_CAB);
                if (p_dly) ui_enter_block(PX_B_DELAY);
                if (p_rev) ui_enter_block(PX_B_REVERB);
            }
        }
        (void)any;
#endif
        if (mv >= 0) {
            /* lissage (moyenne glissante x16, ~160 ms) puis hystérésis de 2 sur le volume 0..100 */
            master_raw = (master_raw < 0) ? mv * 16 : master_raw + mv - master_raw / 16;
            int32_t avg = master_raw / 16;
            uint32_t nv;
            if (avg <= 12) nv = 0u;
            else if (avg >= 1010) nv = 100u;
            else nv = (uint32_t)(avg - 12) * 100u / (1010u - 12u);
            int32_t dv = (int32_t)nv - (int32_t)master_vol; if (dv < 0) dv = -dv;
            if (dv >= 2 || (nv == 0u && master_vol != 0u) || (nv == 100u && master_vol != 100u)) master_vol = nv;
        }
#if !AUDIO_PROBE
        ui_render();                     /* ne redessine que ce qui a changé */
#endif
    }
}

void fm1_cstart(void)
{
    uint32_t *s, *d;
    fm1_time_init();
    fm1_wdt_arm(0x0D);
    if (guard.magic != 0x48575431u || guard.pending > 1u || guard.failed > 1u) {
        guard.magic = 0x48575431u; guard.failed = 0; guard.pending = 0;
    }
    if (guard.pending) guard.failed++;
    guard.pending = 1;
    if (guard.failed >= 2u) { guard.failed = 0; guard.pending = 0; fm1_enter_uboot(); }
    fm1_irq_init();
    for (d = _bss_start; d < _bss_end; d++) *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++) *d = *s;
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++) *d = *s;
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    app_main();
    for (;;) ;
}
