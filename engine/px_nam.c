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
    const int32_t *rr2;                 /* 28: the same residual for pxn_post2_asm: w[o][c] by c then o, then 3 biases */
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

/* the residual reordered for post2: w[o][c] by c then o, then the 3 biases */
static void pxn_rr2(const int32_t *rr, int32_t *r2)
{
    for (int c = 0; c < PXN_C; c++)
        for (int o = 0; o < PXN_C; o++) r2[3 * c + o] = rr[4 * o + c];
    for (int o = 0; o < PXN_C; o++) r2[9 + o] = rr[4 * o + 3];
}

#if defined(__PI32V2__) && !defined(PXN_NO_ASM)
#define PXN_ASM 1
/* ---- the same, by hand (in the flash, with their callers: no call-range question) ---- */
void pxn_dot3x4_asm(const pxn_dot *j);
void pxn_dot1_asm(const pxn_dot *j);
void pxn_post_asm(const pxn_post *j);
void pxn_post2_asm(const pxn_post *j);
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
    /* post2 (v37): the same as post, with fewer memory steps: z, hs and x moved 3 words by one instruction, the
     * residual's 9 multiply-adds in 3 accumulators, each weight loaded in the same step as the multiply before it (a
     * load paired with a multiply takes r0..r7 only). r1 z, r2 x, r3 hs, r7 out, r5 weights, the activations in
     * r4 r14 r15; [sp+0] frames left, [sp+4] x stride - 12 */
    "\t.global pxn_post2_asm\n"
    "pxn_post2_asm:\n"
    "\t[--sp] = {rets, r15-r4}\n"
    "\tsp += -8\n"
    "\tr1 = [r0+0]\n\tr2 = [r0+4]\n\tr3 = [r0+16]\n\tr7 = [r0+20]\n"
    "\tr15 = [r0+24]\n\t[sp+0] = r15\n"
    "\tr15 = [r0+8]\n\tr15 += -12\n\t[sp+4] = r15\n"
    "\tif (r7 == 0) goto .Lpxn_p2h\n"
    ".Lpxn_p2f:\n"
    "\t{r6-r4} = [r1++]\n"
    "\tr14 = 42949673\n"
    "\tifs (r4 < 0) {\n\tr9_r8 = r4 * r14 (s)\n\tr4 = r9\n\t}\n"
    "\tifs (r5 < 0) {\n\tr9_r8 = r5 * r14 (s)\n\tr5 = r9\n\t}\n"
    "\tifs (r6 < 0) {\n\tr9_r8 = r6 * r14 (s)\n\tr6 = r9\n\t}\n"
    "\t{r10-r8} = [r3++]\n"
    "\tr8 += r4\n\tr9 += r5\n\tr10 += r6\n"
    "\t[r3+-12] = r8\n\t[r3+-8] = r9\n\t[r3+-4] = r10\n"
    "\tr14 = r5\n\tr15 = r6\n"
    "\tr5 = [r0+28]\n"
    "\tr6 = [r5++=4]\n"
    "\tr9_r8 = r6 * r4 (s)  #  r6 = [r5++=4]\n"
    "\tr11_r10 = r6 * r4 (s)  #  r6 = [r5++=4]\n"
    "\tr13_r12 = r6 * r4 (s)  #  r6 = [r5++=4]\n"
    "\tr9_r8 += r6 * r14 (s)  #  r6 = [r5++=4]\n"
    "\tr11_r10 += r6 * r14 (s)  #  r6 = [r5++=4]\n"
    "\tr13_r12 += r6 * r14 (s)  #  r6 = [r5++=4]\n"
    "\tr9_r8 += r6 * r15 (s)  #  r6 = [r5++=4]\n"
    "\tr11_r10 += r6 * r15 (s)  #  r6 = [r5++=4]\n"
    "\tr13_r12 += r6 * r15 (s)  #  r6 = [r5++=4]\n"
    "\tr9_r8 >>= 24\n\tr11_r10 >>= 24\n\tr13_r12 >>= 24\n"
    "\tr8 += r6\n\tr6 = [r5++=4]\n"
    "\tr10 += r6\n\tr6 = [r5+0]\n"
    "\tr12 += r6\n"
    "\t{r6-r4} = [r2++]\n"
    "\tr8 += r4\n\tr10 += r5\n\tr12 += r6\n"
    "\t[r7++=4] = r8\n\t[r7++=4] = r10\n\t[r7++=4] = r12\n"
    "\tr15 = [sp+4]\n\tr2 += r15\n"
    "\tr15 = [sp+0]\n\tr15 += -1\n\t[sp+0] = r15\n"
    "\tif (r15 != 0) goto .Lpxn_p2f\n"
    "\tgoto .Lpxn_p2e\n"
    ".Lpxn_p2h:\n"
    "\t{r6-r4} = [r1++]\n"
    "\tr14 = 42949673\n"
    "\tifs (r4 < 0) {\n\tr9_r8 = r4 * r14 (s)\n\tr4 = r9\n\t}\n"
    "\tifs (r5 < 0) {\n\tr9_r8 = r5 * r14 (s)\n\tr5 = r9\n\t}\n"
    "\tifs (r6 < 0) {\n\tr9_r8 = r6 * r14 (s)\n\tr6 = r9\n\t}\n"
    "\t{r10-r8} = [r3++]\n"
    "\tr8 += r4\n\tr9 += r5\n\tr10 += r6\n"
    "\t[r3+-12] = r8\n\t[r3+-8] = r9\n\t[r3+-4] = r10\n"
    "\tr15 = [sp+0]\n\tr15 += -1\n\t[sp+0] = r15\n"
    "\tif (r15 != 0) goto .Lpxn_p2h\n"
    ".Lpxn_p2e:\n"
    "\tsp += 8\n"
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

