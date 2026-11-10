/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket v38 — USER IR (CAB type): the partitioned FFT convolution against the plain one (direct FIR) on noise and
 * on a guitar-like signal, the FFT round trip, the engine's slots (empty slot, names, a new slot is a full change). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define PX_IR_USER_GIVEN
#include "../../engine/px_dsp.c"
static float IRS[2][PX_IR_LEN];
int px_ir_user(int slot, char *name12, float *h)       /* slots 0 and 1 hold test IRs, the others are empty */
{
    if (slot < 0 || slot > 1) return 0;
    strcpy(name12, slot ? "TEST B" : "TEST A");
    if (h) memcpy(h, IRS[slot], sizeof IRS[slot]);
    return 1;
}
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)
static px_engine E;
int main(void)
{
    unsigned r = 99;
    for (int s = 0; s < 2; s++)
        for (int i = 0; i < PX_IR_LEN; i++) {
            r = r * 1664525u + 1013904223u;
            IRS[s][i] = ((float)(r >> 8) / 16777216.0f - 0.5f) * expf(-(float)i / (s ? 120.0f : 300.0f)) * (i ? 1.0f : 4.0f);
        }
    /* FFT round trip */
    fft_init();
    {
        float a[IR_F], b[IR_F];
        for (int i = 0; i < IR_F; i++) { r = r * 1664525u + 1013904223u; a[i] = b[i] = (float)(r >> 8) / 16777216.0f - 0.5f; }
        rfft128(b); irfft128(b);
        double e = 0, m = 0;
        for (int i = 0; i < IR_F; i++) { double d = b[i] / 128.0 - a[i]; e += d * d; m += a[i] * a[i]; }
        printf("FFT round trip: %.1f dB\n", 10.0 * log10(e / m));
        CHECK(10.0 * log10(e / m) < -120.0, "FFT round trip");
        /* the real FFT against a plain DFT */
        for (int i = 0; i < IR_F; i++) b[i] = a[i];
        rfft128(b);
        double em = 0;
        for (int k = 0; k <= 64; k++) {
            double re = 0, im = 0;
            for (int n = 0; n < IR_F; n++) { re += a[n] * cos(2 * M_PI * k * n / 128.0); im -= a[n] * sin(2 * M_PI * k * n / 128.0); }
            double gr = k == 0 ? b[0] : k == 64 ? b[1] : b[2 * k], gi = (k == 0 || k == 64) ? 0.0 : b[2 * k + 1];
            em += (gr - re) * (gr - re) + (gi - im) * (gi - im);
        }
        printf("real FFT against the DFT: %.1f dB\n", 10.0 * log10(em / m / 128.0));
        CHECK(10.0 * log10(em / m / 128.0) < -100.0, "real FFT");
    }
    /* the convolution against the direct FIR (the IR at its loaded level) */
    {
        const int N = 64 * 200;
        float *x = malloc(sizeof(float) * N), *y = malloc(sizeof(float) * N);
        for (int i = 0; i < N; i++) { r = r * 1664525u + 1013904223u; x[i] = 0.3f * sinf((float)i * 0.031f) + 0.2f * ((float)(r >> 8) / 16777216.0f - 0.5f); y[i] = x[i]; }
        ir_load_h(IRS[0], &ir_X[IR_P / 2][0]); ir_clear(); ir_ok = 1;
        for (int i = 0; i < N; i += 64) ir_run(y + i);
        double en = 0; for (int i = 0; i < PX_IR_LEN; i++) en += (double)IRS[0][i] * IRS[0][i];
        const double g = 1.0 / sqrt(en * 2.0);
        double e = 0, m = 0;
        for (int n = 0; n < N; n++) {
            double acc = 0;
            for (int k = 0; k < PX_IR_LEN && k <= n; k++) acc += IRS[0][k] * g * x[n - k];
            e += (y[n] - acc) * (y[n] - acc); m += acc * acc;
        }
        printf("partitioned convolution against the direct FIR: %.1f dB\n", 10.0 * log10(e / m));
        CHECK(10.0 * log10(e / m) < -100.0, "convolution");
        free(x); free(y);
    }
    /* the engine: CAB USER IR, slots */
    {
        px_preset_t p; px_preset_factory(&p, 0);
        for (int b = 0; b < PX_NBLK; b++) p.on[b] = 0;
        p.on[PX_B_CAB] = 1; p.type[PX_B_CAB] = PX_CAB_IR;
        p.p[PX_B_CAB][0] = (uint8_t)px_ir_knob(1); p.p[PX_B_CAB][1] = 0; p.p[PX_B_CAB][2] = 0;
        px_engine_init(&E, 44100.0f); px_engine_set(&E, &p);
        CHECK(ir_ok && E.ir_slot == 1, "slot 1 loaded");
        char nm[12];
        CHECK(px_ir_name(1, nm) && !strcmp(nm, "TEST B"), "name of slot 1");
        CHECK(!px_ir_name(5, nm) && !strcmp(nm, "IR6 EMPTY"), "name of an empty slot (%s)", nm);
        px_preset_t q = p; q.p[PX_B_CAB][0] = (uint8_t)px_ir_knob(0);
        CHECK(px_engine_change(&E, &q) == PX_CH_FULL, "another IR: a full change");
        q = p; q.p[PX_B_CAB][2] = 40;
        CHECK(px_engine_change(&E, &q) == PX_CH_KNOBS, "HIGH CUT: a knob change");
        px_engine_knobs(&E, &q);
        CHECK(ir_ok && E.ir_slot == 1, "a knob change keeps the IR");
        float x[64], y[64]; int nz = 0;
        for (int b = 0; b < 100; b++) {
            for (int i = 0; i < 64; i++) x[i] = (b == 10 && i == 0) ? 0.5f : 0.0f;
            px_engine_process(&E, x, y, 64);
            for (int i = 0; i < 64; i++) { if (!(fabsf(y[i]) < 10.0f)) nz = -1000; else if (y[i] != 0.0f && nz >= 0) nz++; }
        }
        CHECK(nz > 100, "the IR plays (%d samples)", nz);
        q = p; q.p[PX_B_CAB][0] = (uint8_t)px_ir_knob(4);   /* an empty slot: passes through */
        px_engine_set(&E, &q);
        CHECK(!ir_ok, "an empty slot");
        for (int i = 0; i < 64; i++) x[i] = 0.1f;
        for (int b = 0; b < 20; b++) px_engine_process(&E, x, y, 64);
        CHECK(fabsf(y[63]) > 0.0f && fabsf(y[63]) < 1.0f, "an empty slot passes");
        printf("CAB types: %d, the last \"%s\" with %d settings: %s, %s, %s\n", px_ntypes(PX_B_CAB), px_type_name(PX_B_CAB, PX_CAB_IR),
               px_nparams(PX_B_CAB, PX_CAB_IR), px_param_name(PX_B_CAB, PX_CAB_IR, 0), px_param_name(PX_B_CAB, PX_CAB_IR, 1),
               px_param_name(PX_B_CAB, PX_CAB_IR, 2));
    }
    if (fails) { printf("ir: %d FAILED\n", fails); return 1; }
    printf("ir: OK\n");
    return 0;
}
