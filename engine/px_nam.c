/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — NAM A2-Lite in fixed point (see px_nam.h). Written from the published A2 architecture; checked
 * against tools/nam_a2.py (tests/engine/nam_test.c).
 *
 * A layer, frame by frame: z[o] = sum over its taps t of w[t][o] x_t, where the taps are the dilated conv's
 * (input channel c, kernel tap k), then the mixin (x = the input u) and the bias (x = 1.0): one loop, three
 * accumulators, the weights streamed in tap order. Then a = LeakyReLU(z), head[o] += a[o], and the next layer's input
 * x[o] + sum_c one[o][c] a[c] + bias[o]. On the pedal this is one routine by hand (pxn_layer_asm, 5 instructions per
 * 3 multiply-adds in the CPU's hardware loop); everywhere else, and on the pedal if the start-up check fails, the same
 * maths in C (pxn_layer_c). */
#include "px_nam.h"
#include <string.h>

/* Everything here runs from the flash but the hand-written layer routine, which is in RAM (.ram_text): the flash and
 * the RAM code are out of each other's direct call range, so it is called through a pointer (pxn_layer_fn) and calls
 * nothing itself. */
#define PXN_HOT

static const uint8_t pxn_k[PXN_NL] = {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 15, 15, 6, 6, 6, 6, 6, 6, 6};
static const uint16_t pxn_d[PXN_NL] = {1, 3, 7, 17, 41, 101, 239, 1, 3, 7, 17, 41, 101, 239, 1, 13,
                                       1, 3, 7, 17, 41, 101, 239};
static int32_t pxn_one[PXN_N] =                          /* in RAM: the routine reads it as a tap */ {1 << PXN_QA, 1 << PXN_QA, 1 << PXN_QA, 1 << PXN_QA, 1 << PXN_QA,
                                       1 << PXN_QA, 1 << PXN_QA, 1 << PXN_QA, 1 << PXN_QA, 1 << PXN_QA,
                                       1 << PXN_QA, 1 << PXN_QA, 1 << PXN_QA, 1 << PXN_QA, 1 << PXN_QA,
                                       1 << PXN_QA};

/* one layer's work over n frames (the hand routine reads these offsets: keep the order) */
typedef struct {
    const int32_t *const *pt;           /*  0: ntap pointers, each to the tap's input for the chunk's first frame */
    const int32_t *w;                   /*  4: ntap x 3 weights, Q24, [tap][out] */
    const int32_t *x;                   /*  8: this layer's input now (plane 0) */
    int32_t xstride;                    /* 12: bytes between its planes */
    const int32_t *rr;                  /* 16: residual: 3 x {one[o][0], one[o][1], one[o][2], bias[o]} Q24 */
    int32_t *hs;                        /* 20: head sum, planes of PXN_N */
    int32_t *xn;                        /* 24: next layer's input, planes of PXN_N (0: the last layer) */
    int32_t ntap;                       /* 28 */
    int32_t n;                          /* 32: frames, 1..PXN_N */
} pxn_job;

/* LeakyReLU 0.01 on a Q20 value: x < 0 -> x * 42949673 / 2^32 */
static inline __attribute__((always_inline)) int32_t pxn_lrelu(int32_t z)
{
    return z >= 0 ? z : (int32_t)(((int64_t)z * 42949673) >> 32);
}
#define PXN_Z(acc) ((int32_t)((acc) >> PXN_QW))      /* Q44 -> Q20 (no saturation: see px_nam.h) */

static PXN_HOT void pxn_layer_c(const pxn_job *j)
{
    for (int f = 0; f < j->n; f++) {
        int64_t z0 = 0, z1 = 0, z2 = 0;
        const int32_t *w = j->w;
        for (int t = 0; t < j->ntap; t++, w += 3) {
            const int64_t x = j->pt[t][f];
            z0 += w[0] * x; z1 += w[1] * x; z2 += w[2] * x;
        }
        const int32_t a0 = pxn_lrelu(PXN_Z(z0)), a1 = pxn_lrelu(PXN_Z(z1)), a2 = pxn_lrelu(PXN_Z(z2));
        j->hs[f] += a0; j->hs[PXN_N + f] += a1; j->hs[2 * PXN_N + f] += a2;
        if (!j->xn) continue;
        const int32_t *r = j->rr, *x = j->x;
        for (int o = 0; o < PXN_C; o++, r += 4) {
            int64_t v = (int64_t)r[0] * a0 + (int64_t)r[1] * a1 + (int64_t)r[2] * a2 + (int64_t)r[3] * (1 << PXN_QA);
            j->xn[o * PXN_N + f] = x[f] + PXN_Z(v);
            x = (const int32_t *)((const uint8_t *)x + j->xstride);
        }
    }
}

#if defined(__PI32V2__) && !defined(PXN_NO_ASM)
#define PXN_ASM 1
void pxn_layer_asm(const pxn_job *j);
__asm__(
    "\t.section .ram_text,\"ax\"\n"
    "\t.global pxn_layer_asm\n"
    "pxn_layer_asm:\n"
    "\t[--sp] = {rets, r15-r4}\n"
    "\tr13 = 0\n"                                   /* f */
    ".Lpxn_frame:\n"
    "\tr4 = 0\n\tr5 = 0\n\tr6 = 0\n\tr7 = 0\n\tr8 = 0\n\tr9 = 0\n"
    "\tr1 = [r0+0]\n"                               /* pt */
    "\tr2 = [r0+4]\n"                               /* w */
    "\tr11 = [r0+28]\n"                             /* ntap */
    "\tr12 = 4\n"
    ".Lpxn_rep:\n"
    "\trep r11 {\n"
    "\tr3 = [r1++=4]\n"                             /* the tap's input */
    "\tr10 = [r3+r13<<2]\n"                         /* ... at frame f */
    "\tr5_r4 += [r2 ++= r12]*r10 (s)\n"
    "\tr7_r6 += [r2 ++= r12]*r10 (s)\n"
    "\tr9_r8 += [r2 ++= r12]*r10 (s)\n"
    "\t}\n"
    "\tif (r11 != 0) goto .Lpxn_rep\n"
    "\tr5_r4 >>= 24\n\tr7_r6 >>= 24\n\tr9_r8 >>= 24\n"
    "\tr14 = 42949673\n"                            /* LeakyReLU */
    "\tifs (r4 > -1) goto .Lpxn_p0\n\tr5_r4 = r4 * r14 (s)\n\tr4 = r5\n"
    ".Lpxn_p0:\n"
    "\tifs (r6 > -1) goto .Lpxn_p1\n\tr7_r6 = r6 * r14 (s)\n\tr6 = r7\n"
    ".Lpxn_p1:\n"
    "\tifs (r8 > -1) goto .Lpxn_p2\n\tr9_r8 = r8 * r14 (s)\n\tr8 = r9\n"
    ".Lpxn_p2:\n"
    "\tr1 = [r0+20]\n"                              /* head sum */
    "\tr3 = [r1+r13<<2]\n\tr3 += r4\n\t[r1+r13<<2] = r3\n\tr1 += 64\n"
    "\tr3 = [r1+r13<<2]\n\tr3 += r6\n\t[r1+r13<<2] = r3\n\tr1 += 64\n"
    "\tr3 = [r1+r13<<2]\n\tr3 += r8\n\t[r1+r13<<2] = r3\n"
    "\tr1 = [r0+24]\n"                              /* next layer's input */
    "\tif (r1 == 0) goto .Lpxn_next\n"
    "\tr3 = r4\n\tr7 = r6\n\tr9 = r8\n"
    "\tr2 = [r0+16]\n\tr11 = [r0+8]\n\tr15 = [r0+12]\n\tr10 = 1048576\n"
    "\tr4 = 0\n\tr5 = 0\n"
    "\tr5_r4 += [r2 ++= r12]*r3 (s)\n\tr5_r4 += [r2 ++= r12]*r7 (s)\n"
    "\tr5_r4 += [r2 ++= r12]*r9 (s)\n\tr5_r4 += [r2 ++= r12]*r10 (s)\n"
    "\tr5_r4 >>= 24\n\tr6 = [r11+r13<<2]\n\tr6 += r4\n\t[r1+r13<<2] = r6\n\tr11 += r15\n\tr1 += 64\n"
    "\tr4 = 0\n\tr5 = 0\n"
    "\tr5_r4 += [r2 ++= r12]*r3 (s)\n\tr5_r4 += [r2 ++= r12]*r7 (s)\n"
    "\tr5_r4 += [r2 ++= r12]*r9 (s)\n\tr5_r4 += [r2 ++= r12]*r10 (s)\n"
    "\tr5_r4 >>= 24\n\tr6 = [r11+r13<<2]\n\tr6 += r4\n\t[r1+r13<<2] = r6\n\tr11 += r15\n\tr1 += 64\n"
    "\tr4 = 0\n\tr5 = 0\n"
    "\tr5_r4 += [r2 ++= r12]*r3 (s)\n\tr5_r4 += [r2 ++= r12]*r7 (s)\n"
    "\tr5_r4 += [r2 ++= r12]*r9 (s)\n\tr5_r4 += [r2 ++= r12]*r10 (s)\n"
    "\tr5_r4 >>= 24\n\tr6 = [r11+r13<<2]\n\tr6 += r4\n\t[r1+r13<<2] = r6\n"
    ".Lpxn_next:\n"
    "\tr13 += 1\n"
    "\tr14 = [r0+32]\n"
    "\tif (r13 != r14) goto .Lpxn_frame\n"
    "\t{pc, r15-r4} = [sp++]\n"
    "\t.previous\n");
#else
#define PXN_ASM 0
#endif
/* the layer routine in use: the C one until the start-up check (pxn_selftest) picks the hand-written one. Called
 * through this pointer (volatile: the compiler cannot turn it back into a direct call, out of range on the pedal). */
static void (*volatile pxn_layer_fn)(const pxn_job *) = pxn_layer_c;

static inline __attribute__((always_inline)) void pxn_layer_go(const pxn_job *j)
{
    pxn_layer_fn(j);
}

/* runs both on a fixed random pattern (both signs, the largest values a model meets, 47 taps, 16 frames, with and
 * without the next layer): the hand routine is used only if every result is the same. scratch: PXN_TEST_WORDS. */
int pxn_selftest(int32_t *scratch)
{
#if PXN_ASM
    int32_t *data = scratch, *wt = data + 47 * 24, *rr = wt + 47 * 3, *x = rr + 12, *hs0 = x + 3 * 40,
            *hs1 = hs0 + 3 * PXN_N, *xn0 = hs1 + 3 * PXN_N, *xn1 = xn0 + 3 * PXN_N;
    const int32_t *pt[47];
    uint32_t r = 12345u;
    for (int i = 0; i < 47 * 24 + 47 * 3 + 12 + 3 * 40; i++) {
        r = r * 1103515245u + 12345u;
        data[i] = (int32_t)r >> (i < 47 * 24 + 47 * 3 + 12 ? 6 : 8);          /* +-32 (Q20 data, Q24 weights) */
    }
    for (int i = 0; i < 47; i++) pt[i] = data + i * 24 + (i & 7);
    void (*volatile hand)(const pxn_job *) = pxn_layer_asm;   /* through a pointer: in RAM, out of call range */
    int ok = 1;
    pxn_layer_fn = pxn_layer_c;
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < 3 * PXN_N; i++) { hs0[i] = hs1[i] = i * 77; xn0[i] = xn1[i] = -1; }
        pxn_job j = {pt, wt, x + 3, 40 * 4, rr, hs0, pass ? 0 : xn0, pass ? 20 : 47, pass ? 13 : PXN_N};
        hand(&j);
        j.hs = hs1; j.xn = pass ? 0 : xn1;
        pxn_layer_c(&j);
        if (memcmp(hs0, hs1, 3 * PXN_N * 4u) || memcmp(xn0, xn1, 3 * PXN_N * 4u)) ok = 0;
    }
    if (ok) pxn_layer_fn = pxn_layer_asm;
    return ok;