/* ---- the chip's vector unit (in its FFT block, 0x1EE0400): dot products in hardware ----
 * One command computes nloop dot products of nlen 32-bit pairs, z = sum(y * x) >> q, the x, y and z addresses moving by
 * their own byte steps between loops: a layer's weight row (y, still) against its X rows (x, one row a loop), into
 * pxn_z (z, 3 words a loop). The register layout is the one in JieLi's AC79 SDK (apps/common/jl_math, Apache-2.0):
 * control 0xAA1 = dot product, x y z 32 bits; config0 = q << 24 | nloop << 12 | nlen; config1 = z step << 20 |
 * y step << 10 | x step; start 0x149, done = bit 7, cleared by setting bit 6. It runs beside the CPU. Used only if
 * the start-up check finds it there and within 1 of the C result on every value. */
static uint8_t pxn_vec_ok, pxn_vfail __attribute__((unused));
static int32_t pxn_vec_diff = -1;                       /* the start-up check's largest difference (-1: not run) */
static int pxn_mode_v;                                  /* see pxn_mode() */
int pxn_mode(void) { return pxn_mode_v; }
static uint8_t pxn_y16;                                 /* the unit reads the 16-bit weight rows (start-up check) */
static void pxn_q16(int32_t *w, int16_t *w16, uint8_t *q, int T);
int pxn_y16_in_use(void) { return pxn_y16; }
/* a command: nloop n dot products of nlen T: the weight row (32-bit w32, or with pxn_y16 the 16-bit w16 shifted by q)
 * against x rows xs bytes apart, into z (zs bytes apart) */
static void pxn_vfill(volatile uint32_t *c, const int32_t *w32, const int16_t *w16, int q, const int32_t *x, int T,
                      int xs, int zs, int32_t *z, int n)
{
    const int h = pxn_y16 && w16;
    c[0] = h ? 0x9A1u : 0xAA1u;                          /* dot product, x and z 32 bits, y 16 or 32 */
    c[1] = (uint32_t)(uintptr_t)x;
    c[2] = h ? (uint32_t)(uintptr_t)w16 : (uint32_t)(uintptr_t)w32;
    c[3] = (uint32_t)(uintptr_t)z;
    c[4] = ((uint32_t)(h ? q : PXN_QW) << 24) | ((uint32_t)n << 12) | (uint32_t)T;
    c[5] = ((uint32_t)zs << 20) | (uint32_t)xs;
    c[6] = 0;
    c[7] = 0;
}
#if PXN_ASM
#define PXN_FFT_CON   (*(volatile uint32_t *)0x1EE0400u)
#define PXN_FFT_CADR  (*(volatile uint32_t *)0x1EE0404u)
#define PXN_FFT_CONST (*(volatile uint32_t *)0x1EE0408u)
static volatile uint32_t pxn_vctx[8] __attribute__((aligned(32)));
/* 0 = done, -1 = no answer (the unit is not there or not clocked) */
static int pxn_vdot2(const int32_t *w, const int16_t *w16, int q, const int32_t *x, int T, int sb, int32_t *z, int n)
{
    pxn_vfill(pxn_vctx, w, w16, q, x, T, sb, 12, z, n);
    __asm__ volatile("csync" ::: "memory");
    PXN_FFT_CONST = 0;
    PXN_FFT_CADR = (uint32_t)(uintptr_t)pxn_vctx;
    PXN_FFT_CON = 0x149u;
    for (uint32_t k = 0; !(PXN_FFT_CON & 0x80u); k++)
        if (k > 400000u) { PXN_FFT_CON = 0; return -1; }
    PXN_FFT_CON |= 0x40u;
    return 0;
}
static int pxn_vdot(const int32_t *w, const int32_t *x, int T, int sb, int32_t *z, int n)
{
    return pxn_vdot2(w, 0, 0, x, T, sb, z, n);
}
/* the same, without waiting (the bench: what runs beside it); pxn_vwait waits for it */
void pxn_vstart(const int32_t *w, const int32_t *x, int T, int sb, int zb, int32_t *z, int n)
{
    pxn_vfill(pxn_vctx, w, 0, 0, x, T, sb, zb, z, n);
    __asm__ volatile("csync" ::: "memory");
    PXN_FFT_CONST = 0;
    PXN_FFT_CADR = (uint32_t)(uintptr_t)pxn_vctx;
    PXN_FFT_CON = 0x149u;
}
static void pxn_vstart16(const int16_t *w16, int q, const int32_t *x, int T, int sb, int zb, int32_t *z, int n)
{
    const uint8_t was = pxn_y16;
    pxn_y16 = 1;
    pxn_vfill(pxn_vctx, 0, w16, q, x, T, sb, zb, z, n);
    pxn_y16 = was;
    __asm__ volatile("csync" ::: "memory");
    PXN_FFT_CONST = 0;
    PXN_FFT_CADR = (uint32_t)(uintptr_t)pxn_vctx;
    PXN_FFT_CON = 0x149u;
}
void pxn_vwait(void)
{
    for (uint32_t k = 0; !(PXN_FFT_CON & 0x80u); k++)
        if (k > 4000000u) { PXN_FFT_CON = 0; return; }
    PXN_FFT_CON |= 0x40u;
}
#else
void pxn_vstart(const int32_t *w, const int32_t *x, int T, int sb, int zb, int32_t *z, int n)
{
    (void)w; (void)x; (void)T; (void)sb; (void)zb; (void)z; (void)n;
}
void pxn_vwait(void) { }
#endif
/* the command queue (mode 2): commands wait in their own contexts; pxn_kick, called often by the CPU's work, starts the
 * next one as soon as the unit is done. Counters: queued, started, finished. */
#if PXN_ASM || defined(PXN_EMUVEC)
#define PXN_VQON 1
#define PXN_VQ 8
static volatile uint32_t pxn_vq[PXN_VQ][8] __attribute__((aligned(32)));
static uint32_t pxn_vq_in, pxn_vq_st, pxn_vq_done;
#if PXN_ASM
static inline __attribute__((always_inline)) void pxn_kick(void)
{
    if (pxn_vq_st != pxn_vq_done) {
        if (!(PXN_FFT_CON & 0x80u)) return;
        PXN_FFT_CON |= 0x40u;
        pxn_vq_done++;
    }
    if (pxn_vq_st != pxn_vq_in) {
        __asm__ volatile("csync" ::: "memory");
        PXN_FFT_CONST = 0;
        PXN_FFT_CADR = (uint32_t)(uintptr_t)pxn_vq[pxn_vq_st % PXN_VQ];
        PXN_FFT_CON = 0x149u;
        pxn_vq_st++;
    }
}
#define PXN_VSTOP() (PXN_FFT_CON = 0)
#else
/* PC tests (PXN_EMUVEC): a unit that finishes each command at some later kick, at random, with the C dot product
 * reading the X rows then: a row overwritten too early shows as a wrong result */
