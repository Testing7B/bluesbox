/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket X — le VRAI son : guitare -> moteur DSP (engine/px_dsp.c) -> sortie, réglé par l'UI.
 *
 * Chaîne audio de la Pocket X (trouvée avec les builds de test v10..v14, voir docs/AUDIO-SDK.md) :
 *   I2S ALNK0, groupe IIS_PORTA, JieLi maître, MCLK=PA4 SCLK=PA5 LRCK=PA6, 44,1 kHz, 24 bits stéréo (v26)
 *   24 bits comme le firmware officiel (PocketX_016 : bit « 24 bits » de chaque canal dans ALNK CON1) : en 24 bits
 *   la DMA range un échantillon par mot de 32 bits, aligné à droite (bit 23 = signe) ; LEN compte des mots, donc
 *   sr_points = 2 x A_FR pour garder 128 trames stéréo par bloc. En 16 bits, le bruit de quantification de l'entrée,
 *   multiplié par le gain des amplis, s'entendait comme un souffle constant.
 *   ENTREE guitare = DAT1 (PA8)    SORTIE = DAT0 (PA7) -> DAC « 344C » -> ampli SGM8905 -> OUT + casque
 *   ACTIVATION de l'ampli = PA9 à 3,3 V
 * Pilote I2S : celui du SDK officiel JieLi (sdk/iis.bc, Apache-2.0), compilé depuis son bitcode.
 *
 * Le traitement tourne dans l'interruption I2S (bloc de 128 trames = 2,9 ms), en float sur le FPU
 * matériel (-mcpu=r3 -mfprev1). Garde-fou : si le DSP prend plus de 85 % du temps pendant 8 blocs
 * d'affilée, on passe en BYPASS (guitare directe) au lieu de bloquer la pédale. L'UI affiche la charge.
 */
#pragma once
#include <stdint.h>
#ifndef BENCH_AT_BOOT
#define BENCH_AT_BOOT 0                                 /* 1 = écran de mesures du DSP au démarrage */
#endif
#include <math.h>
#include "px_dsp.h"

/* ===================== API du pilote I2S JieLi (iis.h du SDK) ===================== */
struct iis_platform_data {
    uint8_t channel_in, channel_out, port_sel, data_width, mclk_output, slave_mode,
            update_edge, f32e, keep_alive, sel, width_16_to_24;
    uint16_t dump_points_num, sr_points;
};
int  iis_open(struct iis_platform_data *pd, uint8_t index);
int  iis_set_sample_rate(int sample_rate, uint8_t index);
void iis_channel_on(uint8_t channel, uint8_t index);
void iis_set_enc_data_handler(void *priv, void (*cb)(void *, uint8_t *, int, uint8_t), uint8_t index);
void iis_set_dec_data_handler(void *priv, void (*cb)(void *, uint8_t *, int, uint8_t), uint8_t index);
void iis_irq_handler(uint8_t index);
#define IIS_PORTA 0

/* ===================== fonctions fournies au pilote (et au moteur) ===================== */
const uint8_t log_tag_const_d_AUDIO_IIS = 0, log_tag_const_e_AUDIO_IIS = 0, log_tag_const_i_AUDIO_IIS = 0;
const uint32_t config_asser = 0;
void log_print(int lvl, const char *tag, const char *fmt, ...) { (void)lvl; (void)tag; (void)fmt; }
int  printf(const char *fmt, ...) { (void)fmt; return 0; }
void *os_current_task_rom(void) { return 0; }
void cpu_assert_debug(void) { }
void P33_SYSTEM_RESET(void) { for (;;) ; }

static uint8_t jl_heap[4096] __attribute__((aligned(8)));   /* iis_open : 2 canaux x 8 x sr_points (256) */
void *malloc(unsigned n) { return (n <= sizeof jl_heap) ? jl_heap : 0; }   /* un seul bloc : iis_open */
void free(void *p) { (void)p; }
/* memset / memcpy: felucca-bsp libc.c (included by openpocket.c) */

