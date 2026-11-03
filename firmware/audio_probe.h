/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket X — AUDIO PROBE v14: guitar input works, now looking for the pin that enables the OUTPUT.
 *
 * OFFICIAL JieLi drivers (SDK AC79 V1.2.0, Apache-2.0), compiled from their LLVM bitcode:
 *   sdk/iis.bc: I2S/ALNK0  -> guitar INPUT = DAT1 (PA8)   [confirmed on the pedal, v10]
 *   sdk/dac.bc: INTERNAL DAC (DACL pin) -> likely output to the SGM8905 amp
 *     (same schematic as JieLi's QFN48 reference board: board_7911B.c, DACL + amp mute)
 *
 * v11: DAC OK but no sound with weak pulls. v12 (595 via LED): dropped, too indirect.
 * v13: direct sweep -> crackle on the output when PA9 changes state.
 * v14: PA9 and PA7 set by hand (AMP / MOD) + optional sweep of the other pins.
 */
#pragma once
#include <stdint.h>

/* ===================== JieLi driver API (SDK iis.h / dac.h) ===================== */
struct iis_platform_data {
    uint8_t channel_in, channel_out, port_sel, data_width, mclk_output, slave_mode,
            update_edge, f32e, keep_alive, sel, width_16_to_24;
    uint16_t dump_points_num, sr_points;
};
int  iis_open(struct iis_platform_data *pd, uint8_t index);
void iis_close(uint8_t index);
int  iis_set_sample_rate(int sample_rate, uint8_t index);
void iis_channel_on(uint8_t channel, uint8_t index);
void iis_set_enc_data_handler(void *priv, void (*cb)(void *, uint8_t *, int, uint8_t), uint8_t index);
void iis_set_dec_data_handler(void *priv, void (*cb)(void *, uint8_t *, int, uint8_t), uint8_t index);
void iis_irq_handler(uint8_t index);
#define IIS_PORTA 0

struct dac_platform_data {
    uint8_t sw_differ, pa_auto_mute, pa_mute_port, pa_mute_value, differ_output, hw_channel,
            ch_num, fade_enable, auto_src_enable;
    uint16_t mute_delay_ms, fade_delay_ms, sr_points, vcm_init_delay_ms;
    uint32_t poweron_delay;
    void (*pa_mute_cb)(uint8_t mute);
};
void dac_early_init(uint8_t trim_en, uint8_t hw_channel, uint32_t dly_msecs);
int  dac_open(const struct dac_platform_data *pd);
int  dac_on(void);
int  dac_set_sample_rate(uint32_t sample_rate);
void dac_set_volume(int16_t volume);
void dac_set_gain(uint8_t gain, uint8_t fade_opt);
void dac_set_data_handler(void *priv, void (*handler)(void *, uint8_t *data, int len));
void dac_irq_handler(void);

/* ===================== functions provided to the drivers ===================== */
const uint8_t log_tag_const_d_AUDIO_IIS = 0, log_tag_const_e_AUDIO_IIS = 0, log_tag_const_i_AUDIO_IIS = 0;
const uint8_t log_tag_const_e_AUDIO_DAC = 0, log_tag_const_i_AUDIO_DAC = 0;
const uint32_t config_asser = 0;                       /* SDK assertions disabled */
void log_print(int lvl, const char *tag, const char *fmt, ...) { (void)lvl; (void)tag; (void)fmt; }
int  printf(const char *fmt, ...) { (void)fmt; return 0; }
void *os_current_task_rom(void) { return 0; }
void cpu_assert_debug(void) { }
void P33_SYSTEM_RESET(void) { for (;;) ; }             /* the watchdog reboots */

/* delays: feed the watchdog (the DAC waits ~1 s for its VCM voltage to rise) */
void delay_us(uint32_t us)
{
    while (us > 50000u) { fm1_delay_us(50000u); fm1_wdt_feed(); us -= 50000u; }
    fm1_delay_us(us);
}
void os_time_dly(int ticks) { delay_us((uint32_t)(ticks < 0 ? 0 : ticks) * 10000u); }   /* tick = 10 ms */
int os_sem_create(void *s, int n) { (void)s; (void)n; return 0; }
int os_sem_pend(void *s, int t)   { (void)s; (void)t; return 0; }
int os_sem_post(void *s)          { (void)s; return 0; }
int os_sem_set(void *s, int n)    { (void)s; (void)n; return 0; }
uint16_t usr_timer_add(void *p, void (*f)(void *), uint32_t ms, uint8_t pri) { (void)p; (void)f; (void)ms; (void)pri; return 0; }
void usr_timer_del(uint16_t id) { (void)id; }
int syscfg_read(uint16_t id, void *buf, uint16_t len)  { (void)id; (void)buf; (void)len; return -1; }  /* no saved trim */
int syscfg_write(uint16_t id, void *buf, uint16_t len) { (void)id; (void)buf; return len; }

/* heap: stack-style allocation, free() of the last block returns it (iis_close then iis_open) */
static uint8_t jl_heap[24576] __attribute__((aligned(8)));
static uint32_t jl_top, jl_last;
void *memset(void *d, int c, unsigned n) { uint8_t *p = d; while (n--) *p++ = (uint8_t)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { uint8_t *p = d; const uint8_t *q = s; while (n--) *p++ = *q++; return d; }
void *malloc(unsigned n)
{
    n = (n + 7u) & ~7u;
    if (jl_top + n > sizeof jl_heap) return 0;
    jl_last = jl_top; jl_top += n;
    return jl_heap + jl_last;
}
void *zalloc(unsigned n) { void *p = malloc(n); if (p) memset(p, 0, n); return p; }
void free(void *p) { if (p == jl_heap + jl_last) jl_top = jl_last; }

/* JieLi GPIO: number = port*16 + pin; port registers = 0x50000 + port*0x40 */
static volatile uint32_t *jl_preg(uint32_t gpio, uint32_t off)
{
    uint32_t port = gpio >> 4;
    if (port == 3u || port > 7u) return 0;             /* NO port D (flash, bus fault) */
    if (port == 2u && (gpio & 15u) == 7u) return 0;    /* PC7 = display: hands off */
    return (volatile uint32_t *)(0x50000u + port * 0x40u + off);
}
#define JL_BIT(g) (1u << ((g) & 15u))
static void jl_rmw(uint32_t g, uint32_t off, int on)
{
    volatile uint32_t *r = jl_preg(g, off);
    if (!r) return;
    if (on) *r |= JL_BIT(g); else *r &= ~JL_BIT(g);
}
int gpio_direction_input(uint32_t g)        { jl_rmw(g, 0x08, 1); return 0; }
int gpio_direction_output(uint32_t g, int v) { jl_rmw(g, 0x00, v); jl_rmw(g, 0x08, 0); return 0; }
int gpio_set_die(uint32_t g, int on)        { jl_rmw(g, 0x0C, on); return 0; }
int gpio_set_pull_up(uint32_t g, int on)    { jl_rmw(g, 0x10, on); return 0; }
int gpio_set_pull_down(uint32_t g, int on)  { jl_rmw(g, 0x14, on); return 0; }

/* ===================== audio (interrupt context) ===================== */
#define AP_FRAMES 128u
static volatile int32_t ap_in_pk;                      /* DAT1 input peak */
static volatile uint32_t ap_iis_blocks, ap_dac_blocks;
static volatile int ap_thru = 0;                       /* 0 = BEEP, 1 = DIRECT */
static uint32_t ap_ph_dac, ap_ph_iis, ap_smp_dac, ap_smp_iis;

/* ring buffer I2S input -> output (mono) */
#define RB_N 2048u
static int16_t rb[RB_N];
static volatile uint32_t rb_w, rb_r_dac, rb_r_iis;

static int16_t beep_next(uint32_t *ph, uint32_t *smp)  /* 440 Hz, 0.4 s every 0.8 s, -12 dB */
{
    *ph += 40864u;                                     /* 440/44100 * 2^22 */
    if (++*smp >= 35280u) *smp = 0;
    uint32_t t = (*ph >> 6) & 0xFFFFu;
    int32_t tri = (t < 32768u) ? (int32_t)t - 16384 : 49151 - (int32_t)t;
    return (*smp < 17640u) ? (int16_t)(tri / 2) : 0;
}
static int16_t rb_next(volatile uint32_t *r)
{
    uint32_t rr = *r;
    if (rr == rb_w) return 0;                          /* no data yet */
    if (((rb_w - rr) & (RB_N - 1u)) > RB_N / 2u) rr = (rb_w - 256u) & (RB_N - 1u);  /* catch up */
    int16_t v = rb[rr];
    *r = (rr + 1u) & (RB_N - 1u);
    return v;
}

static void ap_enc(void *priv, uint8_t *data, int len, uint8_t ch)   /* I2S input */
{
    (void)priv;
    if (ch != 1u) return;
    const int16_t *s = (const int16_t *)data;
    int n = len / 2;
    int32_t pk = ap_in_pk;
    uint32_t w = rb_w;
    for (int i = 0; i + 1 < n; i += 2) {
        int32_t v = (int32_t)s[i] + s[i + 1];          /* L+R -> mono */
        if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
        rb[w] = (int16_t)v; w = (w + 1u) & (RB_N - 1u);
        if (v < 0) v = -v;
        if (v > pk) pk = v;
    }
    rb_w = w; ap_in_pk = pk;
    ap_iis_blocks++;
}
static void ap_dec(void *priv, uint8_t *data, int len, uint8_t ch)   /* I2S output (if AMP) */
{
    (void)priv; (void)ch;
    int16_t *d = (int16_t *)data;
    int n = len / 2;
    for (int i = 0; i + 1 < n; i += 2) {
        int16_t v = ap_thru ? rb_next(&rb_r_iis) : beep_next(&ap_ph_iis, &ap_smp_iis);
        d[i] = d[i + 1] = v;
    }
}
static void ap_dac(void *priv, uint8_t *data, int len)            /* internal DAC output (mono) */
{
    (void)priv;
    int16_t *d = (int16_t *)data;
    int n = len / 2;
    for (int i = 0; i < n; i++)
        d[i] = ap_thru ? rb_next(&rb_r_dac) : beep_next(&ap_ph_dac, &ap_smp_dac);
    ap_dac_blocks++;
}

void fm1_alnk0_irq(void) { iis_irq_handler(0); }
void op_audio_irq(void)  { dac_irq_handler(); }
extern void isr_alnk0(void);
extern void isr_audio(void);
#define IRQ_AUDIO 13                                   /* SDK hwi.h: IRQ_AUDIO_IDX */

static struct iis_platform_data ap_pd;
static struct dac_platform_data ap_dpd;
static int ap_opened;

/* I2S: DAT1 always input; optional output on DAT0 or DAT2 (out = 0 / 1 / 4) */
static void ap_iis_open(uint8_t out)
{
    fm1_irq_mask(FM1_IRQ_ALNK0);
    if (ap_opened) iis_close(0);
    ap_pd.channel_in = 1u << 1;
    ap_pd.channel_out = out;
    ap_pd.port_sel = IIS_PORTA;
    ap_pd.data_width = 0; ap_pd.mclk_output = 1; ap_pd.slave_mode = 0;
    ap_pd.update_edge = 0; ap_pd.f32e = 0; ap_pd.keep_alive = 0; ap_pd.sel = 0; ap_pd.width_16_to_24 = 0;
    ap_pd.dump_points_num = 0; ap_pd.sr_points = AP_FRAMES;
    iis_set_enc_data_handler(0, ap_enc, 0);
    iis_set_dec_data_handler(0, ap_dec, 0);
    iis_open(&ap_pd, 0);
    ap_opened = 1;
    iis_set_sample_rate(44100, 0);
    iis_channel_on((uint8_t)(ap_pd.channel_in | ap_pd.channel_out), 0);
    fm1_irq_attach(FM1_IRQ_ALNK0, isr_alnk0, 3);
}
static void ap_dac_open(void)
{
    ap_dpd.pa_auto_mute = 0; ap_dpd.pa_mute_port = 0xFF; ap_dpd.pa_mute_value = 0;
    ap_dpd.differ_output = 0; ap_dpd.hw_channel = 0x01;   /* DACL, as in board_7911B.c */
    ap_dpd.ch_num = 1; ap_dpd.fade_enable = 0; ap_dpd.auto_src_enable = 0;
    ap_dpd.sr_points = 256; ap_dpd.vcm_init_delay_ms = 1000;
    dac_early_init(0, 0x01, 1000);
    dac_set_data_handler(0, ap_dac);
    dac_open(&ap_dpd);
    dac_set_sample_rate(44100);
    fm1_irq_attach(IRQ_AUDIO, isr_audio, 3);
    dac_on();
    dac_set_volume(16384);
    dac_set_gain(24, 0);                               /* analog gain 0..31 */
}

/* ===================== v14: PA9 made the output crackle (v13). Combine by hand =====================
 *   AMP: PA9 -> FLOAT / 3.3V / 0V / I2S DAT2 (data)
 *   MOD: PA7 -> I2S DAT0 (data) / FLOAT / 3.3V / 0V
 *   The other pins are swept (3.3 V then 0 V, 1.5 s each): right FS = pause, left FS = resume.
 *   The beep plays everywhere at once: internal DAC + I2S (DAT0/DAT2 if selected). FX: BEEP <-> DIRECT. */
#define G_(port, pin) ((port) * 16u + (pin))
static const struct { uint8_t g; char n[5]; } CAND[] = {
    {G_(0,11),"PA11"},{G_(0,12),"PA12"},
    {G_(1,0),"PB0"},{G_(1,2),"PB2"},{G_(1,3),"PB3"},{G_(1,4),"PB4"},{G_(1,8),"PB8"},
    {G_(2,2),"PC2"},{G_(2,3),"PC3"},{G_(2,4),"PC4"},{G_(2,5),"PC5"},{G_(2,6),"PC6"},
    {G_(2,8),"PC8"},{G_(2,9),"PC9"},{G_(2,10),"PC10"},
    {G_(7,0),"PH0"},{G_(7,1),"PH1"},{G_(7,2),"PH2"},{G_(7,3),"PH3"},{G_(7,5),"PH5"},{G_(7,10),"PH10"},
};
#define NCAND (sizeof CAND / sizeof CAND[0])
#define G_PA7 G_(0,7)
#define G_PA9 G_(0,9)

static void pin_drive(uint32_t g, int level)           /* 1 = 3.3 V, 0 = 0 V, -1 = floating */
{
    jl_rmw(g, 0x10, 0); jl_rmw(g, 0x14, 0);
    if (level < 0) { jl_rmw(g, 0x08, 1); return; }
    jl_rmw(g, 0x00, level);
    jl_rmw(g, 0x08, 0);
}
static void msg(uint32_t y, const char *s, uint16_t fg)
{
    lcd_rect(0, y, 240, 12, UI_BG);
    lcd_text(4, y + 2, s, fg, UI_BG, 1);
}
static void num_into(char *d, uint32_t v)
{
    char t[8]; int n = 0;
    while (*d) d++;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v && n < 7);
    while (n) *d++ = t[--n];
    *d = 0;
}
static void str_into(char *d, const char *s) { while (*d) d++; while (*s) *d++ = *s++; *d = 0; }

enum { M_Z, M_HI, M_LO, M_I2S };
static const char *MTXT[] = {"FLOAT", "3.3V", "0V", "I2S DATA"};
static uint8_t m9 = M_Z, m7 = M_I2S;
static void apply_manual(void)
{
    uint8_t out = (uint8_t)((m7 == M_I2S ? 1u : 0u) | (m9 == M_I2S ? 4u : 0u));
    ap_iis_open(out);
    if (m7 != M_I2S) pin_drive(G_PA7, m7 == M_Z ? -1 : (m7 == M_HI));
    if (m9 != M_I2S) pin_drive(G_PA9, m9 == M_Z ? -1 : (m9 == M_HI));
}
static void draw_head(void)
{
    lcd_rect(0, 0, 240, 240, UI_BG);
    lcd_text(4, 4, "AUDIO " OP_VERSION, UI_ACC, UI_BG, 2);
    msg(170, "AMP: PA9  MOD: PA7  FX: BEEP/DIRECT", UI_TXT);
    msg(184, "FS RIGHT: PAUSE   FS LEFT: RESUME", UI_TXT);
    msg(198, "LISTEN: IS THE BEEP OUT?", UI_ACC);
}

static void audio_probe_run(void)
{
    uint32_t idx = 0, step = 0, t_step = 0, t_ui = 0, ib_prev = 0, db_prev = 0, tour = 1;
    int paused = 1, redraw = 1;                        /* sweep paused at start: test PA9/PA7 first */
    dbn_t bu = {0,0,0}, bd = {0,0,0}, bf = {0,0,0}, bh = {0,0,0}, ba = {0,0,0}, bm = {0,0,0};

    draw_head();
    lcd_text(160, 8, "DAC...", YEL, UI_BG, 1);
    for (uint32_t i = 0; i < NCAND; i++) pin_drive(CAND[i].g, -1);
    apply_manual();
    fm1_irq_enable_all();
    ap_dac_open();

    for (;;) {
        uint32_t now = ms_now();
        fm1_wdt_feed();
        if (now > 3000u) { guard.pending = 0; guard.failed = 0; }
        uint32_t pa = (PA_IN & 0xFu) ^ (base_pa & 0xFu), ph = (PH_IN & 0xFFFFu) ^ base_ph;
        if (dbn_press(&bu, (pa & PA_FS_UP) != 0)) { paused = 1; redraw = 1; }
        if (dbn_press(&bd, (pa & PA_FS_DN) != 0)) {
            if (paused) { paused = 0; t_step = now; pin_drive(CAND[idx].g, step ? 0 : 1); }
            redraw = 1;
        }
        if (dbn_press(&bf, (pa & PA_FX) != 0)) { ap_thru = !ap_thru; redraw = 1; }
        if (dbn_press(&bh, (pa & PA_HOME) != 0)) { lcd_init(); draw_head(); redraw = 1; }
        if (dbn_press(&ba, (ph & PH_AMP) != 0)) { m9 = (uint8_t)((m9 + 1u) % 4u); apply_manual(); redraw = 1; }
        if (dbn_press(&bm, (ph & PH_MOD) != 0)) { m7 = (uint8_t)((m7 + 1u) % 4u); apply_manual(); redraw = 1; }

        if (!paused && now - t_step >= 1500u) {
            t_step = now;
            if (step == 0u) { step = 1u; pin_drive(CAND[idx].g, 0); }
            else {
                pin_drive(CAND[idx].g, -1);
                step = 0u;
                if (++idx >= NCAND) { idx = 0; tour++; }
                pin_drive(CAND[idx].g, 1);
            }
            redraw = 1;
        }

        if (redraw) {
            redraw = 0;
            char l[40] = "PA9: "; str_into(l, MTXT[m9]);
            lcd_rect(0, 30, 240, 44, UI_BG);
            lcd_text(4, 32, l, YEL, UI_BG, 2);
            char k[40] = "PA7: "; str_into(k, MTXT[m7]);
            lcd_text(4, 54, k, YEL, UI_BG, 2);
            char b[48] = "SWEEP ";
            if (paused) str_into(b, "PAUSED");
            else { str_into(b, CAND[idx].n); str_into(b, step ? " 0V" : " 3.3V"); }
            str_into(b, "  ("); num_into(b, idx + 1u); str_into(b, "/"); num_into(b, NCAND); str_into(b, ")");
            msg(84, b, paused ? UI_TXT : WHT);
            if (paused && (idx || step || tour > 1u)) {
                char p[40] = "STOPPED AT "; str_into(p, CAND[idx].n); str_into(p, step ? " 0V" : " 3.3V");
                msg(98, p, GRN);
            } else msg(98, "", UI_TXT);
            msg(118, ap_thru ? "SOUND: DIRECT (GUITAR)" : "SOUND: BEEP", ap_thru ? GRN : ORG);
        }

        if (now - t_ui >= 250u) {
            t_ui = now;
            uint32_t ib = ap_iis_blocks, db = ap_dac_blocks;
            lcd_rect(156, 2, 84, 22, UI_BG);
            lcd_text(160, 4, ib != ib_prev ? "I2S OK" : "I2S --", ib != ib_prev ? GRN : RED, UI_BG, 1);
            lcd_text(160, 14, db != db_prev ? "DAC OK" : "DAC --", db != db_prev ? GRN : RED, UI_BG, 1);
            ib_prev = ib; db_prev = db;
        }
    }
}
