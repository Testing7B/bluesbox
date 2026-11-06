/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — NAM A2-Lite in fixed point (see px_nam.h). Written from the published A2 architecture; checked
 * against tools/nam_a2.py (tests/test_nam.py).
 *
 * How it is computed, for this CPU (no data cache in front of the SRAM, a taken branch costs about 8 cycles, two
 * instructions can issue together): a block of up to PXN_N frames goes through the network layer by layer.
 *  - gather: each frame's inputs to the layer are copied into one row of X: [u, 1.0, then for each kernel tap its 3
 *    channels], from the layer's history ring (interleaved [time][channel], 3 words moved by one instruction);
 *  - dot: each output channel is a dot product of its weight row with the X rows, 4 frames at a time (a weight is read
 *    once for 4 frames), in the CPU's hardware loop, multiply-adds paired with loads: the 3 conv outputs, then the
 *    head the same way;
 *  - post: LeakyReLU, the head sum, the residual 1x1 into the next layer's ring.
 * The three inner routines exist twice: by hand for the pedal (pi32v2 assembly), and in C (the reference, used on the
 * PC and on the pedal if the start-up check finds the hand ones differ by a single bit). */
#include "px_nam.h"
#include <string.h>

static const uint8_t pxn_k[PXN_NL] = {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 15, 15, 6, 6, 6, 6, 6, 6, 6};
static const uint16_t pxn_d[PXN_NL] = {1, 3, 7, 17, 41, 101, 239, 1, 3, 7, 17, 41, 101, 239, 1, 13,
                                       1, 3, 7, 17, 41, 101, 239};
#define PXN_ONE (1 << PXN_QA)

/* the block's work buffers: X rows (+ a row of slack: the dot routines read one word past a row) */
static int32_t pxn_x[(PXN_N + 1) * PXN_S];
static int32_t pxn_z[PXN_N * PXN_C + 4], pxn_hs[PXN_N * PXN_C];

/* dot: z[3f] = (w . x_f) >> 24 for ng frames (dot4: ng groups of 4 frames), x_f the X rows sb bytes apart.
 * The hand routines read w[T] and x_f[T] (one word past): the buffers have slack. Offsets read by the hand code. */
typedef struct {
    const int32_t *w;                   /*  0 */
    const int32_t *x;                   /*  4 */
    int32_t t;                          /*  8: row length */
    int32_t sb;                         /* 12: X row stride, bytes */
    int32_t *z;                         /* 16: output, 3 words a frame */
    int32_t ng;                         /* 20: groups (> 0) */
} pxn_dot;
/* post, for n frames (> 0): a = LeakyReLU(z); hs += a; if out: out[3f+o] = x_f[o] + ((rr_o . a) >> 24) + rr_o[3],
 * x_f the layer's input in the X row (sb bytes apart) */
typedef struct {
    const int32_t *z;                   /*  0 */
    const int32_t *x;                   /*  4 */
    int32_t sb;                         /*  8 */
    const int32_t *rr;                  /* 12: 3 x {w0, w1, w2, bias (Q20)} */
    int32_t *hs;                        /* 16 */
    int32_t *out;                       /* 20: 0 = none (the last layer) */
    int32_t n;                          /* 24 */
} pxn_post;