static volatile uint32_t *jl_preg(uint32_t gpio, uint32_t off)
{
    uint32_t port = gpio >> 4;
    if (port == 3u || port > 7u) return 0;             /* pas de port D (flash) */
    if (port == 2u && (gpio & 15u) == 7u) return 0;    /* PC7 = écran */
    return (volatile uint32_t *)(0x50000u + port * 0x40u + off);
}
static void jl_rmw(uint32_t g, uint32_t off, int on)
{
    volatile uint32_t *r = jl_preg(g, off);
    if (!r) return;
    if (on) *r |= 1u << (g & 15u); else *r &= ~(1u << (g & 15u));
}
int gpio_direction_input(uint32_t g)         { jl_rmw(g, 0x08, 1); return 0; }
int gpio_direction_output(uint32_t g, int v) { jl_rmw(g, 0x00, v); jl_rmw(g, 0x08, 0); return 0; }
int gpio_set_die(uint32_t g, int on)         { jl_rmw(g, 0x0C, on); return 0; }
int gpio_set_pull_up(uint32_t g, int on)     { jl_rmw(g, 0x10, on); return 0; }
int gpio_set_pull_down(uint32_t g, int on)   { jl_rmw(g, 0x14, on); return 0; }

/* ===================== moteur + traitement sous interruption ===================== */
#define A_FR     128u                                  /* trames par bloc I2S */
#define A_BUDGET (A_FR * 1000000u / 44100u * FM1_TICKS_PER_US)   /* ticks dispo par bloc (~2,9 ms) */
#define G_AMP_EN 9u                                    /* PA9 : activation de l'ampli SGM8905 */

static px_engine g_eng __attribute__((section(".pool"), aligned(8)));   /* ~250 Ko : zone RAM « POOL » */
static float a_in[A_FR], a_out[A_FR];
static int32_t a_res[A_FR];                            /* sortie, 24 bits alignés à droite */
static volatile float a_lvl[2];                        /* entrée G / D : moyenne des carrés, en pas de 24 bits */
/* niveau d'entrée en dB sous la pleine échelle (dBFS), arrondi ; -199 = silence numérique */
static int a_level_db(int ch)
{
    float m = a_lvl[ch & 1] * (1.0f / (8388608.0f * 8388608.0f));
    if (m < 1e-20f) return -199;
    return (int)(4.3429448f * logf(m) - 0.5f);         /* 10 log10 */
}
static volatile float a_gain = 0.64f;                  /* volume MASTER (knob), appliqué en sortie */
static volatile uint32_t a_load, a_peak, a_blocks;     /* charge DSP en % */
static volatile uint8_t a_bypass, a_over, a_hold;   /* a_hold: DSP held off (boot, safe mode) */
static volatile uint8_t a_mute;                      /* output zeros (a flash write is coming) */
static volatile uint8_t a_nan;                         /* le moteur a sorti un NaN/Inf : à réinitialiser */
static volatile uint32_t a_nan_count;
static int a_running;

/* le travail DSP d'un bloc : a_in -> a_res (gain, écrêtage) + mesure de charge. Tourne sur le 2e cœur (CPU1)
 * quand il est disponible, sinon sur CPU0 dans l'interruption. */