static struct { const int32_t *w, *x; int32_t *z; } pxn_vqp[PXN_VQ];
static uint32_t pxn_emu_r = 1u;
static void pxn_kick(void)
{
    if (pxn_vq_st != pxn_vq_done) {
        pxn_emu_r = pxn_emu_r * 1103515245u + 12345u;
        if ((pxn_emu_r >> 27) & 3u) return;
        const int k = (int)(pxn_vq_done % PXN_VQ);
        pxn_dot d = {pxn_vqp[k].w, pxn_vqp[k].x, (int32_t)(pxn_vq[k][4] & 0xFFFu), PXN_S * 4, pxn_vqp[k].z,
                     (int32_t)(pxn_vq[k][4] >> 12 & 0xFFFu)};
        pxn_dot1_c(&d);
        pxn_vq_done++;
    }
    if (pxn_vq_st != pxn_vq_in) pxn_vq_st++;
}
#define PXN_VSTOP() ((void)0)
void pxn_emu_on(int mode) { pxn_vec_ok = 1; pxn_vfail = 0; pxn_mode_v = mode; }
void pxn_emu_y16(void) { pxn_y16 = 1; }   /* the models loaded with 16-bit rows (then compare like with like) */
#endif
/* the unit stopped answering: off for good (the CPU does the rest), queue emptied */
static void pxn_vdead(void)
{
    PXN_VSTOP();
    pxn_vfail = 1;
    pxn_vec_ok = 0;
    pxn_vq_in = pxn_vq_st = pxn_vq_done = 0;
}
static int pxn_vupto(uint32_t seq)                      /* waits until `seq` commands are finished; -1 = unit dead */
{
    for (uint32_t k = 0; (int32_t)(pxn_vq_done - seq) < 0; k++) {
        pxn_kick();
        if (k > 2000000u) { pxn_vdead(); return -1; }
    }
    return 0;
}
static void pxn_venq(const int32_t *w, const int16_t *w16, int q, const int32_t *x, int T, int32_t *z, int n)
{
    if (pxn_vq_in - pxn_vq_done >= PXN_VQ && pxn_vupto(pxn_vq_in - PXN_VQ + 1)) return;
    volatile uint32_t *c = pxn_vq[pxn_vq_in % PXN_VQ];
    pxn_vfill(c, w, w16, q, x, T, PXN_S * 4, 12, z, n);   /* (on the PC: pxn_vqp, the 32-bit row) */
#if !PXN_ASM
    pxn_vqp[pxn_vq_in % PXN_VQ].w = w; pxn_vqp[pxn_vq_in % PXN_VQ].x = x; pxn_vqp[pxn_vq_in % PXN_VQ].z = z;
#endif
    pxn_vq_in++;
    pxn_kick();
}
#else
#define PXN_VQON 0
#define pxn_kick() ((void)0)
#endif
int pxn_vec_check(void) { return pxn_vec_ok ? (int)pxn_vec_diff : -1 - (pxn_vec_diff >= 0 ? (int)pxn_vec_diff : 0); }

/* ---- start-up check ---- */
/* the test pattern: X rows (9 of PXN_S + slack), weight rows, residual rows, both signs, values as large as a model
 * meets (+-32); scratch: PXN_TEST_WORDS */
