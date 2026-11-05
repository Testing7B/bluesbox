/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — the fitter's view of the engine (tools/fit_amp.py, docs/FITTED-MODELS.md): the real px_dsp.c, built
 * with PX_FIT, AMP block alone (knobs at noon), the amp's numbers given by the caller instead of the table.
 *   cc -O2 -shared -fPIC -DPX_FIT -Iengine tests/fit/fitlib.c engine/px_dsp.c firmware/px_math.c -lm -o build/fitlib.so */
#include <string.h>
#include "px_dsp.h"

typedef struct {
    const char *name; uint8_t n, hard; float g[4], bias[4], lp[4], hp[4], tight, bright, bright_db, bass, mid, midq10,
    treb, rb, rm, rt, scoop, pres, res, res_db, vf, vdb, sag100, pw10;
} amp_v;
extern amp_v px_fit_amp;
extern int px_fit_on;
extern float px_fit_trim;
static px_engine e;

/* v: g[4] bias[4] lp[4] hp[4] tight bright bright_db bass mid midq10 treb rb rm rt scoop pres res res_db vf vdb
 *    sag100 pw10 trim in_gain  (38 floats) */
int fit_run(const float *v, int nst, int hard, int gain_knob, const float *in, float *out, int n)
{
    amp_v *a = &px_fit_amp;
    float buf[64];
    int i, k;
    memset(a, 0, sizeof *a);
    a->name = "FIT"; a->n = (uint8_t)nst; a->hard = (uint8_t)hard;
    for (i = 0; i < 4; i++) { a->g[i] = v[i]; a->bias[i] = v[4 + i]; a->lp[i] = v[8 + i]; a->hp[i] = v[12 + i]; }
    a->tight = v[16]; a->bright = v[17]; a->bright_db = v[18]; a->bass = v[19]; a->mid = v[20]; a->midq10 = v[21];
    a->treb = v[22]; a->rb = v[23]; a->rm = v[24]; a->rt = v[25]; a->scoop = v[26]; a->pres = v[27]; a->res = v[28];
    a->res_db = v[29]; a->vf = v[30]; a->vdb = v[31]; a->sag100 = v[32]; a->pw10 = v[33];
    px_fit_trim = v[34];
    px_fit_on = 1;
    px_preset_t p;
    px_preset_factory(&p, 0);
    for (k = 0; k < PX_NBLK; k++) p.on[k] = 0;
    p.on[PX_B_AMP] = 1;
    for (k = 0; k < PX_NPAR; k++) p.p[PX_B_AMP][k] = 50;
    p.p[PX_B_AMP][0] = (uint8_t)gain_knob;
    px_engine_init(&e, 44100.0f);
    px_engine_set(&e, &p);
    for (i = 0; i < n; i += 64) {
        int m = n - i < 64 ? n - i : 64;
        for (k = 0; k < 64; k++) buf[k] = k < m ? in[i + k] * v[35] : 0.0f;
        px_engine_process(&e, buf, buf, 64);
        for (k = 0; k < m; k++) out[i + k] = buf[k];
    }
    px_fit_on = 0;
    return 0;
}

/* ---- drives (tools/fit_drive.py): the DRIVE block alone, knobs at noon ---- */
typedef struct {
    const char *name; uint8_t role[5], np;
    float hp, shelf, lp, mid, mid_db, mid_q10, g0, g1;
    uint8_t clip, tm; float t0, t1, p2, p2db, blend, bias, gate;
} drv_v;
extern drv_v px_fit_drv;
extern int px_fit_drv_on;
extern float px_fit_drv_trim;

/* v: hp shelf lp mid mid_db mid_q10 g0 g1 t0 t1 p2 p2db blend bias gate trim in_gain (17); roles: 5 bytes */
int fit_run_drive(const float *v, int clip, int tm, const unsigned char *roles, int np, const float *in, float *out, int n)
{
    drv_v *d = &px_fit_drv;
    float buf[64];
    int i, k;
    memset(d, 0, sizeof *d);
    d->name = "FIT";
    for (k = 0; k < 5; k++) d->role[k] = roles[k];
    d->np = (uint8_t)np;
    d->hp = v[0]; d->shelf = v[1]; d->lp = v[2]; d->mid = v[3]; d->mid_db = v[4]; d->mid_q10 = v[5];
    d->g0 = v[6]; d->g1 = v[7]; d->clip = (uint8_t)clip; d->tm = (uint8_t)tm; d->t0 = v[8]; d->t1 = v[9];
    d->p2 = v[10]; d->p2db = v[11]; d->blend = v[12]; d->bias = v[13]; d->gate = v[14];
    px_fit_drv_trim = v[15];
    px_fit_drv_on = 1;
    px_preset_t p;
    px_preset_factory(&p, 0);
    for (k = 0; k < PX_NBLK; k++) p.on[k] = 0;
    p.on[PX_B_DRIVE] = 1;
    for (k = 0; k < PX_NPAR; k++) p.p[PX_B_DRIVE][k] = 50;
    px_engine_init(&e, 44100.0f);
    px_engine_set(&e, &p);
    for (i = 0; i < n; i += 64) {
        int m = n - i < 64 ? n - i : 64;
        for (k = 0; k < 64; k++) buf[k] = k < m ? in[i + k] * v[16] : 0.0f;
        px_engine_process(&e, buf, buf, 64);
        for (k = 0; k < m; k++) out[i + k] = buf[k];
    }
    px_fit_drv_on = 0;
    return 0;
}

/* ---- cabinets (tools/fit_cab.py): the CAB block alone, knobs at noon; out = the impulse response ---- */
typedef struct { const char *name; float hp, hpq10, bump, bump_db, d1, d1db, d2, d2db, p1, p1db, p2, p2db, lp, lpq; } cab_v;
extern cab_v px_fit_cab;
extern int px_fit_cab_on;

/* v: hp hpq10 bump bump_db d1 d1db d2 d2db p1 p1db p2 p2db lp lpq (14); type: the cab type (open back, breakup seed) */
int fit_run_cab(const float *v, int type, float *out, int n)
{
    cab_v *c = &px_fit_cab;
    float buf[64];
    int i, k;
    c->name = "FIT";
    c->hp = v[0]; c->hpq10 = v[1]; c->bump = v[2]; c->bump_db = v[3]; c->d1 = v[4]; c->d1db = v[5]; c->d2 = v[6];
    c->d2db = v[7]; c->p1 = v[8]; c->p1db = v[9]; c->p2 = v[10]; c->p2db = v[11]; c->lp = v[12]; c->lpq = v[13];
    px_fit_cab_on = 1;
    px_preset_t p;
    px_preset_factory(&p, 0);
    for (k = 0; k < PX_NBLK; k++) p.on[k] = 0;
    p.on[PX_B_CAB] = 1;
    p.type[PX_B_CAB] = (uint8_t)type;
    for (k = 0; k < PX_NPAR; k++) p.p[PX_B_CAB][k] = 50;
    px_engine_init(&e, 44100.0f);
    px_engine_set(&e, &p);
    for (i = 0; i < n; i += 64) {
        int m = n - i < 64 ? n - i : 64;
        for (k = 0; k < 64; k++) buf[k] = (i + k == 0) ? 1.0f : 0.0f;
        px_engine_process(&e, buf, buf, 64);
        for (k = 0; k < m; k++) out[i + k] = buf[k];
    }
    px_fit_cab_on = 0;
    return 0;
}