/* ---- the C versions (the reference) ---- */
static void pxn_dot4_c(const pxn_dot *j)
{
    const int32_t *x = j->x;
    int32_t *z = j->z;
    const int s = j->sb / 4;
    for (int g = 0; g < j->ng; g++, x += 4 * s, z += 12) {
        int64_t a0 = 0, a1 = 0, a2 = 0, a3 = 0;
        for (int t = 0; t < j->t; t++) {
            const int64_t w = j->w[t];
            a0 += w * x[t]; a1 += w * x[s + t]; a2 += w * x[2 * s + t]; a3 += w * x[3 * s + t];
        }
        z[0] = (int32_t)(a0 >> PXN_QW); z[3] = (int32_t)(a1 >> PXN_QW);
        z[6] = (int32_t)(a2 >> PXN_QW); z[9] = (int32_t)(a3 >> PXN_QW);
    }
}
/* the 3 rows of a layer (contiguous, t each) for groups of 4 frames: z[3f + o] */
static void pxn_dot3x4_c(const pxn_dot *j)
{
    for (int o = 0; o < PXN_C; o++) {
        pxn_dot d = *j;
        d.w = j->w + o * j->t;
        d.z = j->z + o;
        pxn_dot4_c(&d);
    }
}
static void pxn_dot1_c(const pxn_dot *j)
{
    const int32_t *x = j->x;
    const int s = j->sb / 4;
    for (int g = 0; g < j->ng; g++, x += s) {
        int64_t a = 0;
        for (int t = 0; t < j->t; t++) a += (int64_t)j->w[t] * x[t];
        j->z[3 * g] = (int32_t)(a >> PXN_QW);
    }
}
static inline __attribute__((always_inline)) int32_t pxn_lrelu(int32_t z)   /* LeakyReLU 0.01 on a Q20 value */
{
    return z < 0 ? (int32_t)(((int64_t)z * 42949673) >> 32) : z;
}
static void pxn_post_c(const pxn_post *j)
{
    const int32_t *z = j->z, *x = j->x;
    int32_t *hs = j->hs, *out = j->out;
    for (int f = 0; f < j->n; f++, z += 3, hs += 3, x += j->sb / 4) {
        const int32_t a0 = pxn_lrelu(z[0]), a1 = pxn_lrelu(z[1]), a2 = pxn_lrelu(z[2]);
        hs[0] += a0; hs[1] += a1; hs[2] += a2;
        if (!out) continue;
        const int32_t *r = j->rr;
        for (int o = 0; o < PXN_C; o++, r += 4) {
            const int64_t v = (int64_t)r[0] * a0 + (int64_t)r[1] * a1 + (int64_t)r[2] * a2;
            *out++ = x[o] + (int32_t)(v >> PXN_QW) + r[3];
        }
    }
}
/* gather: n frames of 3 words, src contiguous -> dst rows sb bytes apart */
static void pxn_gcopy_c(int32_t *dst, const int32_t *src, int n, int sb)
{
    for (int f = 0; f < n; f++, src += 3, dst += sb / 4) { dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; }
}