#else
    (void)scratch;
    return 0;
#endif
}

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
    for (int i = 0; i < PXN_NL; i++) need[i] = (unsigned)(m->L[i].len + PXN_N) * PXN_C * 4u;
    need[PXN_NL] = (unsigned)(m->hlen + PXN_N) * PXN_C * 4u;
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
        const int K = pxn_k[i], T = 3 * K;
        const float *conv = p, *cb = p + 9 * K, *mix = cb + 3, *one = mix + 3, *ob = one + 9;
        int32_t *d = m->wa + off;
        /* [tap][out]: the conv's taps (input c, kernel k), the mixin, the bias. File order: conv [out][in][k] */
        for (int o = 0; o < PXN_C; o++)
            for (int c = 0; c < PXN_C; c++)
                for (int k = 0; k < K; k++) d[(c * K + k) * 3 + o] = pxn_toq(conv[(o * PXN_C + c) * K + k], PXN_QW);
        for (int o = 0; o < PXN_C; o++) d[T * 3 + o] = pxn_toq(mix[o], PXN_QW);
        for (int o = 0; o < PXN_C; o++) d[(T + 1) * 3 + o] = pxn_toq(cb[o], PXN_QW);
        for (int o = 0; o < PXN_C; o++) {
            for (int c = 0; c < PXN_C; c++) L->rr[o * 4 + c] = pxn_toq(one[o * PXN_C + c], PXN_QW);
            L->rr[o * 4 + 3] = pxn_toq(ob[o], PXN_QW);
        }
        L->k = (int16_t)K;
        L->d = (int16_t)pxn_d[i];
        L->off = off;
        L->len = (K - 1) * pxn_d[i] + PXN_N;
        off += (T + 2) * 3;
        p = ob + 3;
    }
    for (int j = 0; j < PXN_C * PXN_HK; j++) m->head[j] = pxn_toq(p[j], PXN_QW);   /* [in][tap] */
    p += PXN_C * PXN_HK;
    m->head_b = pxn_toq(*p++, PXN_QA);
    m->out_gain = *p++ * (1.0f / 1048576.0f);
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
        memset(L->ring, 0, (unsigned)(L->len + PXN_N) * PXN_C * 4u);
        L->w = 0;
    }
    memset(m->hring, 0, (unsigned)(m->hlen + PXN_N) * PXN_C * 4u);
    m->hw = 0;
}