#define PXN_T_X(s)   (s)
#define PXN_T_W(s)   ((s) + 10 * PXN_S)
#define PXN_T_RR(s)  (PXN_T_W(s) + 3 * PXN_S + 4)
#define PXN_T_O(s, k) (PXN_T_RR(s) + 12 + (k) * 64)       /* 8 output areas of 64 words */
static void pxn_test_data_sh(int32_t *s, int sh)
{
    uint32_t r = 12345u;
    for (int i = 0; i < 13 * PXN_S + 16; i++) {
        r = r * 1103515245u + 12345u;
        s[i] = (int32_t)r >> sh;
    }
    for (int k = 0; k < 8; k++)
        for (int i = 0; i < 64; i++) PXN_T_O(s, k)[i] = i * 77 - 1000;
}
static void pxn_test_data(int32_t *s) { pxn_test_data_sh(s, 6); }
int pxn_selftest(int32_t *scratch)
{
#if PXN_ASM
    int32_t *s = scratch, ok = 1, p2ok = 1;
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
        {   /* post2 against the C post (same z, x, residual; hs and out from the same start): z is O0, still
             * the C dot's; hs and out in O2 and O3, whose check is done */
            int32_t *r2 = PXN_T_O(s, 8);
            pxn_rr2(PXN_T_RR(s), r2);
            for (int i = 0; i < 64; i++) PXN_T_O(s, 2)[i] = PXN_T_O(s, 3)[i] = i * 77 - 1000;
            pxn_post q = p;
            q.hs = PXN_T_O(s, 2); if (q.out) q.out = PXN_T_O(s, 3);
            q.rr2 = r2;
            pxn_post2_asm(&q);
            if (memcmp(PXN_T_O(s, 2), PXN_T_O(s, 4), 64 * 4) || memcmp(PXN_T_O(s, 3), PXN_T_O(s, 5), 64 * 4)) p2ok = 0;
        }
        pxn_gcopy_c(PXN_T_O(s, 0), PXN_T_W(s) + pass, 5, 40);
        pxn_gcopy_asm(PXN_T_O(s, 1), PXN_T_W(s) + pass, 5, 40);
        if (memcmp(PXN_T_O(s, 0), PXN_T_O(s, 1), 64 * 4)) ok = 0;
    }
    if (ok) {
        pxn_dot3x4 = pxn_dot3x4_asm; pxn_dot1 = pxn_dot1_asm; pxn_gcopy = pxn_gcopy_asm;
        pxn_post_f = p2ok ? pxn_post2_asm : pxn_post_asm;
    }
    /* the vector unit: rows of 20 and 47 over 8 X rows, against the C dot */
    {
        int vok = 1;
        int32_t maxd = 0;
        for (int pass = 0; pass < 2 && vok; pass++) {
            pxn_test_data_sh(s, 9);                       /* +-4 (the z values a model gives fit in 32 bits) */
            const int T = pass ? 47 : 20;
            pxn_dot d = {PXN_T_W(s), PXN_T_X(s), T, PXN_S * 4, PXN_T_O(s, 0), 8};
            pxn_dot1_c(&d);
            if (pxn_vdot(PXN_T_W(s), PXN_T_X(s), T, PXN_S * 4, PXN_T_O(s, 1), 8)) { vok = 0; maxd = 99999; break; }
            for (int f = 0; f < 8; f++) {
                int32_t df = PXN_T_O(s, 0)[3 * f] - PXN_T_O(s, 1)[3 * f];
                if (df < 0) df = -df;
                if (df > maxd) maxd = df;
            }
        }
        pxn_vec_diff = maxd;
        pxn_vec_ok = (uint8_t)(vok && maxd <= 1);
        pxn_vfail = 0;
        pxn_mode_v = pxn_vec_ok ? 1 : 0;
        /* the 16-bit weight rows: the same rows put onto 16 bits (pxn_q16), the unit against the C dot of their
         * 32-bit form; used if within 1 everywhere. Then the models are loaded that way (pxn_load). */
        pxn_y16 = 0;
        int y16 = pxn_vec_ok;
        for (int pass = 0; pass < 2 && y16; pass++) {
            pxn_test_data_sh(s, 9);
            const int T = pass ? 47 : 20;
            int32_t *w = PXN_T_O(s, 6);
            int16_t *w16 = (int16_t *)PXN_T_O(s, 7);
            uint8_t q;
            memcpy(w, PXN_T_W(s), (unsigned)T * 4u);
            pxn_q16(w, w16, &q, T);
            pxn_dot d = {w, PXN_T_X(s), T, PXN_S * 4, PXN_T_O(s, 0), 8};
            pxn_dot1_c(&d);
            pxn_y16 = 1;
            const int bad = pxn_vdot2(w, w16, q, PXN_T_X(s), T, PXN_S * 4, PXN_T_O(s, 1), 8);
            pxn_y16 = 0;
            if (bad) { y16 = 0; break; }
            for (int f = 0; f < 8; f++) {
                int32_t df = PXN_T_O(s, 0)[3 * f] - PXN_T_O(s, 1)[3 * f];
                if (df < -1 || df > 1) y16 = 0;
            }
        }
        pxn_y16 = (uint8_t)y16;
    }
    return ok;
#else
    (void)scratch;
    return 0;
#endif
}
int pxn_post2_in_use(void)
{
#if PXN_ASM
    return pxn_post_f == pxn_post2_asm;
#else
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
        pxn_post p = {pxn_z, pxn_x + 17, PXN_S * 4, PXN_T_RR(s), pxn_hs, ring, 32, 0};
        po(&p);
    }
    return ticks() - t0;
}

/* ticks for `reps` runs of one part of that layer work, by hand (C without hand code): 0 the dot (3 rows of 20 for
 * 32 frames: 1,920 multiply-adds), 1 the gather (6 taps x 32 frames), 2 the post (32 frames, with the residual) */
uint32_t pxn_bench_part(int32_t *scratch, uint32_t (*ticks)(void), int part, int reps)
{
    static int32_t ring[3 * 64];
    int32_t *s = scratch;
    pxn_test_data(s);
    pxn_dot d = {PXN_T_W(s), pxn_x, 20, PXN_S * 4, pxn_z, 8};
    pxn_post p = {pxn_z, pxn_x + 17, PXN_S * 4, PXN_T_RR(s), pxn_hs, ring, 32, 0};
    uint32_t t0 = ticks();
    for (int r = 0; r < reps; r++) {
#if PXN_ASM
        if (part == 3) { for (int o = 0; o < 3; o++) pxn_vdot(d.w + 20 * o, pxn_x, 20, PXN_S * 4, pxn_z + o, 32); }
        else if (part == 0) pxn_dot3x4_asm(&d);
        else if (part == 1) for (int k = 0; k < 6; k++) pxn_gcopy_asm(pxn_x + 2 + 3 * k, ring + 3 * k, 32, PXN_S * 4);
        else pxn_post_asm(&p);
#else
        if (part == 0) pxn_dot3x4_c(&d);
        else if (part == 1) for (int k = 0; k < 6; k++) pxn_gcopy_c(pxn_x + 2 + 3 * k, ring + 3 * k, 32, PXN_S * 4);
        else pxn_post_c(&p);
#endif
    }
    return ticks() - t0;
}

/* ticks for `reps` runs of a layer's dot work over 32 frames (3 rows of 20: 1,920 multiply-adds), on buffers given
 * (x: 33 rows of PXN_S words, w: 64 words, z: 100 words), by the CPU routine (vec 0) or the vector unit (vec 1);
 * then of the post over 32 frames on one buffer (4,000 words: z, X rows, rr, hs, out). For the bench screen:
 * where the data sits changes the cost on this chip. */