static void dsp_block(void)
{
    uint32_t t0 = fm1_ticks();
    if (!a_bypass && !a_hold) {
        for (uint32_t o = 0; o < A_FR; o += PX_MAX_BLOCK)
            px_engine_process(&g_eng, a_in + o, a_out + o, PX_MAX_BLOCK);
    } else {
        for (uint32_t i = 0; i < A_FR; i++) a_out[i] = a_in[i];
    }
    /* filet de sécurité : un NaN/Inf contaminerait les filtres pour toujours (silence). On sort du silence et
     * la boucle principale réinitialise le moteur (audio_poll). */
    for (uint32_t i = 0; i < A_FR; i++) {
        float v = a_out[i];
        if (v != v || v > 1.0e6f || v < -1.0e6f) { a_nan = 1; break; }
    }
    if (a_nan) for (uint32_t i = 0; i < A_FR; i++) a_out[i] = 0.0f;
    if (a_mute) for (uint32_t i = 0; i < A_FR; i++) a_out[i] = 0.0f;
    float g = a_gain * 8388607.0f;
    for (uint32_t i = 0; i < A_FR; i++) {
        float v = a_out[i] * g;
        if (v > 8388607.0f) v = 8388607.0f; else if (v < -8388608.0f) v = -8388608.0f;
        a_res[i] = (int32_t)v;
    }
    uint32_t pct = (fm1_ticks() - t0) * 100u / A_BUDGET;
    a_load = (a_load * 7u + pct) / 8u;
    if (pct > a_peak) a_peak = pct;
    if (!a_bypass && !a_hold) {
        if (pct > 85u) { if (++a_over >= 8u) a_bypass = 1; } else a_over = 0;
    }
    a_blocks++;
}

/* --- deuxième cœur : le bloc N part sur CPU1 à la fin de l'IRQ N ; l'IRQ N+1 récupère le résultat avant de
 * l'envoyer en sortie. Même latence qu'avant (le traitement devait déjà finir avant le bloc suivant), mais CPU0
 * est libre entre deux IRQ : l'affichage et l'UI ne touchent plus jamais l'audio. Repli automatique sur CPU0
 * si le 2e cœur refuse un travail ou se trompe (FM1_CPU1_FALLBACK 1, fm1_cpu1.h de Felucca). */
static volatile uint8_t a_c1;                          /* 1 = le DSP tourne sur le 2e cœur */
static uint8_t a_job;                                  /* un bloc est en cours sur CPU1 */
#if DSP_CPU1
static void dsp_job(uint32_t arg) { (void)arg; dsp_block(); }
#endif
static void a_collect(void)                            /* récupère le bloc en cours sur CPU1 */
{
#if DSP_CPU1
    if (a_job) {
        a_job = 0;
        if (fm1_cpu1_wait() < 0) a_c1 = 0;             /* retiré : CPU0 reprend (ce bloc-ci est perdu) */
    }
#endif
}

/* ---- accordeur : l'interruption range l'entrée décimée par 4 (11025 Hz) ; la boucle principale estime la note ---- */
#define TN_N 1024u
static float tn_buf[TN_N];
static volatile uint32_t tn_w;
static void tn_capture(const float *x, uint32_t n)
{
    uint32_t w = tn_w;
    for (uint32_t i = 0; i + 3u < n; i += 4u) {
        tn_buf[w] = (x[i] + x[i + 1u] + x[i + 2u] + x[i + 3u]) * 0.25f;
        w = (w + 1u) & (TN_N - 1u);
    }
    tn_w = w;
}
static float tn_lin[TN_N];
static float tn_d[200];
static float tuner_estimate(void)      /* fréquence en Hz, ou 0 (silence / pas clair). Algorithme YIN. */
{
    const uint32_t W = 600u, TMIN = 10u, TMAX = 180u;  /* 61 Hz .. 1100 Hz à 11025 Hz */
    uint32_t w = tn_w;
    float e = 0.0f;
    for (uint32_t i = 0; i < TN_N; i++) { tn_lin[i] = tn_buf[(w + i) & (TN_N - 1u)]; e += tn_lin[i] * tn_lin[i]; }
    if (e < (float)TN_N * 1.0e-6f) return 0.0f;        /* trop faible : rien à accorder */
    const float *x = tn_lin + (TN_N - W - TMAX);
    float run = 0.0f;
    tn_d[0] = 1.0f;
    for (uint32_t t = 1; t <= TMAX; t++) {
        float d = 0.0f;
        for (uint32_t i = 0; i < W; i++) { float v = x[i] - x[i + t]; d += v * v; }
        run += d;
        tn_d[t] = (run > 0.0f) ? d * (float)t / run : 1.0f;
    }
    uint32_t best = 0;
    for (uint32_t t = TMIN; t < TMAX; t++)
        if (tn_d[t] < 0.15f) { while (t + 1u < TMAX && tn_d[t + 1u] < tn_d[t]) t++; best = t; break; }
    if (!best) {
        float m = 0.35f;
        for (uint32_t t = TMIN; t < TMAX; t++) if (tn_d[t] < m) { m = tn_d[t]; best = t; }
        if (!best) return 0.0f;
    }
    float a = tn_d[best - 1u], b = tn_d[best], c = tn_d[best + 1u], den = a - 2.0f * b + c;
    float tf = (float)best + ((den != 0.0f) ? 0.5f * (a - c) / den : 0.0f);
    return 11025.0f / tf;
}