#if defined(__PI32V2__) && !defined(PXN_NO_ASM)
#define PXN_ASM 1
/* ---- the same, by hand (in the flash, with their callers: no call-range question) ---- */
void pxn_dot3x4_asm(const pxn_dot *j);
void pxn_dot1_asm(const pxn_dot *j);
void pxn_post_asm(const pxn_post *j);
void pxn_gcopy_asm(int32_t *dst, const int32_t *src, int n, int sb);
__asm__(
    "\t.text\n"
    /* dot3x4: the 3 rows (contiguous) for 4 frames at a time. Locals: [sp+0] x, [sp+4] z, [sp+8] groups left,
     * [sp+12] job; r0 runs through the weights (back to the row start each group) */
    "\t.global pxn_dot3x4_asm\n"
    "pxn_dot3x4_asm:\n"
    "\t[--sp] = {rets, r15-r4}\n"
    "\tsp += -16\n"
    "\tr1 = [r0+4]\n\t[sp+0] = r1\n"
    "\tr1 = [r0+16]\n\t[sp+4] = r1\n"
    "\tr1 = [r0+20]\n\t[sp+8] = r1\n"
    "\t[sp+12] = r0\n"
    ".Lpxn_d4g:\n"
    "\tr0 = [sp+12]\n\tr0 = [r0+0]\n"
    "\tr5 = [sp+12]\n"
    "\tr12 = [r5+12]\n"
    "\tr7 = [r5+8]\n"
    "\tr1 = [sp+0]\n"
    "\tr2 = r1 + r12\n"
    "\tr3 = r2 + r12\n"
    "\tr4 = r3 + r12\n"
    "\tr9_r8 = 0\n"
    "\tr11_r10 = 0\n"
    "\tr13_r12 = 0\n"
    "\tr15_r14 = 0\n"
    "\tr5 = [r0++=4]\n"
    "\tr6 = [r1++=4]\n"
    ".Lpxn_d4r0:\n"
    "\trep r7 {\n"
    "\tr9_r8 += r6 * r5 (s)  #  r6 = [r2++=4]\n"
    "\tr11_r10 += r6 * r5 (s)  #  r6 = [r3++=4]\n"
    "\tr13_r12 += r6 * r5 (s)  #  r6 = [r4++=4]\n"
    "\tr15_r14 += r6 * r5 (s)  #  r5 = [r0++=4]\n"
    "\tr6 = [r1++=4]\n"
    "\t}\n"
    "\tif (r7 != 0) goto .Lpxn_d4r0\n"
    "\tr9_r8 >>= 24\n"
    "\tr11_r10 >>= 24\n"
    "\tr13_r12 >>= 24\n"
    "\tr15_r14 >>= 24\n"
    "\tr1 = [sp+4]\n"
    "\t[r1+0] = r8\n"
    "\t[r1+12] = r10\n"
    "\t[r1+24] = r12\n"
    "\t[r1+36] = r14\n"
    "\tr0 += -4\n"
    "\tr5 = [sp+12]\n"
    "\tr12 = [r5+12]\n"
    "\tr7 = [r5+8]\n"
    "\tr1 = [sp+0]\n"
    "\tr2 = r1 + r12\n"
    "\tr3 = r2 + r12\n"
    "\tr4 = r3 + r12\n"
    "\tr9_r8 = 0\n"
    "\tr11_r10 = 0\n"
    "\tr13_r12 = 0\n"
    "\tr15_r14 = 0\n"
    "\tr5 = [r0++=4]\n"
    "\tr6 = [r1++=4]\n"
    ".Lpxn_d4r1:\n"
    "\trep r7 {\n"
    "\tr9_r8 += r6 * r5 (s)  #  r6 = [r2++=4]\n"
    "\tr11_r10 += r6 * r5 (s)  #  r6 = [r3++=4]\n"
    "\tr13_r12 += r6 * r5 (s)  #  r6 = [r4++=4]\n"
    "\tr15_r14 += r6 * r5 (s)  #  r5 = [r0++=4]\n"
    "\tr6 = [r1++=4]\n"
    "\t}\n"
    "\tif (r7 != 0) goto .Lpxn_d4r1\n"
    "\tr9_r8 >>= 24\n"
    "\tr11_r10 >>= 24\n"
    "\tr13_r12 >>= 24\n"
    "\tr15_r14 >>= 24\n"
    "\tr1 = [sp+4]\n"
    "\t[r1+4] = r8\n"
    "\t[r1+16] = r10\n"
    "\t[r1+28] = r12\n"
    "\t[r1+40] = r14\n"
    "\tr0 += -4\n"
    "\tr5 = [sp+12]\n"
    "\tr12 = [r5+12]\n"
    "\tr7 = [r5+8]\n"
    "\tr1 = [sp+0]\n"
    "\tr2 = r1 + r12\n"
    "\tr3 = r2 + r12\n"
    "\tr4 = r3 + r12\n"
    "\tr9_r8 = 0\n"
    "\tr11_r10 = 0\n"
    "\tr13_r12 = 0\n"
    "\tr15_r14 = 0\n"
    "\tr5 = [r0++=4]\n"
    "\tr6 = [r1++=4]\n"
    ".Lpxn_d4r2:\n"
    "\trep r7 {\n"
    "\tr9_r8 += r6 * r5 (s)  #  r6 = [r2++=4]\n"
    "\tr11_r10 += r6 * r5 (s)  #  r6 = [r3++=4]\n"
    "\tr13_r12 += r6 * r5 (s)  #  r6 = [r4++=4]\n"
    "\tr15_r14 += r6 * r5 (s)  #  r5 = [r0++=4]\n"
    "\tr6 = [r1++=4]\n"
    "\t}\n"
    "\tif (r7 != 0) goto .Lpxn_d4r2\n"
    "\tr9_r8 >>= 24\n"
    "\tr11_r10 >>= 24\n"
    "\tr13_r12 >>= 24\n"
    "\tr15_r14 >>= 24\n"
    "\tr1 = [sp+4]\n"
    "\t[r1+8] = r8\n"
    "\t[r1+20] = r10\n"
    "\t[r1+32] = r12\n"
    "\t[r1+44] = r14\n"
    "\tr0 += -4\n"
    "\tr1 = [sp+4]\n\tr1 += 48\n\t[sp+4] = r1\n"
    "\tr0 = [sp+12]\n\tr12 = [r0+12]\n\tr12 = r12 << 2\n"
    "\tr1 = [sp+0]\n\tr1 += r12\n\t[sp+0] = r1\n"
    "\tr1 = [sp+8]\n\tr1 += -1\n\t[sp+8] = r1\n"
    "\tif (r1 != 0) goto .Lpxn_d4g\n"
    "\tsp += 16\n"
    "\t{pc, r15-r4} = [sp++]\n"
    /* dot1 */
    "\t.global pxn_dot1_asm\n"
    "pxn_dot1_asm:\n"
    "\t[--sp] = {rets, r15-r4}\n"
    "\tsp += -16\n"
    "\tr1 = [r0+4]\n\t[sp+0] = r1\n"
    "\tr1 = [r0+16]\n\t[sp+4] = r1\n"
    "\tr1 = [r0+20]\n\t[sp+8] = r1\n"
    "\t[sp+12] = r0\n"
    ".Lpxn_d1g:\n"
    "\tr0 = [sp+12]\n"
    "\tr7 = [r0+8]\n"
    "\tr1 = [sp+0]\n"
    "\tr0 = [r0+0]\n"
    "\tr9_r8 = 0\n"
    "\tr5 = [r0++=4]\n"
    "\tr6 = [r1++=4]\n"
    ".Lpxn_d1r:\n"
    "\trep r7 {\n"
    "\tr9_r8 += r6 * r5 (s)  #  r5 = [r0++=4]\n"
    "\tr6 = [r1++=4]\n"
    "\t}\n"
    "\tif (r7 != 0) goto .Lpxn_d1r\n"
    "\tr9_r8 >>= 24\n"
    "\tr1 = [sp+4]\n\t[r1+0] = r8\n\tr1 += 12\n\t[sp+4] = r1\n"
    "\tr0 = [sp+12]\n\tr12 = [r0+12]\n"
    "\tr1 = [sp+0]\n\tr1 += r12\n\t[sp+0] = r1\n"
    "\tr1 = [sp+8]\n\tr1 += -1\n\t[sp+8] = r1\n"
    "\tif (r1 != 0) goto .Lpxn_d1g\n"
    "\tsp += 16\n"
    "\t{pc, r15-r4} = [sp++]\n"
    /* post: r1 z, r2 x, r3 hs, r7 out, r11 rr, r12 the LeakyReLU slope, r13 frames left */
    "\t.global pxn_post_asm\n"
    "pxn_post_asm:\n"
    "\t[--sp] = {rets, r15-r4}\n"
    "\tr1 = [r0+0]\n\tr2 = [r0+4]\n\tr11 = [r0+12]\n\tr3 = [r0+16]\n\tr7 = [r0+20]\n\tr13 = [r0+24]\n"
    "\tr12 = 42949673\n"
    ".Lpxn_pf:\n"
    "\tr4 = [r1++=4]\n\tr5 = [r1++=4]\n\tr6 = [r1++=4]\n"
    "\tifs (r4 < 0) {\n\tr15_r14 = r4 * r12 (s)\n\tr4 = r15\n\t}\n"
    "\tifs (r5 < 0) {\n\tr15_r14 = r5 * r12 (s)\n\tr5 = r15\n\t}\n"
    "\tifs (r6 < 0) {\n\tr15_r14 = r6 * r12 (s)\n\tr6 = r15\n\t}\n"
    "\tr14 = [r3+0]\n\tr14 += r4\n\t[r3++=4] = r14\n"
    "\tr14 = [r3+0]\n\tr14 += r5\n\t[r3++=4] = r14\n"
    "\tr14 = [r3+0]\n\tr14 += r6\n\t[r3++=4] = r14\n"
    "\tif (r7 == 0) goto .Lpxn_pn\n"
    "\tr10 = r11\n\tr15 = 4\n"
    "\tr9_r8 = 0\n"
    "\tr9_r8 += [r10 ++= r15]*r4 (s)\n\tr9_r8 += [r10 ++= r15]*r5 (s)\n\tr9_r8 += [r10 ++= r15]*r6 (s)\n"
    "\tr9_r8 >>= 24\n\tr14 = [r10++=4]\n\tr8 += r14\n\tr14 = [r2+0]\n\tr8 += r14\n\t[r7++=4] = r8\n"
    "\tr9_r8 = 0\n"
    "\tr9_r8 += [r10 ++= r15]*r4 (s)\n\tr9_r8 += [r10 ++= r15]*r5 (s)\n\tr9_r8 += [r10 ++= r15]*r6 (s)\n"
    "\tr9_r8 >>= 24\n\tr14 = [r10++=4]\n\tr8 += r14\n\tr14 = [r2+4]\n\tr8 += r14\n\t[r7++=4] = r8\n"
    "\tr9_r8 = 0\n"
    "\tr9_r8 += [r10 ++= r15]*r4 (s)\n\tr9_r8 += [r10 ++= r15]*r5 (s)\n\tr9_r8 += [r10 ++= r15]*r6 (s)\n"
    "\tr9_r8 >>= 24\n\tr14 = [r10++=4]\n\tr8 += r14\n\tr14 = [r2+8]\n\tr8 += r14\n\t[r7++=4] = r8\n"
    ".Lpxn_pn:\n"
    "\tr14 = [r0+8]\n\tr2 += r14\n"
    "\tr13 += -1\n"
    "\tif (r13 != 0) goto .Lpxn_pf\n"
    "\t{pc, r15-r4} = [sp++]\n"
    /* gcopy: r0 dst, r1 src, r2 frames, r3 row stride */
    "\t.global pxn_gcopy_asm\n"
    "pxn_gcopy_asm:\n"
    "\t[--sp] = {rets, r15-r4}\n"
    "\tr3 += -12\n"
    ".Lpxn_gc:\n"
    "\trep r2 {\n"
    "\t{r6-r4} = [r1++]\n"
    "\t[r0++] = {r6-r4}\n"
    "\tr0 += r3\n"
    "\t}\n"
    "\tif (r2 != 0) goto .Lpxn_gc\n"
    "\t{pc, r15-r4} = [sp++]\n");
