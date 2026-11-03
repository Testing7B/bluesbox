/* OpenPocket — guitar sound engine for the Donner Pocket X / M-Vave (JieLi AC791N)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * px_dsp.h — v3: a preset = 11 blocks; each block has a TYPE, an ON/OFF state and 5 settings 0..100.
 * The UI and the engine share exactly this structure.
 *
 *   IN -> GATE -> COMP -> PITCH -> SFX -> [ DRIVE -> AMP ] (x4) -> CAB -> EQ -> MOD -> DELAY -> REVERB -> OUT
 *
 * Models are voiced from published circuit analyses (ElectroSmash, Electric Druid, Rob Robinette,
 * Duncan Amps, Celestion/Jensen datasheets, Boss/Roland/Ampeg manuals...) — see docs/SOUNDS-AND-CONTROLS.md.
 * Names evoke the models they are inspired by, without using any brand.
 * Single-precision float (AC791N FPU); minimal libm: sinf/cosf/expf/logf/powf/sqrtf (firmware/px_math.c).
 */
#ifndef PX_DSP_H
#define PX_DSP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PX_SR
#define PX_SR 44100.0f
#endif
#ifndef PX_OVERSAMPLE
#define PX_OVERSAMPLE 4
#endif
#define PX_MAX_BLOCK 64

/* --------------------------------------------------------------- blocks --- */
enum { PX_B_GATE, PX_B_COMP, PX_B_PITCH, PX_B_SFX, PX_B_DRIVE, PX_B_AMP, PX_B_CAB, PX_B_EQ, PX_B_MOD,
       PX_B_DELAY, PX_B_REVERB, PX_NBLK };
#define PX_NPAR 5

enum { PX_CMP_SQUASH, PX_CMP_STUDIO, PX_CMP_OPTO, PX_CMP_SWELL, PX_CMP_COUNT };
enum { PX_PIT_OCTAVER, PX_PIT_POLYOCT, PX_PIT_WHAMMY, PX_PIT_HARMONY, PX_PIT_DETUNE, PX_PIT_COUNT };
enum { PX_SFX_AUTOWAH, PX_SFX_LFOWAH, PX_SFX_RING, PX_SFX_CRUSH, PX_SFX_SLICER, PX_SFX_LOFI, PX_SFX_SYNTH, PX_SFX_COUNT };
enum { PX_DRV_TS808, PX_DRV_TS9, PX_DRV_SD1, PX_DRV_KLON, PX_DRV_BD2, PX_DRV_OCD, PX_DRV_RAT, PX_DRV_DS1,
       PX_DRV_DPLUS, PX_DRV_BLUESBR, PX_DRV_KOT, PX_DRV_TIMMY, PX_DRV_ZEN, PX_DRV_MUFF, PX_DRV_RAMS,
       PX_DRV_RUSSIAN, PX_DRV_FUZZGE, PX_DRV_FUZZSI, PX_DRV_TONEB, PX_DRV_OCTAVIA, PX_DRV_FFACTORY,
       PX_DRV_RANGEM, PX_DRV_EPBOOST, PX_DRV_HM2, PX_DRV_MT2, PX_DRV_PRECISION, PX_DRV_BASSDRV, PX_DRV_COUNT };
enum { PX_AMP_TWIN, PX_AMP_DELUXE, PX_AMP_PRINCETON, PX_AMP_BASSMAN, PX_AMP_CHAMP, PX_AMP_AC30, PX_AMP_AC15,
       PX_AMP_JTM45, PX_AMP_PLEXI, PX_AMP_JCM800, PX_AMP_JVM, PX_AMP_HIWATT, PX_AMP_ORANGE, PX_AMP_MATCHLESS,
       PX_AMP_DUMBLE, PX_AMP_SLO, PX_AMP_MARK, PX_AMP_RECTO, PX_AMP_5150, PX_AMP_5150III, PX_AMP_ECSTASY,
       PX_AMP_FRIEDMAN, PX_AMP_VH4, PX_AMP_POWERBALL, PX_AMP_REVV, PX_AMP_JC120, PX_AMP_SUPRO, PX_AMP_TWOROCK,
       PX_AMP_SVT, PX_AMP_COUNT };
