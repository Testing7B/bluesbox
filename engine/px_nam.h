/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — Neural Amp Modeler A2-Lite on the pedal, in fixed point.
 *
 * The network (NAM "A2", 2026, the 3-channel "Lite" size; tools/nam_a2.py is the float reference):
 *   x = rechannel * u                                     1 -> 3
 *   23 layers, kernel 6 (15 for layers 14 and 15), dilations 1 3 7 17 41 101 239 | x2 | 1 13 | 1 .. 239:
 *     z = conv(x) + bias + mixin * u ; a = LeakyReLU(z, 0.01) ; head += a ; x += layer1x1(a) + bias
 *   y = head_scale * (conv16(head) + bias)                3 -> 1
 * 1871 weights, read in the .nam file's order (pxn_load). About 1,730 multiply-adds per sample.
 *
 * Fixed point: activations are int32 with 20 fraction bits (range +-2048, step 1e-6), weights int32 with 24 (+-128),
 * products summed in int64 then shifted back by a constant (one instruction on the pedal).
 * Samples go through in blocks of up to PXN_N frames (px_nam.c: gather, dot, post). Each layer keeps its input history
 * in a ring of (kernel - 1) x dilation + PXN_N frames, the 3 channels side by side. The rings take 83.4 KB, the
 * weights 6.2 KB, the block buffers 7 KB.
 */
#pragma once
#include <stdint.h>

#define PXN_C      3
#define PXN_NL     23
#define PXN_HK     16
#define PXN_NW     1871                 /* A2-Lite weights, head_scale included */
#define PXN_N      32                   /* frames per block */
#define PXN_S      50                   /* a block row: u, 1.0, then up to 16 taps x 3 channels */
#define PXN_QA     20                   /* activations: fraction bits */
#define PXN_QW     24                   /* weights: fraction bits */
#define PXN_LOOK   6331                 /* sum of (kernel - 1) x dilation */
#define PXN_RING   (PXN_LOOK + (PXN_HK - 1) + (PXN_NL + 1) * PXN_N)   /* frames in all rings (head included) */


typedef struct {
    int32_t *ring;                      /* len frames x PXN_C channels, interleaved */
    int32_t len, w;                     /* ring length (lookback + PXN_N), where the next block is written */
    int16_t k, d;                       /* kernel, dilation */
    int32_t off;                        /* this layer's weight rows in pxn_model.wa */
    int32_t rr[12];                     /* its residual 1x1: 3 x {w[o][0..2] Q24, bias[o] Q20} */
    int32_t rr2[12];                    /* the same for the pedal's post2: w[o][c] by c then o, then the 3 biases */
} pxn_layer;

typedef struct {
    int32_t wa[1542 + 4];               /* each layer's 3 rows [mixin, bias, taps x channels], Q24 (+ slack) */
    int32_t rech[PXN_C];
    int32_t head[PXN_S + 4];            /* [0, bias, 16 taps x 3 channels] Q24 (+ slack) */
    float out_gain;                     /* head_scale / 2^20 */
    pxn_layer L[PXN_NL];
    int32_t hlen, hw;                   /* head ring */
    int32_t *hring;
    int ready;
} pxn_model;

/* Converts 1871 float weights (the .nam file's order) into the model. 0 = ok, -1 = a weight is not finite, -2 = the
 * ring memory is too small. The rings (PXN_RING_BYTES in all) come from the caller in up to two pieces (r0 of n0
 * bytes, r1 of n1 bytes: one RAM region may not hold them all); they are cleared. */
int pxn_load(pxn_model *m, const float *w, int32_t *r0, unsigned n0, int32_t *r1, unsigned n1);
#define PXN_RING_BYTES ((unsigned)PXN_RING * PXN_C * 4u)
/* the pedal's hand-written routines: checked once against the C ones (1 = in use); call before audio starts.
 * scratch: PXN_TEST_WORDS words (the ring memory will do: reset the model after) */
int pxn_selftest(int32_t *scratch);
#define PXN_TEST_WORDS 1280
int pxn_asm_in_use(void);
/* the vector unit: its start-up check's largest difference with C if in use (>= 0), else -1 - that difference (or -1) */
int pxn_vec_check(void);
void pxn_vstart(const int32_t *w, const int32_t *x, int T, int sb, int zb, int32_t *z, int n);   /* bench */
void pxn_vwait(void);
/* measurements for the firmware's bench screen (px_audio.h, BENCH_AT_BOOT): ticks, see px_nam.c */
uint32_t pxn_bench_layer(int32_t *scratch, uint32_t (*ticks)(void), int hand, int reps);
uint32_t pxn_bench_dot(int32_t *x, int32_t *w, int32_t *z, int vec, uint32_t (*ticks)(void), int reps);
uint32_t pxn_bench_post(int32_t *b, uint32_t (*ticks)(void), int reps, int v2);
uint32_t pxn_bench_vcmd(int32_t *buf, uint32_t (*ticks)(void), int nlen, int nloop, int xs, int zs, int reps, int start_only);
/* how the blocks run: 0 the CPU alone, 1 each dot product on the vector unit in turn, 2 the vector unit and the CPU at
 * once (half a block on the unit while the CPU prepares or finishes the other half). pxn_tune times them on a loaded
 * model and keeps the fastest (ticks for 256 frames in t[3], 0 = not available); the model is cleared after. */
int pxn_tune(pxn_model *m, uint32_t (*ticks)(void), uint32_t t[3]);
int pxn_mode(void);
int pxn_post2_in_use(void);
uint32_t pxn_bench_part(int32_t *scratch, uint32_t (*ticks)(void), int part, int reps);
uint32_t pxn_bench_ops(int kind, int32_t *buf, int reps, int *n, uint32_t (*ticks)(void));
/* clears the histories (silence in) */
void pxn_reset(pxn_model *m);
/* n frames (any n): in -> out, float, in place allowed */
void pxn_process(pxn_model *m, const float *in, float *out, int n);