#else
#define PXN_ASM 0
#endif

/* the routines in use: C until the start-up check (pxn_selftest) picks the hand ones */
static void (*pxn_dot3x4)(const pxn_dot *) = pxn_dot3x4_c;
static void (*pxn_dot1)(const pxn_dot *) = pxn_dot1_c;
static void (*pxn_post_f)(const pxn_post *) = pxn_post_c;
static void (*pxn_gcopy)(int32_t *, const int32_t *, int, int) = pxn_gcopy_c;

/* ---- start-up check ---- */
/* the test pattern: X rows (9 of PXN_S + slack), weight rows, residual rows, both signs, values as large as a model
 * meets (+-32); scratch: PXN_TEST_WORDS */
#define PXN_T_X(s)   (s)
#define PXN_T_W(s)   ((s) + 10 * PXN_S)
#define PXN_T_RR(s)  (PXN_T_W(s) + 3 * PXN_S + 4)
#define PXN_T_O(s, k) (PXN_T_RR(s) + 12 + (k) * 64)       /* 8 output areas of 64 words */
static void pxn_test_data(int32_t *s)
{
    uint32_t r = 12345u;
    for (int i = 0; i < 13 * PXN_S + 16; i++) {
        r = r * 1103515245u + 12345u;
        s[i] = (int32_t)r >> 6;
    }
    for (int k = 0; k < 8; k++)
        for (int i = 0; i < 64; i++) PXN_T_O(s, k)[i] = i * 77 - 1000;
}
int pxn_selftest(int32_t *scratch)
{
#if PXN_ASM
    int32_t *s = scratch, ok = 1;
    for (int pass = 0; pass < 4 && ok; pass++) {
        pxn_test_data(s);
        const int T = pass & 1 ? 47 : 20, sb = PXN_S * 4;
        pxn_dot d = {PXN_T_W(s), PXN_T_X(s), T, sb, PXN_T_O(s, 0), 2};
        pxn_dot3x4_c(&d);
        d.z = PXN_T_O(s, 1); pxn_dot3x4_asm(&d);
        d.ng = 3; d.x = PXN_T_X(s) + PXN_S; d.z = PXN_T_O(s, 2) + 1; pxn_dot1_c(&d);
        d.z = PXN_T_O(s, 3) + 1; pxn_dot1_asm(&d);
        pxn_post p = {PXN_T_O(s, 0), PXN_T_X(s) + 2 + pass, sb, PXN_T_RR(s), PXN_T_O(s, 4), pass < 2 ? PXN_T_O(s, 5) : 0, 7};
        pxn_post_c(&p);
        p.hs = PXN_T_O(s, 6); if (p.out) p.out = PXN_T_O(s, 7);
        pxn_post_asm(&p);
        if (memcmp(PXN_T_O(s, 0), PXN_T_O(s, 1), 64 * 4) || memcmp(PXN_T_O(s, 2), PXN_T_O(s, 3), 64 * 4) ||
            memcmp(PXN_T_O(s, 4), PXN_T_O(s, 6), 64 * 4) || memcmp(PXN_T_O(s, 5), PXN_T_O(s, 7), 64 * 4)) ok = 0;
        pxn_gcopy_c(PXN_T_O(s, 0), PXN_T_W(s) + pass, 5, 40);
        pxn_gcopy_asm(PXN_T_O(s, 1), PXN_T_W(s) + pass, 5, 40);
        if (memcmp(PXN_T_O(s, 0), PXN_T_O(s, 1), 64 * 4)) ok = 0;
    }
    if (ok) { pxn_dot3x4 = pxn_dot3x4_asm; pxn_dot1 = pxn_dot1_asm; pxn_post_f = pxn_post_asm; pxn_gcopy = pxn_gcopy_asm; }
    return ok;
#else
    (void)scratch;
    return 0;
#endif
}
int pxn_asm_in_use(void)
{
#if PXN_ASM
    return pxn_dot3x4 == pxn_dot3x4_asm;
#else
    return 0;
#endif
}