enum { PX_CAB_110, PX_CAB_112JN, PX_CAB_210, PX_CAB_212BLUE, PX_CAB_212GB, PX_CAB_410BASSMAN, PX_CAB_412GB,
       PX_CAB_412T75, PX_CAB_412V30, PX_CAB_412H30, PX_CAB_112CREAM, PX_CAB_115, PX_CAB_810SVT, PX_CAB_410BASS,
       PX_CAB_DI, PX_CAB_COUNT };
enum { PX_EQ_5BAND, PX_EQ_GEQ, PX_EQ_PARAM, PX_EQ_COUNT };
enum { PX_MOD_CHORUS, PX_MOD_DIMENSION, PX_MOD_FLANGER, PX_MOD_PHASE90, PX_MOD_STONE, PX_MOD_UNIVIBE,
       PX_MOD_OPTOTREM, PX_MOD_BIASTREM, PX_MOD_HARMTREM, PX_MOD_ROTARY, PX_MOD_VIBRATO, PX_MOD_COUNT };
enum { PX_DLY_DIGITAL, PX_DLY_ANALOG, PX_DLY_MEMORY, PX_DLY_TAPE, PX_DLY_SPACE, PX_DLY_REVERSE, PX_DLY_LOFI,
       PX_DLY_DUAL, PX_DLY_COUNT };
enum { PX_REV_ROOM, PX_REV_HALL, PX_REV_CATHEDRAL, PX_REV_PLATE, PX_REV_SPRING, PX_REV_SHIMMER, PX_REV_GATED,
       PX_REV_AMBIENT, PX_REV_COUNT };

/* ---------------------------------------------------------------- preset --- */
typedef struct {
    char    name[14];
    uint8_t on[PX_NBLK];
    uint8_t type[PX_NBLK];
    uint8_t p[PX_NBLK][PX_NPAR];   /* 0..100 */
} px_preset_t;

/* ------------------------------------------------------------ UI catalog --- */
const char *px_block_label(int blk);
const char *px_block_name(int blk);
int         px_ntypes(int blk);
const char *px_type_name(int blk, int type);
int         px_nparams(int blk, int type);
const char *px_param_name(int blk, int type, int k);
/* text for a knob value when a bare 0..100 says little ("+5TH", "440" ms...); returns 0 = show the number */
int         px_param_text(int blk, int type, int k, int v, char *buf8);
int         px_preset_count(void);
void        px_preset_factory(px_preset_t *p, int idx);

/* ---------------------------------------------------------------- state --- */
typedef struct { float b0, b1, b2, a1, a2, z1, z2; } px_bq;
typedef struct { float b0, b1, a1, z; } px_op;      /* first-order section (RC) */

#define PX_PS_N 2048                   /* pitch shifter buffer (46 ms) */
#define PX_DLY_N 48000                 /* 1.09 s of delay */
#define PX_RV_C 2800                   /* reverb combs */
#define PX_RV_A 1100
#define PX_CABM 6                      /* cabinet detail: cone breakup modes (2-pole resonators) */
#define PX_CABT 8                      /* cabinet detail: sparse taps (mic comb, back wave) */