static void a_enc(void *priv, uint8_t *data, int len, uint8_t ch)     /* bloc de guitare reçu */
{
    (void)priv;
    if (ch != 1u) return;
    a_collect();                                       /* a_in ne doit plus être lu par CPU1 */
    const uint32_t *s = (const uint32_t *)data;
    uint32_t n = (uint32_t)len / 8u;                   /* 2 mots de 32 bits par trame */
    if (n > A_FR) n = A_FR;
    float sl = 0.0f, sr = 0.0f;
    for (uint32_t i = 0; i < n; i++) {
        float l = (float)((int32_t)(s[2u * i] << 8) >> 8), r = (float)((int32_t)(s[2u * i + 1u] << 8) >> 8);
        sl += l * l; sr += r * r;
        a_in[i] = l * (0.5f / 8388608.0f);             /* guitare = canal G seul (comme l'officiel) ; le D ne porte
                                                          que du bruit (~ -77 dBFS, mesuré v26). x0.5 : même niveau
                                                          qu'avant (G + D) / 2, les réglages ne changent pas */
    }
    for (uint32_t i = n; i < A_FR; i++) a_in[i] = 0.0f;
    if (n) {                                           /* niveaux d'entrée G / D (écran de l'accordeur) */
        a_lvl[0] += (sl / (float)n - a_lvl[0]) * 0.0625f;
        a_lvl[1] += (sr / (float)n - a_lvl[1]) * 0.0625f;
    }
    tn_capture(a_in, A_FR);
#if DSP_CPU1
    if (a_c1 && fm1_cpu1_run(dsp_job, 0)) { a_job = 1; return; }
    a_c1 = 0;
#endif
    dsp_block();
}
static void a_dec(void *priv, uint8_t *data, int len, uint8_t ch)     /* bloc de sortie à remplir */
{
    (void)priv; (void)ch;
    a_collect();                                       /* le bloc précédent doit être fini */
    int32_t *d = (int32_t *)data;
    uint32_t n = (uint32_t)len / 8u;
    if (n > A_FR) n = A_FR;
    for (uint32_t i = 0; i < n; i++) d[2u * i] = d[2u * i + 1u] = a_res[i];
}
void fm1_alnk0_irq(void) { iis_irq_handler(0); }
extern void isr_alnk0(void);