uint32_t pxn_bench_dot(int32_t *x, int32_t *w, int32_t *z, int vec, uint32_t (*ticks)(void), int reps)
{
    uint32_t r = 777u;
    for (int i = 0; i < 33 * PXN_S; i++) { r = r * 1103515245u + 12345u; x[i] = (int32_t)r >> 10; }
    for (int i = 0; i < 64; i++) { r = r * 1103515245u + 12345u; w[i] = (int32_t)r >> 10; }
    pxn_dot d = {w, x, 20, PXN_S * 4, z, 8};
    uint32_t t0 = ticks();
    for (int k = 0; k < reps; k++) {
#if PXN_ASM
        if (vec) { for (int o = 0; o < 3; o++) pxn_vdot(w + 20 * o, x, 20, PXN_S * 4, z + o, 32); }
        else pxn_dot3x4_asm(&d);
#else
        (void)vec;
        pxn_dot3x4_c(&d);
#endif
    }
    return ticks() - t0;
}
uint32_t pxn_bench_post(int32_t *b, uint32_t (*ticks)(void), int reps, int v2)
{
    int32_t *z = b, *x = b + 100, *rr = b + 100 + 33 * PXN_S, *rr2 = rr + 12, *hs = rr + 32, *out = hs + 100;
    uint32_t r = 99u;
    for (int i = 0; i < 4000; i++) { r = r * 1103515245u + 12345u; b[i] = (int32_t)r >> 10; }
    pxn_rr2(rr, rr2);
    pxn_post p = {z, x + 17, PXN_S * 4, rr, hs, out, 32, rr2};
    uint32_t t0 = ticks();
    for (int k = 0; k < reps; k++) {
#if PXN_ASM
        if (v2) pxn_post2_asm(&p); else pxn_post_asm(&p);
#else
        (void)v2;
        pxn_post_c(&p);
#endif
    }
    return ticks() - t0;
}
/* ticks for `reps` vector commands of 32 dot products of 20 words, x rows xs bytes apart, from the x and w given,
 * waited for; y16: w read as 16-bit weights */
uint32_t pxn_bench_vplace(const int32_t *x, const int32_t *w, int32_t *z, int xs, uint32_t (*ticks)(void), int reps, int y16)
{
#if PXN_ASM
    uint32_t t0 = ticks();
    for (int k = 0; k < reps; k++) {
        if (y16) pxn_vstart16((const int16_t *)w, 20, x, 20, xs, 12, z, 32); else pxn_vstart(w, x, 20, xs, 12, z, 32);
        pxn_vwait();
    }
    return ticks() - t0;
#else
    (void)x; (void)w; (void)z; (void)xs; (void)ticks; (void)reps; (void)y16;
    return 0;
#endif
}
/* ticks for `reps` vector commands of nloop dot products of nlen words (x moving xs bytes and z zs bytes a loop),
 * each waited for; start_only: only the CPU's part of starting one (it is waited for outside the timing).
 * buf: 40,000 words (x from 0, the weights at 30,000, z at 31,000) */
uint32_t pxn_bench_vcmd(int32_t *buf, uint32_t (*ticks)(void), int nlen, int nloop, int xs, int zs, int reps, int start_only)
{
#if PXN_ASM
    int32_t *x = buf, *w = buf + 30000, *z = buf + 31000;
    for (int i = 0; i < 1000; i++) { w[i] = i * 13 - 6000; x[i] = 3000 - i * 7; }
    uint32_t dt = 0;
    if (start_only) {
        for (int k = 0; k < reps; k++) {
            uint32_t t0 = ticks();
            pxn_vstart(w, x, nlen, xs, zs, z, nloop);
            dt += ticks() - t0;
            pxn_vwait();
        }
        return dt;
    }
    uint32_t t0 = ticks();
    for (int k = 0; k < reps; k++) { pxn_vstart(w, x, nlen, xs, zs, z, nloop); pxn_vwait(); }
    return ticks() - t0;
#else
    (void)buf; (void)ticks; (void)nlen; (void)nloop; (void)xs; (void)zs; (void)reps; (void)start_only;
    return 0;
#endif
}

#if PXN_ASM
/* one instruction (or pair) n times in the hardware loop: what each kind costs on this CPU */
#define PXN_UB(name, body)                                                                                        \
    "\t.global " name "\n" name ":\n\t[--sp] = {rets, r15-r4}\n"                                              \
    "\tr4 = 0\n\tr5 = 0\n\tr6 = 0\n\tr7 = 0\n\tr3 = 5\n\tr10 = 3\n\tr12 = 4\n\tr2 = r0\n\tr11 = r1\n"          \
    ".L" name ":\n\trep r11 {\n" body "\t}\n\tif (r11 != 0) goto .L" name "\n"                                  \
    "\t{pc, r15-r4} = [sp++]\n"
__asm__("\t.text\n"
        PXN_UB("pxn_ub_ld", "\tr3 = [r2++=4]\n")
        PXN_UB("pxn_ub_ldd", "\tr5_r4 = d[r2++=8]\n")
        PXN_UB("pxn_ub_ldm3", "\t{r6-r4} = [r2++]\n")
        PXN_UB("pxn_ub_st", "\t[r2++=4] = r3\n")
        PXN_UB("pxn_ub_rmac", "\tr5_r4 += r3 * r10 (s)\n")
        PXN_UB("pxn_ub_pair", "\tr5_r4 += r3 * r10 (s)  #  r3 = [r2++=4]\n")
        PXN_UB("pxn_ub_fmac", "\tr5_r4 += [r2 ++= r12]*r10 (s)\n")
        PXN_UB("pxn_ub_rmac2", "\tr5_r4 += r3 * r10 (s)\n\tr7_r6 += r3 * r10 (s)\n"));
void pxn_ub_ld(const int32_t *, int), pxn_ub_ldd(const int32_t *, int), pxn_ub_ldm3(const int32_t *, int),
    pxn_ub_st(const int32_t *, int), pxn_ub_rmac(const int32_t *, int), pxn_ub_pair(const int32_t *, int),
    pxn_ub_fmac(const int32_t *, int), pxn_ub_rmac2(const int32_t *, int);
#endif
/* ticks for reps x n loops of `kind`: 0 load, 1 64-bit load, 2 3-word load, 3 store, 4 multiply-add (registers),
 * 5 multiply-add paired with a load, 6 multiply-add with its load, 7 two multiply-adds; buf: 1024 words. n: the
 * loop count used (written). 0 without hand code. */