typedef struct {
    float sr;
    px_preset_t cur;

    float dc_x1, dc_y1, odc_x1, odc_y1, dn;          /* dn: alternating 1e-15 anti-denormal offset */

    /* gate */
    float gate_env, gate_g, gate_thr, gate_rel;
    /* comp */
    float cmp_env, cmp_slow, cmp_att, cmp_rel, cmp_k, cmp_make, cmp_mix, cmp_sw, cmp_thr, cmp_rise, cmp_slowk;
    /* pitch: delay-line shifters (2 voices) + analog octaver */
    float ps_buf[PX_PS_N]; int ps_w, ps_nv;
    float ps_ph[2], ps_inc[2], ps_W[2], ps_lvl[2], ps_off[2][2], ps_dry, ps_r, ps_rt, ps_glide, ps_invW;
    px_bq oct_lp, oct_lp2, oct_sublp, oct_sublp2, oct_bp, oct_uphp;
    float oct_env, oct_ff, oct_ff2, oct_prev, oct_dry, oct_s1, oct_s2, oct_up;
    /* special fx */
    float sfx_lo, sfx_bp, sfx_env, sfx_ph, sfx_ph2, sfx_inc, sfx_ff, sfx_prev, sfx_g;
    float sfx_a, sfx_b, sfx_c, sfx_d, sfx_mix, sfx_qd;
    int   sfx_pat, crush_n, crush_k; float crush_v, crush_q;
    px_bq sfx_f1, sfx_f2;
    /* oversampling */
    px_bq os_up, os_up2, os_dn, os_dn2;
    /* drive */
    px_bq drv_hp, drv_shelf, drv_lp, drv_mid, drv_t1, drv_t2, drv_t3;
    float drv_gain, drv_level, drv_blend, drv_bias, drv_off, drv_gate, drv_dcs, drv_dck, drv_shelf_lo, drv_mix;
    int   drv_clip, drv_tm, drv_np, drv_pre, drv_dcb;
    /* amp */
    int   amp_n, amp_hard;
    float amp_g[4], amp_bias[4], amp_boff[4], amp_env[4], amp_envk, amp_bk;   /* env: bias shift (blocking), x depth */
    px_bq amp_tight, amp_bright;
    px_op amp_hp[4], amp_lp[4];
    px_bq ts_low, ts_mid, ts_high, amp_pres, amp_res, amp_voice;
    float amp_sag, amp_sagk, amp_pw, amp_pwn, amp_out;
    /* cabinet */
    px_bq cab[8]; float cab_g; int cab_n;
    /* fine structure (v27: same response as v25's 128-tap FIR, ~4x cheaper): sparse taps + breakup resonators */
    float cab_rb[64], cab_tg[PX_CABT], cab_m[PX_CABM][3], cab_ms[PX_CABM][2], cab_dg;
    uint8_t cab_ti[PX_CABT]; int cab_rw, cab_nt, cab_firon;
    /* eq */
    px_bq eq[5]; int eq_n;
    /* modulation */
    float mod_buf[2048]; int mod_w;
    float mod_ph, mod_inc, mod_depth, mod_mix, mod_fb, mod_base, mod_sweep, mod_shape;
    px_bq mod_lp;
    float ph_z[4], ph_fb, ph_lo, ph_hi, uv_k[4], uv_lamp, uv_up, uv_dn, trem_g, trem_att, trem_rel;
    float rot_h, rot_d, rot_hs, rot_ds, rot_ht, rot_dt, rot_hk, rot_dk, rot_bal;
    /* delay */
    float dly_buf[PX_DLY_N]; int dly_w, dly_type, dly_heads, rev_start, rev_cnt;
    float dly_len, dly_tgt, dly_fb, dly_mix, dly_ph, dly_ph2, dly_inc, dly_inc2, dly_wow, dly_flut, dly_mod;
    float dly_sat, dly_satn, dly_q, dly_hn, dly_hf, rev_xf;
    px_bq dly_lp, dly_hp;
    /* reverb: 8 damped combs + 4 all-pass (+ spring dispersion, shimmer shifter, gate) */
    float rv_c[8][PX_RV_C]; int rv_cw[8], rv_cl[8]; float rv_cs[8], rv_g[8], rv_damp;
    float rv_a[4][PX_RV_A]; int rv_aw[4], rv_al[4]; float rv_ag, rv_mix;
    int   rv_gate, rv_hold_n, sp_on; float rv_hold, rv_genv, rv_gg, rv_gate_rel;
    float sp_z[6], sp_a;
    float sh_buf[PX_PS_N]; int sh_w; float sh_ph, sh_out, sh_lp, sh_k, rv_shim;
    px_bq rv_tone, rv_hp;
} px_engine;

void px_engine_init(px_engine *e, float sr);
void px_engine_set(px_engine *e, const px_preset_t *p);
void px_engine_process(px_engine *e, const float *in, float *out, int n);

#ifdef __cplusplus
}
#endif
#endif