/* writes n frames of 3 planes at w (wrapping), keeps the copy of [0, PXN_N) after the end */
static PXN_HOT void pxn_put(int32_t *ring, int32_t len, int32_t w, const int32_t *src, int n)
{
    const int first = n < len - w ? n : len - w;
    for (int c = 0; c < PXN_C; c++) {
        int32_t *pl = ring + c * (len + PXN_N);
        const int32_t *s = src + c * PXN_N;
        memcpy(pl + w, s, (unsigned)first * 4u);
        if (first < n) memcpy(pl, s + first, (unsigned)(n - first) * 4u);
        if (w < PXN_N || first < n) memcpy(pl + len, pl, PXN_N * 4u);
    }
}

/* one layer over n frames (n <= PXN_N): its input is in its ring at L->w; adds its activations to hs and, when xn is
 * given, writes the next layer's input there. Planes: [c][PXN_N]. */
static PXN_HOT void pxn_layer_run(const pxn_model *m, pxn_layer *L, const int32_t *u, int32_t *hs, int32_t *xn,
                                  int n)
{
    const int K = L->k, len = L->len, stride = len + PXN_N, T = 3 * K;
    const int32_t *pt[3 * 15 + 2];
    int32_t base = L->w - (K - 1) * L->d;
    if (base < 0) base += len;
    for (int k = 0; k < K; k++) {
        int32_t j = base + k * L->d;
        if (j >= len) j -= len;
        for (int c = 0; c < PXN_C; c++) pt[c * K + k] = L->ring + c * stride + j;
    }
    pt[T] = u;
    pt[T + 1] = pxn_one;
    const pxn_job jb = {pt, m->wa + L->off, L->ring + L->w, stride * 4, L->rr, hs, xn, T + 2, n};
    pxn_layer_go(&jb);
    L->w += n;
    if (L->w >= len) L->w -= len;
}