uint32_t pxn_bench_ops(int kind, int32_t *buf, int reps, int *n, uint32_t (*ticks)(void))
{
#if PXN_ASM
    static void (*const F[8])(const int32_t *, int) = {pxn_ub_ld, pxn_ub_ldd, pxn_ub_ldm3, pxn_ub_st,
                                                        pxn_ub_rmac, pxn_ub_pair, pxn_ub_fmac, pxn_ub_rmac2};
    static const int N[8] = {1024, 512, 340, 1024, 1024, 1024, 1024, 1024};
    *n = N[kind & 7];
    uint32_t t0 = ticks();
    for (int r = 0; r < reps; r++) F[kind & 7](buf, *n);
    return ticks() - t0;
#else
    (void)kind; (void)buf; (void)reps; (void)ticks;
    *n = 1;
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

/* a weight row onto 16 bits: the row's largest weight sets a shift sh (0 if all fit), each weight rounded to a multiple
 * of 2^sh, so that w = w16 << sh exactly: the vector unit reads the 16-bit row (half the reads) with z = sum >> (24 - sh),
 * the CPU the 32-bit one, and both give the same result. About 15 bits for the row's largest weight. */
static void pxn_q16(int32_t *w, int16_t *w16, uint8_t *q, int T)
{
    int32_t mx = 0;
    for (int t = 0; t < T; t++) { int32_t a = w[t] < 0 ? -w[t] : w[t]; if (a > mx) mx = a; }
    int sh = 0;
    while ((mx >> sh) > 32767 && sh < 24) sh++;
    for (int t = 0; t < T; t++) {
        int64_t r = sh ? (((int64_t)w[t] + ((int64_t)1 << (sh - 1))) >> sh) : w[t];
        if (r > 32767) r = 32767;
        if (r < -32768) r = -32768;
        w16[t] = (int16_t)r;
        w[t] = (int32_t)(r * ((int64_t)1 << sh));
    }
    *q = (uint8_t)(PXN_QW - sh);
}
int pxn_load(pxn_model *m, const float *w, int32_t *r0, unsigned n0, int32_t *r1, unsigned n1)
{
    m->ready = 0;
    for (int i = 0; i < PXN_NW; i++)
        if (!pxn_finite(w[i])) return -1;
    const float *p = w;
    for (int c = 0; c < PXN_C; c++) m->rech[c] = pxn_toq(p[c], PXN_QW);
    p += PXN_C;
    int32_t off = 0, off16 = 0;
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
        pxn_rr2(L->rr, L->rr2);
        L->k = (int16_t)K;
        L->d = (int16_t)pxn_d[i];
        L->off = off;
        L->len = (K - 1) * pxn_d[i] + PXN_N;
        L->off16 = off16;
        for (int o = 0; o < PXN_C; o++) {
            if (pxn_y16) pxn_q16(m->wa + off + o * T, m->wa16 + off16 + o * PXN_T2(T), &L->q[o], T);
            else L->q[o] = PXN_QW;
        }
        off += 3 * T;
        off16 += 3 * PXN_T2(T);
        p = ob + 3;
    }
    /* head row: [u (0), bias, then for each of its 16 taps the 3 channels]. File: head [c][k], bias, scale */
    m->head[0] = 0;
    m->head[1] = pxn_toq(p[PXN_C * PXN_HK], PXN_QW);
    for (int k = 0; k < PXN_HK; k++)
        for (int c = 0; c < PXN_C; c++) m->head[2 + 3 * k + c] = pxn_toq(p[c * PXN_HK + k], PXN_QW);
    if (pxn_y16) pxn_q16(m->head, m->head16, &m->hq, PXN_S); else m->hq = PXN_QW;
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
/* how each mode runs (pxn_tune picks one): h > 0: two cores, the helper core takes frames 0 .. h-1 (it never waits:
 * earlier frames never need later ones), the calling core the rest; pipe: the vector unit beside the calling core
 * (block or range in two halves), the CPU taking c frames of each half's dot products itself */
static const struct { uint8_t h, pipe, c; } PXN_MC[PXN_MODES] = {
    {0, 0, 0}, {0, 0, 0}, {0, 1, 0}, {0, 1, 4}, {0, 1, 8}, {16, 0, 0}, {12, 1, 0}, {16, 1, 0}, {12, 1, 4}};
/* the K taps (dilation d) of frames f0 .. f0+n-1 of the block (the block starts at ring position w) into X columns 2..;
 * kick: this core drives the vector unit */
static void pxn_gather(const int32_t *ring, int32_t len, int32_t w, int K, int d, int f0, int n, int kick)
{
    for (int k = 0; k < K; k++) {
        int32_t s = w + f0 - (K - 1 - k) * d;
        if (s < 0) s += len;
        if (s >= len) s -= len;
        int n1 = len - s;
        if (n1 > n) n1 = n;
        int32_t *x = pxn_x + f0 * PXN_S + 2 + 3 * k;
        pxn_gcopy(x, ring + 3 * s, n1, PXN_S * 4);
        if (n1 < n) pxn_gcopy(x + n1 * PXN_S, ring, n - n1, PXN_S * 4);
        if (kick) pxn_kick();
    }
}
/* rows weight rows (1, or the 3 of a layer, contiguous) over X rows f0 .. f0+n-1 -> pxn_z, by the CPU */
static void pxn_rows_cpu(const int32_t *w, int T, int rows, int f0, int n)
{
    const int n4 = rows == PXN_C ? n & ~3 : 0;
    pxn_dot d = {w, pxn_x + f0 * PXN_S, T, PXN_S * 4, pxn_z + 3 * f0, n4 >> 2};
    if (n4) pxn_dot3x4(&d);
    if (n4 < n)
        for (int o = 0; o < rows; o++) {
            d.w = w + o * T; d.x = pxn_x + (f0 + n4) * PXN_S; d.z = pxn_z + 3 * (f0 + n4) + o; d.ng = n - n4;
            pxn_dot1(&d);
        }
}
/* the same, on the vector unit one command after the other if mode 1 or more (w16, q: the 16-bit rows and shifts) */
static void pxn_rows(const int32_t *w, const int16_t *w16, const uint8_t *q, int T, int rows, int n)
{
#if PXN_ASM
    if (pxn_mode_v && pxn_vec_ok) {
        for (int o = 0; o < rows; o++)
            if (pxn_vdot2(w + o * T, w16 + o * PXN_T2(T), q[o], pxn_x, T, PXN_S * 4, pxn_z + o, n)) { pxn_vec_ok = 0; break; }
        if (pxn_vec_ok) return;
    }
#else
    (void)w16; (void)q;
#endif
    pxn_rows_cpu(w, T, rows, 0, n);
}
/* layer i's post for frames f0 .. f0+n-1: LeakyReLU, head sum, residual into the next layer's ring (its block starts
 * at its write position, still the one before this block); in pieces of 4 frames, the vector unit kicked between */
static void pxn_post_part(pxn_model *m, int i, int f0, int n, int kick)
{
    pxn_layer *L = &m->L[i];
    const int K = L->k;
    pxn_post p = {pxn_z + 3 * f0, pxn_x + f0 * PXN_S + 2 + 3 * (K - 1), PXN_S * 4, L->rr, pxn_hs + 3 * f0, 0, 0, L->rr2};
    pxn_layer *N = i < PXN_NL - 1 ? &m->L[i + 1] : 0;
    int32_t w = N ? N->w + f0 : 0;
    if (N && w >= N->len) w -= N->len;
    while (n > 0) {
        int c = n < 4 ? n : 4;
        if (N) {
            if (c > N->len - w) c = N->len - w;
            p.out = N->ring + 3 * w;
            w += c;
            if (w == N->len) w = 0;
        }
        p.n = c;
        pxn_post_f(&p);
        if (kick) pxn_kick();
        p.z += 3 * c; p.x += c * PXN_S; p.hs += 3 * c;
        n -= c;
    }
}
static void pxn_layer_block(pxn_model *m, int i, int n)
{
    pxn_layer *L = &m->L[i];
    const int K = L->k, T = 3 * K + 2;
    pxn_gather(L->ring, L->len, L->w, K, L->d, 0, n, 1);
    pxn_rows(m->wa + L->off, m->wa16 + L->off16, L->q, T, PXN_C, n);
    pxn_post_part(m, i, 0, n, 1);
}
/* layer i for frames f0 .. f0+n-1, by the CPU alone */
static void pxn_range_cpu(pxn_model *m, int i, int f0, int n, int kick)
{
    pxn_layer *L = &m->L[i];
    pxn_gather(L->ring, L->len, L->w, L->k, L->d, f0, n, kick);
    pxn_rows_cpu(m->wa + L->off, 3 * L->k + 2, PXN_C, f0, n);
    pxn_post_part(m, i, f0, n, kick);
}

/* ---- two cores: the helper (the other core, through the caller's job functions) takes the first h frames ---- */
static int (*pxn_hrun)(void (*fn)(uint32_t), uint32_t arg);   /* 1 = the other core has the job */
static int (*pxn_hwait)(void);                                  /* 0 = done, -1 = dropped */
void pxn_set_helper(int (*run)(void (*fn)(uint32_t), uint32_t arg), int (*wait)(void)) { pxn_hrun = run; pxn_hwait = wait; }
static struct { pxn_model *m; int32_t h; volatile int32_t done; } pxn_hj;   /* done: layers the helper has posted */
static uint8_t pxn_hon, pxn_hdead;
#define PXN_SYNC() __asm__ volatile("" ::: "memory")
#if PXN_ASM
#undef PXN_SYNC
#define PXN_SYNC() __asm__ volatile("csync" ::: "memory")
#endif
static void pxn_help_job(uint32_t arg)                  /* on the helper core */
{
    (void)arg;
    for (int i = 0; i < PXN_NL; i++) {
        pxn_range_cpu(pxn_hj.m, i, 0, pxn_hj.h, 0);
        PXN_SYNC();
        pxn_hj.done = i + 1;
        PXN_SYNC();
    }
}
#ifdef PXN_EMUVEC
/* PC tests: a helper that moves on by one layer only while the calling core waits for it (as late as it can be: a
 * frame read before it is ready shows) */
static uint8_t pxn_emu_hjob;
static int pxn_emu_hrun(void (*fn)(uint32_t), uint32_t arg) { (void)fn; (void)arg; pxn_emu_hjob = 1; return 1; }
static void pxn_emu_hstep(void)
{
    if (pxn_emu_hjob && pxn_hj.done < PXN_NL) { pxn_range_cpu(pxn_hj.m, pxn_hj.done, 0, pxn_hj.h, 0); pxn_hj.done++; }
}
static int pxn_emu_hwait(void) { while (pxn_hj.done < PXN_NL) pxn_emu_hstep(); pxn_emu_hjob = 0; return 0; }
void pxn_emu_helper(void) { pxn_hrun = pxn_emu_hrun; pxn_hwait = pxn_emu_hwait; }
#endif
/* the calling core: wait until the helper has posted layer j-1 (layer j's ring holds its frames); if the helper is
 * gone, its frames are done here */
static void pxn_hneed(int j)
{
    if (!pxn_hon) return;
    for (uint32_t k = 0; pxn_hj.done < j && !pxn_hdead; ) {
        PXN_SYNC();
#ifdef PXN_EMUVEC
        pxn_emu_hstep();
#endif
        if (++k > 400000u) {                             /* very late: its job API's own wait (and time limit) */
            if (pxn_hwait() < 0) pxn_hdead = 1;          /* dropped: never runs again */
            k = 0;
        }
    }
    if (pxn_hdead)
        while (pxn_hj.done < j) { pxn_range_cpu(pxn_hj.m, pxn_hj.done, 0, pxn_hj.h, 1); pxn_hj.done++; }
}
static void pxn_hstart(pxn_model *m, int h)
{
    pxn_hj.m = m;
    pxn_hj.h = h;
    pxn_hj.done = 0;
    PXN_SYNC();
    pxn_hon = 1;
    pxn_hdead = 0;
    if (!pxn_hrun || !pxn_hrun(pxn_help_job, 0)) pxn_hdead = 1;   /* no helper: its frames here, layer by layer */
}
static void pxn_hend(void)
{
    pxn_hneed(PXN_NL);
    if (!pxn_hdead && pxn_hwait) pxn_hwait();
    pxn_hon = 0;
}

#if PXN_VQON
/* the vector unit beside the CPU, over frames fa .. fb-1 in two halves. Half B of layer i, and layer i+1's half A,
 * never need each other, so while the vector unit does one half's 3 dot products, the CPU gathers or posts the other:
 *   unit:  D(i,A)        D(i,B)              D(i+1,A)            ...
 *   CPU:   G(i,B)  P(i,A) G(i+1,A)   P(i,B) G(i+1,B)   ...
 * (G gather, D dot, P post). Each half has its own X rows and z, so nothing is overwritten while the unit reads it. */
static uint32_t pxn_issue(pxn_model *m, int i, int f0, int n, int c)   /* the 3 dot products of layer i, frames f0.. */
{
    const pxn_layer *L = &m->L[i];
    const int T = 3 * L->k + 2;
    if (!pxn_vec_ok) { pxn_rows_cpu(m->wa + L->off, T, PXN_C, f0, n); return 0; }
    const int nv = n - c;                                /* the CPU takes the last c frames itself, meanwhile */
    for (int o = 0; o < PXN_C && nv > 0; o++)
        pxn_venq(m->wa + L->off + o * T, m->wa16 + L->off16 + o * PXN_T2(T), L->q[o], pxn_x + f0 * PXN_S, T,
                 pxn_z + 3 * f0 + o, nv);
    const uint32_t seq = nv > 0 ? pxn_vq_in : 0;
    if (c > 0) pxn_rows_cpu(m->wa + L->off, T, PXN_C, f0 + (nv > 0 ? nv : 0), nv > 0 ? c : n);
    return seq;
}
static void pxn_need(pxn_model *m, uint32_t seq, int i, int f0, int n, int c)
{
    if (!seq) return;
    if (!pxn_vfail && !pxn_vupto(seq)) return;
    const pxn_layer *L = &m->L[i];                       /* the unit died: the CPU does its part (X rows intact) */
    pxn_rows_cpu(m->wa + L->off, 3 * L->k + 2, PXN_C, f0, n - c);
}
static void pxn_layers_pipe(pxn_model *m, int fa, int fb, int c)
{
    const int h = ((fb - fa) >> 1) & ~3, f0[2] = {fa, fa + h}, nf[2] = {h, fb - fa - h};
    uint32_t seq[2];
    pxn_layer *L = &m->L[0];
    for (int s = 0; s < 2; s++) {
        pxn_gather(L->ring, L->len, L->w, L->k, L->d, f0[s], nf[s], 1);
        seq[s] = pxn_issue(m, 0, f0[s], nf[s], c);
    }
    for (int i = 0; i < PXN_NL; i++)
        for (int s = 0; s < 2; s++) {
            pxn_need(m, seq[s], i, f0[s], nf[s], c);
            pxn_post_part(m, i, f0[s], nf[s], 1);
            if (i < PXN_NL - 1) {
                pxn_hneed(i + 1);
                L = &m->L[i + 1];
                pxn_gather(L->ring, L->len, L->w, L->k, L->d, f0[s], nf[s], 1);
                seq[s] = pxn_issue(m, i + 1, f0[s], nf[s], c);
            }
        }
}
#endif
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
    const int h = PXN_MC[pxn_mode_v].h, pipe = PXN_MC[pxn_mode_v].pipe && pxn_vec_ok, c = PXN_MC[pxn_mode_v].c;
    if (h && n == PXN_N) {                               /* two cores */
        pxn_hstart(m, h);
#if PXN_VQON
        if (pipe) pxn_layers_pipe(m, h, n, c);
        else
#endif
        for (int i = 0; i < PXN_NL; i++) { pxn_hneed(i); pxn_range_cpu(m, i, h, n - h, 1); }
        pxn_hend();
    }
#if PXN_VQON
    else if (pipe && n >= 16) pxn_layers_pipe(m, 0, n, c);
#endif
    else for (int i = 0; i < PXN_NL; i++) pxn_layer_block(m, i, n);
    (void)pipe; (void)c;
    for (int i = 0; i < PXN_NL; i++) {                   /* every ring moves on by the block */
        pxn_layer *L = &m->L[i];
        L->w += n;
        if (L->w >= L->len) L->w -= L->len;
    }
    /* head: the activation sums into its ring, 16 taps gathered, one row */
    int n1 = m->hlen - m->hw;
    if (n1 > n) n1 = n;
    memcpy(m->hring + 3 * m->hw, pxn_hs, (unsigned)n1 * PXN_C * 4u);
    if (n1 < n) memcpy(m->hring, pxn_hs + 3 * n1, (unsigned)(n - n1) * PXN_C * 4u);
    int32_t hw = m->hw;
    m->hw += n;
    if (m->hw >= m->hlen) m->hw -= m->hlen;
    pxn_gather(m->hring, m->hlen, hw, PXN_HK, 1, 0, n, 1);
    pxn_rows(m->head, m->head16, &m->hq, PXN_S, 1, n);
    const float g = m->out_gain;
    for (int f = 0; f < n; f++) out[f] = (float)pxn_z[3 * f] * g;
}
int pxn_dual(void) { return PXN_MC[pxn_mode_v].h != 0; }