/* ---- measurements for the firmware's bench screen (BENCH_AT_BOOT) ---- */
/* ticks for `reps` runs of the work of one kernel-6 layer over 32 frames (gather 6 taps, 3 dot rows of 20, post with
 * the residual): 1,920 multiply-adds a run. hand: the hand routines (if any), else C. scratch: PXN_TEST_WORDS */
uint32_t pxn_bench_layer(int32_t *scratch, uint32_t (*ticks)(void), int hand, int reps)
{
    static int32_t ring[3 * 64];
    int32_t *s = scratch;
    void (*d4)(const pxn_dot *) = pxn_dot3x4_c;
    void (*po)(const pxn_post *) = pxn_post_c;
    void (*gc)(int32_t *, const int32_t *, int, int) = pxn_gcopy_c;
#if PXN_ASM
    if (hand) { d4 = pxn_dot3x4_asm; po = pxn_post_asm; gc = pxn_gcopy_asm; }
#else
    (void)hand;
#endif
    pxn_test_data(s);
    uint32_t t0 = ticks();
    for (int r = 0; r < reps; r++) {
        for (int k = 0; k < 6; k++) gc(pxn_x + 2 + 3 * k, ring + 3 * k, 32, PXN_S * 4);
        pxn_dot d = {PXN_T_W(s), pxn_x, 20, PXN_S * 4, pxn_z, 8};
        d4(&d);
        pxn_post p = {pxn_z, pxn_x + 17, PXN_S * 4, PXN_T_RR(s), pxn_hs, ring, 32};
        po(&p);
    }
    return ticks() - t0;
}