static PXN_HOT void pxn_chunk(pxn_model *m, const float *in, float *out, int n)
{
    int32_t u[PXN_N], x[PXN_C * PXN_N], xn[PXN_C * PXN_N], hs[PXN_C * PXN_N];
    for (int f = 0; f < n; f++) {                        /* Q20, clamped to +-64 (far above any guitar) */
        float v = in[f] * 1048576.0f;
        u[f] = v >= 67108864.0f ? 67108864 : v <= -67108864.0f ? -67108864 : (int32_t)v;
    }
    for (int c = 0; c < PXN_C; c++)
        for (int f = 0; f < n; f++) x[c * PXN_N + f] = PXN_Z((int64_t)m->rech[c] * u[f]);
    memset(hs, 0, sizeof hs);
    pxn_put(m->L[0].ring, m->L[0].len, m->L[0].w, x, n);
    for (int i = 0; i < PXN_NL; i++) {
        const int last = i == PXN_NL - 1;
        pxn_layer_run(m, &m->L[i], u, hs, last ? 0 : xn, n);
        if (!last) pxn_put(m->L[i + 1].ring, m->L[i + 1].len, m->L[i + 1].w, xn, n);
    }
    /* head: 16 taps over the activation sum */
    pxn_put(m->hring, m->hlen, m->hw, hs, n);
    const int hl = m->hlen, hst = hl + PXN_N;
    int32_t base = m->hw - (PXN_HK - 1);
    if (base < 0) base += hl;
    const int64_t hb = (int64_t)m->head_b << PXN_QW;
    const float g = m->out_gain;
    for (int f = 0; f < n; f++) {
        int64_t y = hb;
        int32_t j = base + f;
        if (j >= hl) j -= hl;
        for (int c = 0; c < PXN_C; c++) {
            const int32_t *s = m->hring + c * hst + j, *w = m->head + c * PXN_HK;   /* the copy covers the wrap */
            for (int k = 0; k < PXN_HK; k++) y += (int64_t)w[k] * s[k];
        }
        int64_t v = y >> PXN_QW;                         /* the output: saturated */
        if (v > 2147483647) v = 2147483647; else if (v < -2147483647) v = -2147483647;
        out[f] = (float)(int32_t)v * g;
    }
    m->hw += n;
    if (m->hw >= hl) m->hw -= hl;
}

PXN_HOT void pxn_process(pxn_model *m, const float *in, float *out, int n)
{
    if (!m->ready) {
        if (out != in) memcpy(out, in, (unsigned)n * sizeof(float));
        return;
    }
    while (n > 0) {
        const int c = n < PXN_N ? n : PXN_N;
        pxn_chunk(m, in, out, c);
        in += c;
        out += c;
        n -= c;
    }
}