/* ===================== UI -> moteur : même structure (px_preset_t), simple copie ===================== */
static void ui_to_px(px_preset_t *pp) { *pp = PRESET; }
static uint32_t ui_hash(void)                          /* empreinte de tout le preset (le volume est géré à part) */
{
    const uint8_t *b = (const uint8_t *)&PRESET;
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < sizeof(PRESET); i++) h = (h ^ b[i]) * 16777619u;
    return h;
}
static void audio_apply(void)                          /* recalcul des coefficients : hors interruption */
{
    px_preset_t pp;
    ui_to_px(&pp);
    if (a_running) fm1_irq_mask(FM1_IRQ_ALNK0);
    a_collect();                                       /* CPU1 ne doit pas traiter pendant le recalcul */
    px_engine_set(&g_eng, &pp);
    a_bypass = 0; a_over = 0; a_peak = 0;
    if (a_running) fm1_irq_unmask(FM1_IRQ_ALNK0);
}
static void audio_reset(void)                          /* après un NaN : moteur remis à zéro, preset réappliqué */
{
    px_preset_t pp;
    ui_to_px(&pp);
    if (a_running) fm1_irq_mask(FM1_IRQ_ALNK0);
    a_collect();
    px_engine_init(&g_eng, 44100.0f);
    px_engine_set(&g_eng, &pp);
    a_nan = 0; a_nan_count++;
    if (a_running) fm1_irq_unmask(FM1_IRQ_ALNK0);
}
static void audio_volume(uint32_t vol)                 /* 0..100 -> gain (courbe quadratique, ~logarithmique) */
{
    float v = (float)vol * 0.01f;
    a_gain = v * v;
}


/* ===================== écran de mesures au démarrage (6 s) : vitesse CPU/FPU et coût du moteur ===================== */
static void b_num(char *d, uint32_t v)
{
    char t[10]; int n = 0;
    while (*d) d++;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v && n < 9);
    while (n) *d++ = t[--n];
    *d = 0;
}
static void b_str(char *d, const char *s) { while (*d) d++; while (*s) *d++ = *s++; *d = 0; }
static uint32_t b_engine(const px_preset_t *pp)        /* % du budget temps réel pour 128 trames */
{
    px_engine_set(&g_eng, pp);
    for (uint32_t i = 0; i < A_FR; i++) a_in[i] = 0.3f * sinf((float)i * 0.0157f) + 0.05f * sinf((float)i * 0.11f);
    for (int w = 0; w < 4; w++) { px_engine_process(&g_eng, a_in, a_out, 64); px_engine_process(&g_eng, a_in + 64, a_out + 64, 64); }
    fm1_wdt_feed();
    uint32_t t0 = fm1_ticks();
    for (int r = 0; r < 8; r++) { px_engine_process(&g_eng, a_in, a_out, 64); px_engine_process(&g_eng, a_in + 64, a_out + 64, 64); }
    uint32_t dt = (fm1_ticks() - t0) / 8u;
    fm1_wdt_feed();
    return dt * 100u / A_BUDGET;
}
static void audio_bench(void)
{
    char l[48];
    lcd_rect(0, 0, 240, 240, UI_BG);
    lcd_text(4, 4, "DSP BENCH " OP_VERSION, UI_ACC, UI_BG, 2);
    /* boucle entière et boucle float : temps par tour (ns) */
    volatile uint32_t ia = 0; volatile float fa = 0.0f, fb = 1.0001f, fc = 0.5f;
    uint32_t t0 = fm1_ticks();
    for (uint32_t i = 0; i < 100000u; i++) ia += i;
    uint32_t ti = fm1_ticks() - t0;
    t0 = fm1_ticks();
    for (uint32_t i = 0; i < 100000u; i++) fa = fa * fb + fc;
    uint32_t tf = fm1_ticks() - t0;
    fm1_wdt_feed();
    l[0] = 0; b_str(l, "INT   100K LOOPS: "); b_num(l, ti / 24u); b_str(l, " US");
    lcd_text(4, 30, l, UI_TXT, UI_BG, 1);
    l[0] = 0; b_str(l, "FLOAT 100K LOOPS: "); b_num(l, tf / 24u); b_str(l, " US");
    lcd_text(4, 42, l, UI_TXT, UI_BG, 1);
    /* coût du moteur, bloc par bloc */
    px_preset_t pp;
    static const char *NAMES[4] = {"AMP ONLY     ", "+ CAB        ", "+ DRIVE/GATE ", "ALL ON       "};
    for (int c = 0; c < 4; c++) {
        px_preset_factory(&pp, 15);                    /* GROOVE METAL: 4-stage 5150 III */
        for (int z = 0; z < PX_NBLK; z++) pp.on[z] = 0;
        pp.on[PX_B_AMP] = 1;
        if (c >= 1) pp.on[PX_B_CAB] = 1;
        if (c >= 2) { pp.on[PX_B_DRIVE] = 1; pp.on[PX_B_GATE] = 1; }
        if (c >= 3) { for (int z = 0; z < PX_NBLK; z++) pp.on[z] = 1; pp.type[PX_B_MOD] = PX_MOD_ROTARY; pp.type[PX_B_PITCH] = PX_PIT_POLYOCT;
                        pp.type[PX_B_REVERB] = PX_REV_SHIMMER; pp.type[PX_B_DELAY] = PX_DLY_SPACE; }
        uint32_t pct = b_engine(&pp);
        l[0] = 0; b_str(l, NAMES[c]); b_num(l, pct); b_str(l, " %");
        lcd_text(4, 64u + (uint32_t)c * 16u, l, pct <= 85u ? GRN : RED, UI_BG, 2);
    }
    lcd_text(4, 140, "100% = REAL TIME (2.9 MS / BLOCK)", UI_TXT, UI_BG, 1);
    lcd_text(4, 160, "PLEASE TAKE A PHOTO OF THIS SCREEN", UI_ACC, UI_BG, 1);
    for (int i = 0; i < 60; i++) { fm1_wdt_feed(); fm1_delay_ms(100); }
}