#if PXN_ASM
/* one instruction n times in the hardware loop (n words read from p): the cost of each kind of instruction, from RAM
 * (_r) and from the flash (_x). fmac: acc += [p++] * r; ld: r = [p++]; rmac: acc += r * r; fmac2: two accumulators
 * from two pointers (p and p + 2 KB) in turn. */
#define PXN_UB(name, body, pre)                                                                                   \
    "\t.global " name "\n" name ":\n\t[--sp] = {rets, r15-r4}\n"                                              \
    "\tr4 = 0\n\tr5 = 0\n\tr6 = 0\n\tr7 = 0\n\tr3 = 5\n\tr10 = 3\n\tr12 = 4\n\tr2 = r0\n\tr11 = r1\n" pre      \
    ".L" name ":\n\trep r11 {\n" body "\t}\n\tif (r11 != 0) goto .L" name "\n"                                  \
    "\t{pc, r15-r4} = [sp++]\n"
#define PXN_UB_SET(sfx)                                                                                           \
    PXN_UB("pxn_ub_fmac" sfx, "\tr5_r4 += [r2 ++= r12]*r10 (s)\n", "")                                         \
    PXN_UB("pxn_ub_ld" sfx, "\tr3 = [r2++=4]\n", "")                                                            \
    PXN_UB("pxn_ub_rmac" sfx, "\tr5_r4 += r3 * r10 (s)\n", "")                                                  \
    PXN_UB("pxn_ub_fmac2" sfx, "\tr5_r4 += [r2 ++= r12]*r10 (s)\n\tr7_r6 += [r8 ++= r12]*r10 (s)\n",          \
           "\tr8 = r0 + 2048\n\tr11 = r1 >> 1\n")
__asm__("\t.section .ram_text,\"ax\"\n" PXN_UB_SET("_r") "\t.previous\n"
        "\t.text\n" PXN_UB_SET("_x"));
void pxn_ub_fmac_r(const int32_t *, int), pxn_ub_ld_r(const int32_t *, int), pxn_ub_rmac_r(const int32_t *, int),
    pxn_ub_fmac2_r(const int32_t *, int), pxn_ub_fmac_x(const int32_t *, int), pxn_ub_ld_x(const int32_t *, int),
    pxn_ub_rmac_x(const int32_t *, int), pxn_ub_fmac2_x(const int32_t *, int);
#endif
/* ticks for reps x 1024 instructions of `kind` (0 fmac, 1 ld, 2 rmac, 3 fmac2), from RAM (xip 0) or the flash (1);
 * buf: 1024 words (4 KB) of data. 0 where there is no hand-written code. */
uint32_t pxn_bench_ops(int kind, int xip, const int32_t *buf, int reps, uint32_t (*ticks)(void))
{
#if PXN_ASM
    static void (*const F[2][4])(const int32_t *, int) = {
        {pxn_ub_fmac_r, pxn_ub_ld_r, pxn_ub_rmac_r, pxn_ub_fmac2_r}, {pxn_ub_fmac_x, pxn_ub_ld_x, pxn_ub_rmac_x, pxn_ub_fmac2_x}};
    void (*volatile f)(const int32_t *, int) = F[xip ? 1 : 0][kind & 3];   /* through a pointer: RAM code */
    uint32_t t0 = ticks();
    for (int r = 0; r < reps; r++) f(buf, 1024);
    return ticks() - t0;
#else
    (void)kind; (void)xip; (void)buf; (void)reps; (void)ticks;
    return 0;
#endif
}

