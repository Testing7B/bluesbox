/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket X — mini single-precision libm for the DSP engine (the toolchain has no libm).
 * Polynomial approximations: relative error ~1e-6, plenty for audio.
 * Built with -mcpu=r3 -mfprev1: float ops run on the AC791N's hardware FPU. */
#include <stdint.h>

#define PI_F    3.14159265358979f
#define TWOPI_F 6.28318530717959f
#define HALFPI_F 1.57079632679490f
#define LN2_F   0.69314718055995f
#define LOG2E_F 1.44269504088896f

typedef union { float f; uint32_t i; } fbits;

/* sin/cos: reduction to [-pi/4, pi/4] (pi/2 split in two parts to keep precision) + polynomials.
 * Crucial for low-frequency biquads at high sample rates (30 Hz at 176 kHz:
 * w = 1e-3, cos(w) = 1 - w^2/2 must be accurate to 1e-8, otherwise the filter goes unstable -> NaN). */
#define PIO2_HI 1.5707963705062866f                    /* float(pi/2) */
#define PIO2_LO (-4.3711388286737929e-8f)              /* pi/2 - PIO2_HI */
static float k_sin(float r) { float r2 = r * r; return r + r * r2 * (-1.0f / 6.0f + r2 * (1.0f / 120.0f + r2 * (-1.0f / 5040.0f + r2 * (1.0f / 362880.0f)))); }
static float k_cos(float r) { float r2 = r * r; return 1.0f + r2 * (-0.5f + r2 * (1.0f / 24.0f + r2 * (-1.0f / 720.0f + r2 * (1.0f / 40320.0f)))); }
static float reduce(float x, int *q)
{
    float t = x * (2.0f / PI_F);
    int k = (int)(t + (t >= 0.0f ? 0.5f : -0.5f));
    *q = k & 3;
    return (x - (float)k * PIO2_HI) - (float)k * PIO2_LO;
}
float sinf(float x)
{
    int q; float r = reduce(x, &q);
    switch (q) { case 0: return k_sin(r); case 1: return k_cos(r); case 2: return -k_sin(r); default: return -k_cos(r); }
}
float cosf(float x)
{
    int q; float r = reduce(x, &q);
    switch (q) { case 0: return k_cos(r); case 1: return -k_sin(r); case 2: return -k_cos(r); default: return k_sin(r); }
}

float expf(float x)
{
    if (x > 88.0f) x = 88.0f;
    if (x < -87.0f) return 0.0f;
    float t = x * LOG2E_F;
    int k = (int)(t + (t >= 0.0f ? 0.5f : -0.5f));
    float r = x - (float)k * LN2_F;                    /* |r| <= ln2/2 */
    float p = 1.0f + r * (1.0f + r * (0.5f + r * (1.0f / 6.0f + r * (1.0f / 24.0f + r * (1.0f / 120.0f + r * (1.0f / 720.0f))))));
    fbits s; s.i = (uint32_t)(k + 127) << 23;          /* 2^k */
    return p * s.f;
}

float logf(float x)
{
    if (x <= 0.0f) return -1.0e30f;
    fbits b; b.f = x;
    int e = (int)((b.i >> 23) & 0xFFu) - 127;
    b.i = (b.i & 0x7FFFFFu) | 0x3F800000u;             /* mantissa in [1, 2) */
    float m = b.f;
    if (m > 1.41421356f) { m *= 0.5f; e++; }
    float t = (m - 1.0f) / (m + 1.0f), t2 = t * t;
    float l = 2.0f * t * (1.0f + t2 * (1.0f / 3.0f + t2 * (1.0f / 5.0f + t2 * (1.0f / 7.0f + t2 * (1.0f / 9.0f)))));
    return (float)e * LN2_F + l;
}

float powf(float x, float y)
{
    if (x <= 0.0f) return 0.0f;                        /* the engine only uses bases > 0 (10^x) */
    return expf(y * logf(x));
}

float sqrtf(float x)
{
    if (x <= 0.0f) return 0.0f;
    fbits b; b.f = x;
    b.i = 0x1FBD1DF5u + (b.i >> 1);                    /* initial estimate */
    float y = b.f;
    y = 0.5f * (y + x / y);
    y = 0.5f * (y + x / y);
    y = 0.5f * (y + x / y);
    return y;
}

float fabsf(float x) { return x < 0.0f ? -x : x; }
