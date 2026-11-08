/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket v38 — changes without clicks (px_engine_change / px_engine_knobs / px_engine_prepare):
 *  - a knob change through px_engine_knobs ends exactly where px_engine_set puts the engine (every gliding setting);
 *  - it clicks less: the largest step in the output's second difference in the 30 ms after the change, against the same
 *    change made the old way (px_engine_set), summed over many changes;
 *  - every block switched on and off fades and ends in the right state; no NaN anywhere;
 *  - px_engine_change sorts the changes (type, order, NAM model: full; settings, on / off: knobs). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../../engine/px_dsp.c"
static px_engine A, B;
#define SR 44100
#define NB 64
static float x[SR * 3], ya[SR * 3], yb[SR * 3];
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)
static void make_input(void)
{
    unsigned r = 7;
    for (int i = 0; i < SR * 3; i++) {
        r = r * 1664525u + 1013904223u;
        x[i] = 0.12f * sinf((float)i * 0.0313f) + 0.0005f * ((float)(r >> 8) / 16777216.0f - 0.5f);   /* steady: a click stands out */
    }
}
static void run(px_engine *e, float *y, int from, int to) { for (int i = from; i + NB <= to; i += NB) px_engine_process(e, x + i, y + i, NB); }
static float d3max(const float *y, int at, int len)     /* the largest third difference in [at, at + len) */
{
    float m = 0.0f;
    for (int i = at; i < at + len; i++) { float d = fabsf(y[i] - 3.0f * y[i - 1] + 3.0f * y[i - 2] - y[i - 3]); if (d > m) m = d; }
    return m;
}
static float click(const float *y, int at, int len)    /* how far the change's 30 ms stand out from the 30 ms before */
{
    float b = d3max(y, at - len, len) + 1e-6f, a = d3max(y, at, len);
    return a > b ? a / b - 1.0f : 0.0f;
}
static int nanfree(const float *y, int n) { for (int i = 0; i < n; i++) if (!(fabsf(y[i]) < 10.0f)) return 0; return 1; }
int main(void)
{
    make_input();
    const int T0 = SR / 4 / NB * NB, T1 = SR / NB * NB;
    double ca = 0, cb = 0; int nk = 0, nfade = 0;
    unsigned r = 12345;
    for (int f = 0; f < px_preset_count(); f++) {
        for (int trial = 0; trial < 4; trial++) {
            px_preset_t p0, p1;
            px_preset_factory(&p0, f);
            p1 = p0;
            r = r * 1664525u + 1013904223u;
            int b = (int)((r >> 8) % PX_NBLK), k = (int)((r >> 4) % PX_NPAR);
            if (trial % 3 == 2) p1.on[b] ^= 1;          /* a block on or off */
            else { int v = p1.p[b][k] + (int)((r >> 12) % 81) - 40; p1.p[b][k] = (uint8_t)(v < 0 ? 0 : v > 100 ? 100 : v); }
            px_engine_init(&A, 44100.0f); px_engine_set(&A, &p0);
            px_engine_init(&B, 44100.0f); px_engine_set(&B, &p0);
            const int ch = px_engine_change(&A, &p1);
            run(&A, ya, 0, T0); run(&B, yb, 0, T0);
            if (ch == PX_CH_NONE) continue;
            if (ch == PX_CH_FULL) { CHECK(b == PX_B_AMP, "preset %d: block %d change sorted full", f, b); continue; }
            px_engine_prepare(&A, &p1);
            px_engine_knobs(&A, &p1);
            px_engine_set(&B, &p1);
            run(&A, ya, T0, T1); run(&B, yb, T0, T1);
            CHECK(nanfree(ya, T1), "preset %d block %d: NaN or runaway", f, b);
            CHECK(A.gl_left == 0, "preset %d: glide not finished", f);
            for (int i = 0; i < GL_N; i++)
                CHECK(*GLP(&A, i) == *GLP(&B, i), "preset %d block %d: setting %d ends at %g, not %g", f, b, i, *GLP(&A, i), *GLP(&B, i));
            for (int z = 0; z < PX_NBLK; z++) CHECK(A.run.on[z] == p1.on[z] && A.bfd[z] == 0, "preset %d: block %d fade state", f, z);
            CHECK(A.cab_g == B.cab_g && A.cab_n == B.cab_n && !memcmp(&A.cd, &B.cd, sizeof A.cd), "preset %d: cabinet", f);
            for (int c = 0; c < A.cab_n; c++)
                CHECK(A.cab[c].b0 == B.cab[c].b0 && A.cab[c].a1 == B.cab[c].a1 && A.cab[c].a2 == B.cab[c].a2, "preset %d: cab filter %d", f, c);
            if (trial % 3 == 2) nfade++; else nk++;
            ca += click(ya, T0, 1323); cb += click(yb, T0, 1323);
        }
    }
    /* change sorting */
    {
        px_preset_t p; px_preset_factory(&p, 0);
        px_engine_init(&A, 44100.0f); px_engine_set(&A, &p);
        px_preset_t q = p; CHECK(px_engine_change(&A, &q) == PX_CH_NONE, "same preset");
        q = p; q.p[PX_B_EQ][1] ^= 3; CHECK(px_engine_change(&A, &q) == PX_CH_KNOBS, "a knob");
        q = p; q.on[PX_B_DELAY] ^= 1; CHECK(px_engine_change(&A, &q) == PX_CH_KNOBS, "a block on / off");
        q = p; q.type[PX_B_MOD] = (uint8_t)((q.type[PX_B_MOD] + 1) % PX_MOD_COUNT); CHECK(px_engine_change(&A, &q) == PX_CH_FULL, "a type");
        q = p; uint8_t t = q.order[3]; q.order[3] = q.order[4]; q.order[4] = t; CHECK(px_engine_change(&A, &q) == PX_CH_FULL, "the order");
        if (p.type[PX_B_AMP] == PX_AMP_NAM) {
            q = p; q.p[PX_B_AMP][0] = (uint8_t)px_nam_knob((px_nam_model(p.p[PX_B_AMP][0]) + 1) % PX_NAM_NFACT);
            CHECK(px_engine_change(&A, &q) == PX_CH_FULL, "another NAM model");
            q = p; q.on[PX_B_AMP] ^= 1; CHECK(px_engine_change(&A, &q) == PX_CH_FULL, "the NAM amp on / off");
        }
    }
    printf("changes: %d knob changes and %d blocks on / off glided and faded; clicks (summed: how far the 30 ms after the change stand out) %.1f against %.1f "
           "the old way (%.1f dB)\n", nk, nfade, ca, cb, 20.0 * log10(ca / cb));
    CHECK(ca < cb * 0.7, "not fewer clicks");
    if (fails) { printf("change: %d FAILED\n", fails); return 1; }
    printf("change: OK\n");
    return 0;
}