/* ---- loading ---- */
static int pxn_finite(float x)
{
    union { float f; uint32_t u; } v;
    v.f = x;
    return (v.u & 0x7F800000u) != 0x7F800000u;
}
/* to fixed point with q fraction bits, rounded and clamped (real A2 weights stay below 2) */
static int32_t pxn_toq(float x, int32_t q)
{
    float v = x * (float)(1u << q);
    if (v >= 2147483520.0f) return 2147483647;
    if (v <= -2147483520.0f) return -2147483647;
    return (int32_t)(v < 0.0f ? v - 0.5f : v + 0.5f);
}

/* the rings, largest first, each into the piece where it fits best (the smaller leftover) */
static int pxn_rings(pxn_model *m, int32_t *r0, unsigned n0, int32_t *r1, unsigned n1)
{
    unsigned need[PXN_NL + 1];
    for (int i = 0; i < PXN_NL; i++) need[i] = (unsigned)m->L[i].len * PXN_C * 4u;
    need[PXN_NL] = (unsigned)m->hlen * PXN_C * 4u;
    uint32_t done = 0;
    for (int t = 0; t <= PXN_NL; t++) {
        int b = -1;
        for (int i = 0; i <= PXN_NL; i++)
            if (!(done >> i & 1u) && (b < 0 || need[i] > need[b])) b = i;
        done |= 1u << b;
        int32_t **pp; unsigned *pn;
        const int f0 = n0 >= need[b], f1 = n1 >= need[b];
        if (!f0 && !f1) return -2;
        if (f0 && (!f1 || n0 <= n1)) { pp = &r0; pn = &n0; } else { pp = &r1; pn = &n1; }
        if (b == PXN_NL) m->hring = *pp; else m->L[b].ring = *pp;
        *pp += need[b] / 4u;
        *pn -= need[b];
    }
    return 0;
}

int pxn_load(pxn_model *m, const float *w, int32_t *r0, unsigned n0, int32_t *r1, unsigned n1)
{
    m->ready = 0;
    for (int i = 0; i < PXN_NW; i++)
        if (!pxn_finite(w[i])) return -1;
    const float *p = w;
    for (int c = 0; c < PXN_C; c++) m->rech[c] = pxn_toq(p[c], PXN_QW);
    p += PXN_C;
    int32_t off = 0;
    for (int i = 0; i < PXN_NL; i++) {
        pxn_layer *L = &m->L[i];
        const int K = pxn_k[i], T = 3 * K + 2;
        const float *conv = p, *cb = p + 9 * K, *mix = cb + 3, *one = mix + 3, *ob = one + 9;
        /* row o: [mixin, bias, then for each kernel tap k its 3 input channels], as the X rows. File: conv [o][c][k] */
        for (int o = 0; o < PXN_C; o++) {
            int32_t *d = m->wa + off + o * T;
            d[0] = pxn_toq(mix[o], PXN_QW);
            d[1] = pxn_toq(cb[o], PXN_QW);
            for (int k = 0; k < K; k++)
                for (int c = 0; c < PXN_C; c++) d[2 + 3 * k + c] = pxn_toq(conv[(o * PXN_C + c) * K + k], PXN_QW);
            for (int c = 0; c < PXN_C; c++) L->rr[o * 4 + c] = pxn_toq(one[o * PXN_C + c], PXN_QW);
            L->rr[o * 4 + 3] = pxn_toq(ob[o], PXN_QA);
        }
        L->k = (int16_t)K;
        L->d = (int16_t)pxn_d[i];
        L->off = off;
        L->len = (K - 1) * pxn_d[i] + PXN_N;
        off += 3 * T;
        p = ob + 3;
    }
    /* head row: [u (0), bias, then for each of its 16 taps the 3 channels]. File: head [c][k], bias, scale */
    m->head[0] = 0;
    m->head[1] = pxn_toq(p[PXN_C * PXN_HK], PXN_QW);
    for (int k = 0; k < PXN_HK; k++)
        for (int c = 0; c < PXN_C; c++) m->head[2 + 3 * k + c] = pxn_toq(p[c * PXN_HK + k], PXN_QW);
    m->out_gain = p[PXN_C * PXN_HK + 1] * (1.0f / 1048576.0f);
    m->hlen = (PXN_HK - 1) + PXN_N;
    if (pxn_rings(m, r0, n0, r1, n1)) return -2;
    pxn_reset(m);
    m->ready = 1;
    return 0;
}

void pxn_reset(pxn_model *m)
{
    for (int i = 0; i < PXN_NL; i++) {
        pxn_layer *L = &m->L[i];
        memset(L->ring, 0, (unsigned)L->len * PXN_C * 4u);
        L->w = 0;
    }
    memset(m->hring, 0, (unsigned)m->hlen * PXN_C * 4u);
    m->hw = 0;
}