static struct iis_platform_data a_pd;
static void audio_start(void)
{
#if BENCH_AT_BOOT
    px_engine_init(&g_eng, 44100.0f);
    audio_bench();                                     /* écran de mesures (diagnostic) : désactivé par défaut */
#endif
    op_stage(ST_ENG_INIT, "ENGINE INIT...");
    px_engine_init(&g_eng, 44100.0f);
    op_stage(ST_ENG_SET, "ENGINE SET...");
#if DSP_CPU1
    a_c1 = fm1_cpu1_on;                                /* démarré par app_main au tout début (fm1_cpu1_start) */
#endif
    audio_volume(master_vol);
    audio_apply();
    op_stage(ST_I2S, "AUDIO START...");
    a_pd.channel_in = 1u << 1;                         /* DAT1 = PA8 : guitare */
    a_pd.channel_out = 1u << 0;                        /* DAT0 = PA7 : sortie */
    a_pd.port_sel = IIS_PORTA;
    a_pd.data_width = (1u << 1) | (1u << 0);          /* 24 bits sur les deux canaux, comme l'officiel */
    a_pd.mclk_output = 1; a_pd.slave_mode = 0;
    a_pd.dump_points_num = 0; a_pd.sr_points = 2u * A_FR;   /* LEN en mots : 2 par trame en 24 bits */
    iis_set_enc_data_handler(0, a_enc, 0);
    iis_set_dec_data_handler(0, a_dec, 0);
    iis_open(&a_pd, 0);
    iis_set_sample_rate(44100, 0);
    iis_channel_on((uint8_t)(a_pd.channel_in | a_pd.channel_out), 0);
    fm1_irq_attach(FM1_IRQ_ALNK0, isr_alnk0, 3);
    a_running = 1;
    fm1_irq_enable_all();
    fm1_delay_ms(30);
    gpio_direction_output(G_AMP_EN, 1);                /* ampli ON (après l'I2S : pas de « pop ») */
}

/* à appeler dans la boucle principale : applique les changements du preset (anti-rebond 40 ms) */
static void audio_poll(uint32_t now)
{
    static uint32_t last_h, t_change, vol_shown = 1000u;
    static int pending;
    if (a_nan) audio_reset();
    uint32_t h = ui_hash();
    if (h != last_h) { last_h = h; t_change = now; pending = 1; }
    if (pending && now - t_change >= 40u) { pending = 0; audio_apply(); }
    if (master_vol != vol_shown) { vol_shown = master_vol; audio_volume(master_vol); }
}