/* times the modes on the loaded model (8 blocks of 32 frames of a test signal each), keeps the fastest: the modes
 * with the vector unit only if it is there, the two-core ones only with a helper */
int pxn_tune(pxn_model *m, uint32_t (*ticks)(void), uint32_t t[PXN_MODES])
{
    static float in[PXN_N], out[PXN_N];
    int best = 0;
    for (int k = 0; k < PXN_MODES; k++) t[k] = 0;
    if (!m->ready) return pxn_mode_v;
    for (int mode = 0; mode < PXN_MODES; mode++) {
        const int unit = mode == 1 || PXN_MC[mode].pipe;
        if ((unit && !pxn_vec_ok) || (PXN_MC[mode].h && !pxn_hrun)) continue;
        pxn_mode_v = mode;
        pxn_reset(m);
        uint32_t r = 4321u, dt = 0;
        for (int b = 0; b < 10; b++) {
            for (int f = 0; f < PXN_N; f++) { r = r * 1103515245u + 12345u; in[f] = (float)((int32_t)r >> 8) * (0.5f / 8388608.0f); }
            uint32_t t0 = ticks();
            pxn_block(m, in, out, PXN_N);
            if (b >= 2) dt += ticks() - t0;
        }
        if ((unit && !pxn_vec_ok) || (PXN_MC[mode].h && pxn_hdead)) continue;   /* failed on the way */
        t[mode] = dt;
        if (dt < t[best]) best = mode;
    }
    pxn_mode_v = best;
    pxn_reset(m);
    return best;
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