/* ---- one block ---- */
/* the K taps (dilation d) of n frames from a ring (write position w) into X columns 2.. */
static void pxn_gather(const int32_t *ring, int32_t len, int32_t w, int K, int d, int n)
{
    for (int k = 0; k < K; k++) {
        int32_t s = w - (K - 1 - k) * d;
        if (s < 0) s += len;
        int n1 = len - s;
        if (n1 > n) n1 = n;
        pxn_gcopy(pxn_x + 2 + 3 * k, ring + 3 * s, n1, PXN_S * 4);
        if (n1 < n) pxn_gcopy(pxn_x + n1 * PXN_S + 2 + 3 * k, ring, n - n1, PXN_S * 4);
    }
}
/* rows weight rows (1, or the 3 of a layer, contiguous) over the n X rows -> pxn_z */
static void pxn_rows(const int32_t *w, int T, int rows, int n)
{
    const int n4 = rows == PXN_C ? n & ~3 : 0;
    pxn_dot d = {w, pxn_x, T, PXN_S * 4, pxn_z, n4 >> 2};
    if (n4) pxn_dot3x4(&d);
    if (n4 < n)
        for (int o = 0; o < rows; o++) {
            d.w = w + o * T; d.x = pxn_x + n4 * PXN_S; d.z = pxn_z + 3 * n4 + o; d.ng = n - n4;
            pxn_dot1(&d);
        }
}
static void pxn_layer_block(pxn_model *m, int i, int n)
{
    pxn_layer *L = &m->L[i];
    const int K = L->k, T = 3 * K + 2;
    pxn_gather(L->ring, L->len, L->w, K, L->d, n);
    pxn_rows(m->wa + L->off, T, PXN_C, n);
    pxn_post p = {pxn_z, pxn_x + 2 + 3 * (K - 1), PXN_S * 4, L->rr, pxn_hs, 0, n};
    if (i == PXN_NL - 1) pxn_post_f(&p);
    else {                                               /* into the next layer's ring, in up to two pieces */
        pxn_layer *N = &m->L[i + 1];
        int n1 = N->len - N->w;
        if (n1 > n) n1 = n;
        p.out = N->ring + 3 * N->w; p.n = n1;
        pxn_post_f(&p);
        if (n1 < n) {
            p.z = pxn_z + 3 * n1; p.x += n1 * PXN_S; p.hs = pxn_hs + 3 * n1; p.out = N->ring; p.n = n - n1;
            pxn_post_f(&p);
        }
    }
    L->w += n;
    if (L->w >= L->len) L->w -= L->len;
}
static void pxn_block(pxn_model *m, const float *in, float *out, int n)
{
    pxn_layer *L0 = &m->L[0];
    int32_t w0 = L0->w;
    for (int f = 0; f < n; f++) {                        /* Q20, clamped to +-64 (far above any guitar) */
        float v = in[f] * 1048576.0f;
        const int32_t u = v >= 67108864.0f ? 67108864 : v <= -67108864.0f ? -67108864 : (int32_t)v;
        int32_t *x = pxn_x + f * PXN_S, *r = L0->ring + 3 * w0;
        x[0] = u;
        x[1] = PXN_ONE;
        for (int c = 0; c < PXN_C; c++) r[c] = (int32_t)(((int64_t)m->rech[c] * u) >> PXN_QW);
        if (++w0 == L0->len) w0 = 0;
    }
    memset(pxn_hs, 0, (unsigned)n * PXN_C * 4u);
    for (int i = 0; i < PXN_NL; i++) pxn_layer_block(m, i, n);
    /* head: the activation sums into its ring, 16 taps gathered, one row */
    int n1 = m->hlen - m->hw;
    if (n1 > n) n1 = n;
    memcpy(m->hring + 3 * m->hw, pxn_hs, (unsigned)n1 * PXN_C * 4u);
    if (n1 < n) memcpy(m->hring, pxn_hs + 3 * n1, (unsigned)(n - n1) * PXN_C * 4u);
    m->hw += n;
    if (m->hw >= m->hlen) m->hw -= m->hlen;
    int32_t hw = m->hw - n;
    if (hw < 0) hw += m->hlen;
    pxn_gather(m->hring, m->hlen, hw, PXN_HK, 1, n);
    pxn_rows(m->head, PXN_S, 1, n);
    const float g = m->out_gain;
    for (int f = 0; f < n; f++) out[f] = (float)pxn_z[3 * f] * g;
}

void pxn_process(pxn_model *m, const float *in, float *out, int n)
{
    if (!m->ready) {
        if (out != in) memcpy(out, in, (unsigned)n * sizeof(float));
        return;
    }
    while (n > 0) {
        const int c = n < PXN_N ? n : PXN_N;
        pxn_block(m, in, out, c);
        in += c;
        out += c;
        n -= c;
    }
}
