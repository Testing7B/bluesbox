/* OpenPocket — guitar sound engine v3 (see px_dsp.h)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Engine rules (learned on the pedal):
 *  - single precision everywhere (AC791N float FPU), never double;
 *  - coefficients are computed in px_engine_set (outside real time); processing is only + and x;
 *  - no modulo in hot loops; anti-denormal offset at the input;
 *  - ALWAYS verify with firmware/px_math.c (the pedal's libm), never the PC libm.
 *
 * Voicing sources (circuit analyses, datasheets, manuals): ElectroSmash (TS808, RAT, DS-1, Big Muff, Fuzz Face,
 * Klon, Distortion+, Bluesbreaker), Electric Druid, R.G. Keen / GEOFEX, Rob Robinette & Duncan's TSC (amp tone
 * stacks), Celestion / Jensen / EV / JBL speaker curves, Roland/Boss/Ampeg/Electro-Harmonix manuals.
 */
#include "px_dsp.h"
#include <math.h>
#include <string.h>

#define PI_F 3.14159265f

/* ================================================================ tools ===== */
static inline float px_tanh(float x)                   /* rational, err < 1e-3 on [-3,3] */
{
    if (x < -3.0f) return -1.0f;
    if (x >  3.0f) return  1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
static inline float px_abs(float x) { return x < 0.0f ? -x : x; }
static inline float lfo_sin(float ph)                  /* parabolic sine, ph in [0,1) -> -1..1 */
{
    if (ph < 0.5f) return 16.0f * ph * (0.5f - ph);
    ph -= 0.5f;
    return -16.0f * ph * (0.5f - ph);
}
static inline float sin_hq(float ph)                   /* refined parabola (0.1 % error): audio-rate carrier */
{
    float y = lfo_sin(ph);
    return 0.225f * (y * px_abs(y) - y) + y;
}
static inline float lfo_tri(float ph) { return 4.0f * px_abs(ph - 0.5f) - 1.0f; }
static inline float f01(uint8_t v) { return (float)(v > 100 ? 100 : v) * 0.01f; }
static inline float db2lin(float db) { return powf(10.0f, db / 20.0f); }
static inline float wrap1(float ph) { if (ph >= 1.0f) ph -= 1.0f; else if (ph < 0.0f) ph += 1.0f; return ph; }
static float onepole_k(float sr, float sec) { return 1.0f - expf(-1.0f / (sec * sr)); }

static void bq_reset(px_bq *b) { b->z1 = b->z2 = 0.0f; }
static inline float bq_run(px_bq *b, float x)
{
    float y = b->b0 * x + b->z1;
    b->z1 = b->b1 * x - b->a1 * y + b->z2;
    b->z2 = b->b2 * x - b->a2 * y;
    return y;
}
static void bq_set(px_bq *b, float b0, float b1, float b2, float a0, float a1, float a2)
{
    float z1 = b->z1, z2 = b->z2;                      /* keep state: no click on change */
    b->b0 = b0 / a0; b->b1 = b1 / a0; b->b2 = b2 / a0; b->a1 = a1 / a0; b->a2 = a2 / a0;
    b->z1 = z1; b->z2 = z2;
}
static void bq_ident(px_bq *b) { bq_set(b, 1, 0, 0, 1, 0, 0); }
static float clampf_hz(float fs, float f) { if (f > fs * 0.45f) f = fs * 0.45f; if (f < 5.0f) f = 5.0f; return f; }
static void bq_lowpass(px_bq *b, float fs, float f, float q)
{
    f = clampf_hz(fs, f);
    float w = 2.0f * PI_F * f / fs, c = cosf(w), s = sinf(w), al = s / (2.0f * q);
    bq_set(b, (1 - c) * 0.5f, 1 - c, (1 - c) * 0.5f, 1 + al, -2 * c, 1 - al);
}
static void bq_highpass(px_bq *b, float fs, float f, float q)
{
    f = clampf_hz(fs, f);
    float w = 2.0f * PI_F * f / fs, c = cosf(w), s = sinf(w), al = s / (2.0f * q);
    bq_set(b, (1 + c) * 0.5f, -(1 + c), (1 + c) * 0.5f, 1 + al, -2 * c, 1 - al);
}
static void bq_bandpass(px_bq *b, float fs, float f, float q)
{
    f = clampf_hz(fs, f);
    float w = 2.0f * PI_F * f / fs, c = cosf(w), s = sinf(w), al = s / (2.0f * q);
    bq_set(b, al, 0, -al, 1 + al, -2 * c, 1 - al);
}
static void bq_peak(px_bq *b, float fs, float f, float q, float db)
{
    f = clampf_hz(fs, f);
    float A = powf(10.0f, db / 40.0f);
    float w = 2.0f * PI_F * f / fs, c = cosf(w), s = sinf(w), al = s / (2.0f * q);
    bq_set(b, 1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
}
static void bq_lowshelf(px_bq *b, float fs, float f, float db)
{
    f = clampf_hz(fs, f);
    float A = powf(10.0f, db / 40.0f);
    float w = 2.0f * PI_F * f / fs, c = cosf(w), s = sinf(w);
    float al = s / 2.0f * 1.4142135f, ap = A + 1, am = A - 1, tsa = 2.0f * sqrtf(A) * al;
    bq_set(b, A * (ap - am * c + tsa), 2 * A * (am - ap * c), A * (ap - am * c - tsa),
           ap + am * c + tsa, -2 * (am + ap * c), ap + am * c - tsa);
}
static void bq_highshelf(px_bq *b, float fs, float f, float db)
{
    f = clampf_hz(fs, f);
    float A = powf(10.0f, db / 40.0f);
    float w = 2.0f * PI_F * f / fs, c = cosf(w), s = sinf(w);
    float al = s / 2.0f * 1.4142135f, ap = A + 1, am = A - 1, tsa = 2.0f * sqrtf(A) * al;
    bq_set(b, A * (ap + am * c + tsa), -2 * A * (am + ap * c), A * (ap + am * c - tsa),
           ap - am * c + tsa, 2 * (am - ap * c), ap - am * c - tsa);
}
static float tan_pi(float fs, float f) { float w = PI_F * clampf_hz(fs, f) / fs; return sinf(w) / cosf(w); }
static inline float op_run(px_op *o, float x)
{
    float y = o->b0 * x + o->z;
    o->z = o->b1 * x - o->a1 * y;
    return y;
}
static void op_hp(px_op *o, float fs, float f)
{
    float K = tan_pi(fs, f), n = 1.0f / (1.0f + K);
    o->b0 = n; o->b1 = -n; o->a1 = (K - 1.0f) * n;
}
static void op_lp(px_op *o, float fs, float f)
{
    float K = tan_pi(fs, f), n = 1.0f / (1.0f + K);
    o->b0 = K * n; o->b1 = K * n; o->a1 = (K - 1.0f) * n;
}
static void bq_hp1(px_bq *b, float fs, float f)          /* first order (RC coupling, like the real circuits) */
{
    float K = tan_pi(fs, f);
    bq_set(b, 1.0f, -1.0f, 0.0f, 1.0f + K, K - 1.0f, 0.0f);
}

/* ============================================================ catalog ===== */
static const char *const BLK_LABEL[PX_NBLK] = {"GT", "CMP", "PIT", "SFX", "DRV", "AMP", "CAB", "EQ", "MOD", "DLY", "REV"};
static const char *const BLK_NAME[PX_NBLK] = {"NOISE GATE", "COMPRESSOR", "PITCH", "SPECIAL FX", "DRIVE / FUZZ",
                                              "AMP", "CABINET", "EQUALIZER", "MODULATION", "DELAY", "REVERB"};
typedef struct { const char *name; uint8_t np; const char *const *pn; } tdef;

static const char *const PN_GATE[] = {"THRESH", "RELEASE"};
static const tdef T_GATE[] = {{"NOISE GATE", 2, PN_GATE}};

static const char *const PN_CMP[] = {"SUSTAIN", "ATTACK", "LEVEL", "MIX"};
static const char *const PN_OPTO[] = {"COMPRESS", "LEVEL", "MIX"};
static const char *const PN_SWELL[] = {"RISE", "SENS", "LEVEL"};
static const tdef T_COMP[PX_CMP_COUNT] = {
    {"SQUASH", 3, PN_CMP}, {"STUDIO", 4, PN_CMP}, {"OPTO LA-2", 3, PN_OPTO}, {"SLOW SWELL", 3, PN_SWELL}};

static const char *const PN_OCT[] = {"DRY", "SUB -1", "SUB -2", "UP +1", "TONE"};
static const char *const PN_POLY[] = {"DRY", "OCT -1", "OCT +1"};
static const char *const PN_WHAM[] = {"SHIFT", "MIX", "GLIDE"};
static const char *const PN_HARM[] = {"INTERVAL", "MIX"};
static const char *const PN_DET[] = {"CENTS", "MIX"};
static const tdef T_PITCH[PX_PIT_COUNT] = {
    {"OCTAVER", 5, PN_OCT}, {"POLY OCTAVE", 3, PN_POLY}, {"WHAMMY", 3, PN_WHAM}, {"HARMONIZER", 2, PN_HARM},
    {"DETUNE", 2, PN_DET}};

static const char *const PN_AWAH[] = {"SENS", "RESO", "RANGE", "DECAY"};
static const char *const PN_LWAH[] = {"RATE", "DEPTH", "RESO", "CENTER"};
static const char *const PN_RING[] = {"FREQ", "MIX", "WOBBLE"};
static const char *const PN_CRUSH[] = {"BITS", "SR DIV", "MIX"};
static const char *const PN_SLICE[] = {"RATE", "DEPTH", "PATTERN"};
static const char *const PN_LOFI[] = {"TONE", "DRIVE", "MIX"};
static const char *const PN_SYN[] = {"SWEEP", "RESO", "SUB", "MIX"};
static const tdef T_SFX[PX_SFX_COUNT] = {
    {"ENVELOPE WAH", 4, PN_AWAH}, {"AUTO WAH", 4, PN_LWAH}, {"RING MOD", 3, PN_RING}, {"BITCRUSHER", 3, PN_CRUSH},
    {"SLICER", 3, PN_SLICE}, {"LO-FI RADIO", 3, PN_LOFI}, {"SQUARE SYNTH", 4, PN_SYN}};

/* drives: each knob has a role; the name shown comes from the role */
enum { R_DRIVE, R_DIST, R_FUZZ, R_SUST, R_BOOST, R_TONE, R_TREBLE, R_LEVEL, R_BASS, R_VOICE, R_GATE, R_BIAS,
       R_LOW, R_HIGH, R_TIGHT, R_BRIGHT, R_BLEND, R_NROLE };
static const char *const ROLE_NAME[R_NROLE] = {"DRIVE", "DIST", "FUZZ", "SUSTAIN", "BOOST", "TONE", "TREBLE", "LEVEL",
                                               "BASS", "VOICE", "GATE", "BIAS", "LOW", "HIGH", "TIGHT", "BRIGHT", "BLEND"};

/* ============================================================== drives ===== */
enum { CL_SOFT, CL_ASYM, CL_HARD, CL_GE, CL_LED, CL_MOS, CL_MUFF, CL_FUZZ, CL_OCTA, CL_BOOST };
enum { TM_LP, TM_MUFF, TM_HM2, TM_MT2 };
typedef struct {
    const char *name;
    uint8_t role[5], np;
    uint16_t hp, shelf, lp, mid; int8_t mid_db; uint8_t mid_q10;   /* pre-clip (base rate) */
    int8_t g0, g1;                                                   /* gain range, dB */
    uint8_t clip, tm; uint16_t t0, t1;                               /* clipper, post tone (oversampled) */
    uint16_t p2; int8_t p2db;                                        /* fixed post filter (0 dB = low-pass) */
    uint8_t blend, bias, gate;                                       /* /100 */
} drv_v;
#define L3(a, b, c) {a, b, c, 0, 0}, 3
#define L4(a, b, c, d) {a, b, c, d, 0}, 4
static const drv_v DV[PX_DRV_COUNT] = {
    /* name           roles                                  hp  shelf  lp  mid  dB q   g0 g1 clip     tm      t0    t1    p2  p2dB bl bi gt */
    {"TS 808",      L3(R_DRIVE, R_TONE, R_LEVEL),            30, 720,   0,   0,  0, 7, 21, 41, CL_SOFT, TM_LP,   900, 4500, 0,   0, 0, 0, 0},
    {"TS 9",        L3(R_DRIVE, R_TONE, R_LEVEL),            30, 720,   0,   0,  0, 7, 21, 41, CL_SOFT, TM_LP,  1300, 7000, 0,   0, 0, 0, 0},
    {"SD-1 OD",     L3(R_DRIVE, R_TONE, R_LEVEL),            30, 720,   0,   0,  0, 7, 21, 46, CL_ASYM, TM_LP,   900, 6000, 0,   0, 0, 30, 0},
    {"KLONE",       L3(R_DRIVE, R_TREBLE, R_LEVEL),          80, 408,   0, 1000, 3, 7,  6, 40, CL_GE,   TM_LP,  2500,14000, 0,   0, 60, 0, 0},
    {"BD BLUES",    L3(R_DRIVE, R_TONE, R_LEVEL),            40,   0,   0,  120, 6, 7, 10, 45, CL_HARD, TM_LP,  2000, 9000, 700, -3, 0, 0, 0},
    {"OCD DRIVE",   L3(R_DRIVE, R_TONE, R_LEVEL),            60,   0,   0,    0, 0, 7, 12, 50, CL_MOS,  TM_LP,  1000,10000, 0,   0, 0, 0, 0},
    {"RAT DIST",    L3(R_DIST, R_TONE, R_LEVEL),             60, 1500,  0,    0, 0, 7, 18, 62, CL_HARD, TM_LP,   600,12000, 6000, 0, 0, 0, 0},
    {"DS-1 DIST",   L3(R_DIST, R_TONE, R_LEVEL),             60,   0,   0,    0, 0, 7, 20, 42, CL_HARD, TM_MUFF,  234, 1063, 7200, 0, 0, 0, 0},
    {"DIST PLUS",   L3(R_DIST, R_TONE, R_LEVEL),            720,   0,   0,    0, 0, 7,  6, 47, CL_GE,   TM_LP,  3000,16000, 0,   0, 0, 0, 0},
    {"BLUESBREAKER",L3(R_DRIVE, R_TONE, R_LEVEL),           590,   0,   0,    0, 0, 7,  6, 30, CL_SOFT, TM_LP,  1200, 6000, 0,   0, 0, 0, 0},
    {"KING OF TONE",L3(R_DRIVE, R_TONE, R_LEVEL),            80, 720,   0,    0, 0, 7,  8, 36, CL_ASYM, TM_LP,  2000, 8000, 0,   0, 10, 15, 0},
    {"TIMMY",       L4(R_DRIVE, R_BASS, R_TREBLE, R_LEVEL), 200,   0,   0,    0, 0, 7,  3, 30, CL_LED,  TM_LP,  2000,10000, 0,   0, 0, 0, 0},
    {"ZEN DRIVE",   L4(R_DRIVE, R_TONE, R_VOICE, R_LEVEL),   30, 720,   0, 1800, 4, 8, 15, 45, CL_SOFT, TM_LP,  1500, 7000, 0,   0, 0, 0, 0},
    {"MUFF PI",     L3(R_SUST, R_TONE, R_LEVEL),             90,   0,   0,    0, 0, 7, 30, 55, CL_MUFF, TM_MUFF,  408, 1800, 0,   0, 0, 0, 0},
    {"RAMS HEAD",   L3(R_SUST, R_TONE, R_LEVEL),             90,   0,   0,  700, 3, 7, 25, 50, CL_MUFF, TM_MUFF,  500, 1600, 0,   0, 0, 0, 0},
    {"GREEN RUSSIAN",L3(R_SUST, R_TONE, R_LEVEL),            60,   0,   0,    0, 0, 7, 30, 58, CL_MUFF, TM_MUFF,  350, 1400, 0,   0, 0, 0, 0},
    {"FACE GERMAN.",L3(R_FUZZ, R_TONE, R_LEVEL),             20,   0,   0,    0, 0, 7, 15, 45, CL_FUZZ, TM_LP,  2500, 9000, 0,   0, 0, 20, 0},
    {"FACE SILICON",L3(R_FUZZ, R_TONE, R_LEVEL),             20,   0,   0,    0, 0, 7, 20, 52, CL_FUZZ, TM_LP,  3500,12000, 0,   0, 0, 35, 0},
    {"BENDER MK2",  L3(R_FUZZ, R_TONE, R_LEVEL),             40,   0,   0, 1000, 3, 7, 30, 60, CL_FUZZ, TM_LP,  2000, 8000, 0,   0, 0, 30, 0},
    {"OCTAFUZZ",    L3(R_FUZZ, R_TONE, R_LEVEL),             60,   0, 1500,   0, 0, 7, 20, 45, CL_OCTA, TM_LP,  2000, 9000, 0,   0, 0, 0, 0},
    {"FUZZ FACTORY",L4(R_FUZZ, R_GATE, R_BIAS, R_LEVEL),     30,   0,   0,    0, 0, 7, 25, 55, CL_FUZZ, TM_LP,  5000, 5000, 0,   0, 0, 30, 0},
    {"TREBLE BOOST",L3(R_BOOST, R_TONE, R_LEVEL),           300,   0,   0, 3500,10, 7,  6, 26, CL_ASYM, TM_LP,  6000,15000, 0,   0, 0, 20, 0},
    {"EP BOOST",    L3(R_BOOST, R_TONE, R_LEVEL),            30,   0,   0, 2500, 2, 5,  0, 20, CL_BOOST,TM_LP,  5000,18000, 0,   0, 0, 0, 0},
    {"SWEDE HM-2",  L4(R_DIST, R_LOW, R_HIGH, R_LEVEL),      50,   0,   0,    0, 0, 7, 25, 50, CL_HARD, TM_HM2,    87, 1100, 0,   0, 0, 0, 0},
    {"METAL ZONE",  L4(R_DIST, R_LOW, R_HIGH, R_LEVEL),     106,   0,   0, 1000, 8, 7, 30, 60, CL_HARD, TM_MT2,   105, 4900, 800,-8, 0, 0, 0},
    {"PRECISION",   L4(R_DRIVE, R_TIGHT, R_BRIGHT, R_LEVEL),100,   0,   0,    0, 0, 7,  6, 30, CL_LED,  TM_LP,  6000, 6000, 0,   0, 0, 0, 0},
    {"BASS DI 7K",  L4(R_DRIVE, R_TONE, R_BLEND, R_LEVEL),   40,   0,   0,    0, 0, 7, 10, 40, CL_LED,  TM_LP,  1500, 8000, 0,   0, 0, 0, 0},
};

/* =============================================================== amps ====== */
typedef struct {
    const char *name;
    uint8_t n, hard;
    float g[4], bias[4];
    uint16_t lp[4], hp[4];                                /* inter-stage low-pass / coupling high-pass */
    uint16_t tight, bright; int8_t bright_db;             /* input high-pass, bright cap (fades with gain) */
    uint16_t bass, mid; uint8_t midq10; uint16_t treb;    /* tone stack */
    int8_t rb, rm, rt, scoop;                             /* knob ranges (dB), fixed mid scoop at noon */
    uint16_t pres, res; int8_t res_db;                    /* presence, resonance (power amp + speaker) */
    uint16_t vf; int8_t vdb;                              /* fixed voice peak */
    uint8_t sag100, pw10;                                 /* power-supply sag, power-amp drive */
} amp_v;
static const amp_v AV[PX_AMP_COUNT] = {
  {"TWIN CLEAN",  1,0, {1.0f,0,0,0},          {0,0,0,0},             {12000,0,0,0},          {25,0,0,0},     30,    0,0,  100, 400, 7, 2400, 10,8,10, -6, 4500, 90,3,    0, 0, 15,10},
  {"DELUXE 65",   2,0, {1.3f,1.6f,0,0},       {0.02f,0.03f,0,0},     {11000,9000,0,0},       {25,40,0,0},    40,    0,0,  110, 400, 7, 2400, 10,8,10, -5, 4200, 90,3,    0, 0, 35,13},
  {"PRINCETONE",  2,0, {1.2f,1.5f,0,0},       {0.02f,0.03f,0,0},     {10000,8000,0,0},       {25,40,0,0},    45,    0,0,  110, 400, 7, 2200, 10,8,10, -5, 3500, 95,3,    0, 0, 40,14},
  {"TWEED 59",    3,0, {1.6f,2.0f,1.8f,0},    {0.03f,0.05f,0.04f,0}, {8000,6500,6000,0},     {15,30,40,0},   40,    0,0,  142, 500, 8, 2500, 10,9,9,  -3, 4000,100,3,    0, 0, 55,14},
  {"TWEED CHAMP", 2,0, {2.0f,2.4f,0,0},       {0.05f,0.07f,0,0},     {6000,5000,0,0},        {40,60,0,0},    70,    0,0,  150, 800, 7, 2500,  6,0,8,   0, 3500,120,2,    0, 0, 70,18},
  {"AC 30 TB",    3,0, {1.8f,2.2f,2.0f,0},    {0.03f,0.05f,0.04f,0}, {10000,9000,8000,0},    {25,40,60,0},   60,    0,0,  150, 800, 9, 1800,  9,8,11, -5, 5000,110,3,    0, 0, 30,16},
  {"AC 15",       2,0, {2.0f,2.4f,0,0},       {0.04f,0.06f,0,0},     {9000,8000,0,0},        {30,50,0,0},    65,    0,0,  150, 800, 9, 1800,  9,8,11, -4, 5000,110,3,    0, 0, 45,17},
  {"JTM 45",      3,0, {1.8f,2.2f,2.0f,0},    {0.04f,0.06f,0.05f,0}, {7500,6500,6000,0},     {30,45,55,0},   60,  720,4,  219, 650, 8, 2000, 10,9,10, -3, 4000, 95,3,    0, 0, 50,13},
  {"PLEXI 68",    3,0, {2.2f,2.6f,2.4f,0},    {0.05f,0.07f,0.05f,0}, {8500,7000,6500,0},     {35,50,60,0},   87,  720,6,  220, 650, 8, 2000, 11,10,10,-3, 4000, 95,3,    0, 0, 40,14},
  {"BRIT 800",    3,0, {2.6f,3.2f,2.8f,0},    {0.04f,0.07f,0.05f,0}, {9000,7000,6000,0},     {35,55,72,0},   90,  720,3,  220, 680, 8, 2000, 11,10,10,-2, 3800, 95,3,    0, 0, 30,13},
  {"BRIT JVM",    4,0, {2.6f,3.0f,3.2f,2.6f}, {0.04f,0.06f,0.07f,0.05f},{9000,7500,6500,5500},{40,55,70,90}, 105,   0,0,  200, 700, 8, 2200, 12,10,10,-3, 3800, 95,4,    0, 0, 30,13},
  {"HIWATT DR",   2,0, {1.6f,1.8f,0,0},       {0.02f,0.03f,0,0},     {12000,10000,0,0},      {30,45,0,0},    40,    0,0,  100, 500, 7, 3000, 10,8,10, -1, 4500, 90,2,    0, 0, 10,11},
  {"ORANGE 120",  4,0, {2.4f,2.8f,3.0f,2.4f}, {0.04f,0.06f,0.07f,0.05f},{7000,6000,5500,5000},{35,50,65,80},  75,    0,0,  150, 600, 7, 1500, 12,10,10,-2, 3500,100,4,  350, 3, 35,14},
  {"CHIME 30",    3,0, {1.8f,2.2f,2.0f,0},    {0.03f,0.05f,0.04f,0}, {10000,9000,8000,0},    {25,40,55,0},   60,  900,3,  150, 800, 9, 2000,  9,8,10, -5, 5000,110,3,    0, 0, 35,16},
  {"ODS LEAD",    4,0, {2.2f,2.6f,2.8f,2.2f}, {0.04f,0.06f,0.06f,0.04f},{7000,6000,5000,4500},{35,50,60,80}, 100, 1500,3,   80, 500, 7, 2400, 12,10,10,-4, 3800, 90,3,    0, 0, 30,12},
  {"SLO 100",     4,0, {2.8f,3.2f,3.4f,2.8f}, {0.05f,0.07f,0.08f,0.05f},{8000,6500,5500,5000},{40,55,70,88}, 100,   0,0,  170, 650, 8, 2200, 12,11,10,-3, 3800, 95,3,    0, 0, 30,13},
  {"MARK IIC+",   4,0, {2.6f,3.2f,3.4f,2.6f}, {0.04f,0.06f,0.08f,0.05f},{9000,7000,6000,5000},{40,60,80,100},125,   0,0,   90, 750,10, 3400, 12,12,10,-4, 4200, 90,3,    0, 0, 20,12},
  {"RECTIFIED",   4,0, {3.0f,3.2f,3.4f,3.0f}, {0.05f,0.08f,0.06f,0.05f},{7000,5500,6500,6000},{30,45,60,85},  85,    0,0,   85, 700,10, 2800, 12,12,9, -6, 4500, 80,4,    0, 0, 50,13},
  {"5150 LEAD",   4,0, {3.2f,3.6f,3.8f,3.2f}, {0.06f,0.09f,0.07f,0.05f},{8000,6000,5000,5000},{35,50,70,90}, 110,   0,0,  180, 650, 9, 3600, 12,11,11,-4, 4200,110,3,    0, 0, 25,13},
  {"5150 III",    4,0, {3.4f,3.8f,4.0f,3.4f}, {0.06f,0.09f,0.07f,0.05f},{8500,6500,5500,5200},{40,60,80,100},125,   0,0,  170, 700, 9, 3500, 12,11,11,-4, 4200,100,3,    0, 0, 20,13},
  {"ECSTASY",     4,0, {2.8f,3.2f,3.4f,2.8f}, {0.05f,0.08f,0.06f,0.05f},{8000,6500,5500,5000},{35,50,65,90}, 100,   0,0,  110, 650, 9, 3000, 12,11,10,-3, 3600, 95,3,    0, 0, 30,13},
  {"BE LEAD",     4,0, {2.8f,3.4f,3.6f,2.8f}, {0.05f,0.07f,0.07f,0.05f},{8500,7000,6000,5200},{35,50,65,90},  90,  720,3,  220, 650, 8, 2200, 11,10,10,-2, 3800, 95,3,    0, 0, 30,13},
  {"UBER VH4",    4,0, {3.0f,3.4f,3.6f,3.4f}, {0.04f,0.07f,0.07f,0.05f},{7500,6500,5500,4800},{45,60,75,100},125,   0,0,  170, 800,10, 3300, 12,12,10,-3, 3400,100,3,    0, 0, 25,13},
  {"EURO BALL",   4,0, {2.8f,3.4f,3.6f,3.0f}, {0.04f,0.07f,0.08f,0.05f},{8000,7000,6000,5200},{40,55,70,100},125,   0,0,  150, 650,10, 3500, 12,12,10,-6, 3800,100,3,    0, 0, 20,13},
  {"REVV GEN",    4,0, {3.0f,3.6f,3.8f,3.2f}, {0.05f,0.08f,0.07f,0.05f},{8500,7000,6000,5200},{40,60,80,100},120,   0,0,  160, 700, 9, 3200, 12,11,10,-2, 4000,100,3, 2000, 3, 20,13},
  {"JC CLEAN",    0,0, {0,0,0,0},             {0,0,0,0},             {0,0,0,0},              {0,0,0,0},      25,    0,0,  100,1000, 7, 4500, 12,10,12,  0, 5000, 90,0,    0, 0,  0, 8},
  {"SUPRO 64",    3,0, {2.2f,2.6f,2.4f,0},    {0.05f,0.08f,0.06f,0}, {5000,4500,4000,0},     {30,45,55,0},   40,    0,0,  150,1000, 7, 2500,  9,6,9,   0, 3000,110,3, 1000, 3, 70,18},
  {"TWO-ROCK",    2,0, {1.4f,1.8f,0,0},       {0.02f,0.03f,0,0},     {11000,9000,0,0},       {25,40,0,0},    35,    0,0,  100, 500, 7, 2600, 10,8,10, -4, 4500, 90,3,    0, 0, 20,11},
  {"SVT BASS",    2,0, {1.4f,1.6f,0,0},       {0.03f,0.04f,0,0},     {9000,7000,0,0},        {15,25,0,0},    25,    0,0,   40, 500, 6, 4000, 12,10,10,  0, 5000, 60,2,    0, 0, 40,14},
};
#include "px_trim.h"                                   /* per-amp / per-drive level matching (calibrated on PC) */

/* ============================================================ cabinets ===== */
typedef struct {
    const char *name;
    uint16_t hp; uint8_t hpq10; uint16_t bump; int8_t bump_db;
    uint16_t d1; int8_t d1db; uint16_t d2; int8_t d2db;
    uint16_t p1; int8_t p1db; uint16_t p2; int8_t p2db; uint16_t lp;
} cab_v;
static const cab_v CV[PX_CAB_COUNT] = {
    /* name          hp  q  bump dB   dip1 dB  dip2 dB  pres1 dB pres2 dB  lp */
    {"1x10 OPEN",    110, 9, 125, 3,  1000,-2,    0, 0,  2500, 5,  500, 2,  5000},
    {"1x12 US OPEN",  95, 9, 105, 3,   650,-3,    0, 0,  2700, 5, 5000, 3,  6500},
    {"2x10 VIBRO",    95, 9, 105, 3,   800,-2,    0, 0,  2500, 5, 4000, 1,  5800},
    {"2x12 BLUE",     80, 8,  90, 3,  2000,-3,    0, 0,  1350, 3, 3500, 6,  6000},
    {"2x12 BRIT",     80, 8,  90, 4,  2800,-3,    0, 0,  2000, 4, 3800, 4,  5000},
    {"4x10 TWEED",    80, 8,  97, 4,   900,-2,    0, 0,  2500, 5, 4000, 1,  5500},
    {"4x12 GREEN",    70, 7, 100, 5,  2800,-3,    0, 0,  2000, 4, 3500, 3,  4800},
    {"4x12 T75",      75, 7, 105, 5,   650,-3,    0, 0,  2500, 5, 5000, 5,  6000},
    {"4x12 V30",      70, 7, 105, 6,  1250,-2, 3500,-4,  2500, 6, 4800, 4,  5500},
    {"4x12 H30",      75, 7, 110, 5,  1500,-2,    0, 0,  3000, 5, 5000, 3,  5500},
    {"1x12 CREAM",    80, 8,  90, 4,  1500,-2,    0, 0,  2500, 4, 4500, 3,  6000},
    {"1x15 OPEN",     55, 8,  65, 4,  1000,-3,    0, 0,  3500, 4,    0, 0,  9000},
    {"8x10 BASS",     55, 7,  80, 4,   500,-2,    0, 0,  1500, 3,    0, 0,  5000},
    {"4x10 HORN",     45, 7,  55, 3,     0, 0,    0, 0,  2000, 1, 8000, 2, 15000},
    {"DI (NO CAB)",    0, 0,   0, 0,     0, 0,    0, 0,     0, 0,    0, 0,     0},
};

/* ================================================================== EQ ===== */
static const char *const PN_EQ5[] = {"100 HZ", "400 HZ", "1 KHZ", "2.5 KHZ", "6 KHZ"};
static const char *const PN_GEQ[] = {"80 HZ", "240 HZ", "750 HZ", "2.2 KHZ", "6.6 KHZ"};
static const char *const PN_PEQ[] = {"LOW", "MID FREQ", "MID GAIN", "MID Q", "HIGH"};
static const tdef T_EQ[PX_EQ_COUNT] = {{"5 BAND", 5, PN_EQ5}, {"GRAPHIC 5", 5, PN_GEQ}, {"PARAMETRIC", 5, PN_PEQ}};

/* ========================================================== modulation ===== */
static const char *const PN_RDM[] = {"RATE", "DEPTH", "MIX"};
static const char *const PN_DIM[] = {"MODE", "MIX"};
static const char *const PN_FLG[] = {"RATE", "DEPTH", "MANUAL", "FDBK", "MIX"};
static const char *const PN_P90[] = {"SPEED", "DEPTH"};
static const char *const PN_STN[] = {"RATE", "COLOR", "DEPTH"};
static const char *const PN_UVB[] = {"SPEED", "INTENS", "MIX"};
static const char *const PN_RD[] = {"RATE", "DEPTH"};
static const char *const PN_BTR[] = {"RATE", "DEPTH", "SHAPE"};
static const char *const PN_HTR[] = {"RATE", "DEPTH", "TONE"};
static const char *const PN_ROT[] = {"SPEED", "DEPTH", "BALANCE"};
static const tdef T_MOD[PX_MOD_COUNT] = {
    {"CE CHORUS", 3, PN_RDM}, {"DIMENSION", 2, PN_DIM}, {"FLANGER", 5, PN_FLG}, {"PHASE 90", 2, PN_P90},
    {"SMALL STONE", 3, PN_STN}, {"UNI-VIBE", 3, PN_UVB}, {"OPTO TREMOLO", 2, PN_RD}, {"BIAS TREMOLO", 3, PN_BTR},
    {"HARMONIC TREM", 3, PN_HTR}, {"ROTARY", 3, PN_ROT}, {"VIBRATO", 2, PN_RD}};

/* =============================================================== delay ===== */
static const char *const PN_DT[] = {"TIME", "FDBK", "MIX", "TONE"};
static const char *const PN_DMM[] = {"TIME", "FDBK", "MIX", "RATE", "DEPTH"};
static const char *const PN_DTP[] = {"TIME", "FDBK", "MIX", "TONE", "WOW"};
static const char *const PN_DSP[] = {"TIME", "FDBK", "MIX", "HEADS", "WOW"};
static const char *const PN_DRV2[] = {"TIME", "FDBK", "MIX"};
static const char *const PN_DLF[] = {"TIME", "FDBK", "MIX", "GRIT"};
static const tdef T_DLY[PX_DLY_COUNT] = {
    {"DIGITAL", 4, PN_DT}, {"ANALOG BBD", 4, PN_DT}, {"MEMORY MAN", 5, PN_DMM}, {"TAPE EP-3", 5, PN_DTP},
    {"SPACE ECHO", 5, PN_DSP}, {"REVERSE", 3, PN_DRV2}, {"LO-FI", 4, PN_DLF}, {"DUAL TAP", 4, PN_DT}};

/* ============================================================== reverb ===== */
static const char *const PN_RV[] = {"DECAY", "TONE", "MIX"};
static const char *const PN_RSP[] = {"DECAY", "TONE", "MIX", "DRIP"};
static const char *const PN_RSH[] = {"DECAY", "TONE", "MIX", "SHIMMER"};
static const char *const PN_RGT[] = {"HOLD", "TONE", "MIX"};
static const tdef T_REV[PX_REV_COUNT] = {
    {"ROOM", 3, PN_RV}, {"HALL", 3, PN_RV}, {"CATHEDRAL", 3, PN_RV}, {"PLATE", 3, PN_RV}, {"SPRING", 4, PN_RSP},
    {"SHIMMER", 4, PN_RSH}, {"GATED", 3, PN_RGT}, {"AMBIENT", 3, PN_RV}};

/* ------------------------------------------------------------ catalog API --- */
static const char *const PN_AMP[] = {"GAIN", "BASS", "MID", "TREBLE", "PRESENCE"};
static const char *const PN_CAB[] = {"BODY", "BRIGHT", "MIC"};

const char *px_block_label(int b) { return (b >= 0 && b < PX_NBLK) ? BLK_LABEL[b] : "?"; }
const char *px_block_name(int b)  { return (b >= 0 && b < PX_NBLK) ? BLK_NAME[b] : "?"; }
int px_ntypes(int b)
{
    static const uint8_t N[PX_NBLK] = {1, PX_CMP_COUNT, PX_PIT_COUNT, PX_SFX_COUNT, PX_DRV_COUNT, PX_AMP_COUNT,
                                       PX_CAB_COUNT, PX_EQ_COUNT, PX_MOD_COUNT, PX_DLY_COUNT, PX_REV_COUNT};
    return (b >= 0 && b < PX_NBLK) ? N[b] : 1;
}
static const tdef *tdef_of(int b, int t)
{
    switch (b) {
    case PX_B_GATE: return &T_GATE[0];
    case PX_B_COMP: return &T_COMP[t];
    case PX_B_PITCH: return &T_PITCH[t];
    case PX_B_SFX: return &T_SFX[t];
    case PX_B_EQ: return &T_EQ[t];
    case PX_B_MOD: return &T_MOD[t];
    case PX_B_DELAY: return &T_DLY[t];
    case PX_B_REVERB: return &T_REV[t];
    }
    return 0;
}
const char *px_type_name(int b, int t)
{
    if (t < 0 || t >= px_ntypes(b)) t = 0;
    if (b == PX_B_DRIVE) return DV[t].name;
    if (b == PX_B_AMP) return AV[t].name;
    if (b == PX_B_CAB) return CV[t].name;
    const tdef *d = tdef_of(b, t);
    return d ? d->name : "?";
}
int px_nparams(int b, int t)
{
    if (t < 0 || t >= px_ntypes(b)) t = 0;
    if (b == PX_B_DRIVE) return DV[t].np;
    if (b == PX_B_AMP) return 5;
    if (b == PX_B_CAB) return t == PX_CAB_DI ? 0 : 3;
    const tdef *d = tdef_of(b, t);
    return d ? d->np : 0;
}
const char *px_param_name(int b, int t, int k)
{
    if (t < 0 || t >= px_ntypes(b)) t = 0;
    if (k < 0 || k >= px_nparams(b, t)) return "";
    if (b == PX_B_DRIVE) return ROLE_NAME[DV[t].role[k]];
    if (b == PX_B_AMP) return PN_AMP[k];
    if (b == PX_B_CAB) return PN_CAB[k];
    return tdef_of(b, t)->pn[k];
}

/* value text for knobs where a number 0..100 says little (intervals, ms, bits...) */
static const int8_t HARM_ST[10] = {-12, -7, -5, -4, -3, 3, 4, 5, 7, 12};
static const char *const HARM_TXT[10] = {"-OCT", "-5TH", "-4TH", "-3RD", "-m3", "+m3", "+3RD", "+4TH", "+5TH", "+OCT"};
static const char *const HEAD_TXT[7] = {"1", "2", "3", "1+2", "2+3", "1+3", "ALL"};
static int harm_idx(int v) { int i = v * 10 / 101; return i < 0 ? 0 : (i > 9 ? 9 : i); }
static float dly_ms(int t, int v) { return t == PX_DLY_REVERSE ? 250.0f + (float)v * 7.5f : 20.0f + (float)v * 9.8f; }
static void put_int(char *b, int v, int plus)
{
    char t[8]; int n = 0, neg = v < 0;
    if (neg) v = -v;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 6);
    int i = 0;
    if (neg) b[i++] = '-'; else if (plus && n && !(n == 1 && t[0] == '0')) b[i++] = '+';
    while (n) b[i++] = t[--n];
    b[i] = 0;
}
static void put_str(char *b, const char *s) { int i = 0; while (s[i] && i < 7) { b[i] = s[i]; i++; } b[i] = 0; }
int px_param_text(int b, int t, int k, int v, char *buf)
{
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    float x = (float)v * 0.01f;
    switch (b) {
    case PX_B_PITCH:
        if (t == PX_PIT_HARMONY && k == 0) { put_str(buf, HARM_TXT[harm_idx(v)]); return 1; }
        if (t == PX_PIT_WHAMMY && k == 0) { float st = (x - 0.5f) * 48.0f; put_int(buf, (int)(st + (st >= 0 ? 0.5f : -0.5f)), 1); return 1; }
        if (t == PX_PIT_DETUNE && k == 0) { put_int(buf, (int)(3.0f + x * 27.0f + 0.5f), 0); return 1; }
        break;
    case PX_B_SFX:
        if (t == PX_SFX_CRUSH && k == 0) { put_int(buf, 16 - (int)(x * 14.0f + 0.5f), 0); return 1; }
        if (t == PX_SFX_CRUSH && k == 1) { put_int(buf, 1 + (int)(x * 39.0f + 0.5f), 0); return 1; }
        if (t == PX_SFX_RING && k == 0) { put_int(buf, (int)(20.0f * powf(100.0f, x) + 0.5f), 0); return 1; }
        if (t == PX_SFX_SLICER && k == 2) { buf[0] = 'P'; put_int(buf + 1, 1 + (int)(x * 7.99f), 0); return 1; }
        break;
    case PX_B_EQ:
        if (t == PX_EQ_PARAM && k == 1) { put_int(buf, (int)(150.0f * powf(33.0f, x) + 0.5f), 0); return 1; }
        if (t == PX_EQ_PARAM && k == 3) { int q = (int)((0.4f + x * 5.6f) * 10.0f + 0.5f);
            buf[0] = (char)('0' + q / 10); buf[1] = '.'; buf[2] = (char)('0' + q % 10); buf[3] = 0; return 1; }
        { float db = (x - 0.5f) * 24.0f; put_int(buf, (int)(db + (db >= 0 ? 0.5f : -0.5f)), 1); return 1; }
    case PX_B_MOD:
        if (t == PX_MOD_DIMENSION && k == 0) { put_int(buf, 1 + (int)(x * 3.99f), 0); return 1; }
        if (t == PX_MOD_ROTARY && k == 0) { put_str(buf, v < 50 ? "SLOW" : "FAST"); return 1; }
        break;
    case PX_B_DELAY:
        if (k == 0) { put_int(buf, (int)(dly_ms(t, v) + 0.5f), 0); return 1; }
        if (t == PX_DLY_SPACE && k == 3) { put_str(buf, HEAD_TXT[(int)(x * 6.99f)]); return 1; }
        break;
    }
    return 0;
}

/* ============================================================= presets ===== */
static void blk(px_preset_t *p, int b, int on, int type, int a, int c, int d, int e, int f)
{
    p->on[b] = (uint8_t)on; p->type[b] = (uint8_t)type;
    p->p[b][0] = (uint8_t)a; p->p[b][1] = (uint8_t)c; p->p[b][2] = (uint8_t)d; p->p[b][3] = (uint8_t)e; p->p[b][4] = (uint8_t)f;
}
static void base_preset(px_preset_t *p)
{
    memset(p, 0, sizeof(*p));
    blk(p, PX_B_GATE,   1, 0, 40, 20, 0, 0, 0);
    blk(p, PX_B_COMP,   0, PX_CMP_SQUASH, 50, 30, 50, 100, 0);
    blk(p, PX_B_PITCH,  0, PX_PIT_OCTAVER, 70, 50, 0, 0, 40);
    blk(p, PX_B_SFX,    0, PX_SFX_AUTOWAH, 60, 60, 60, 40, 0);
    blk(p, PX_B_DRIVE,  0, PX_DRV_TS808, 30, 50, 60, 50, 50);
    blk(p, PX_B_AMP,    1, PX_AMP_PLEXI, 60, 60, 50, 60, 50);
    blk(p, PX_B_CAB,    1, PX_CAB_412V30, 50, 50, 50, 0, 0);
    blk(p, PX_B_EQ,     0, PX_EQ_5BAND, 50, 50, 50, 50, 50);
    blk(p, PX_B_MOD,    0, PX_MOD_CHORUS, 30, 40, 40, 30, 50);
    blk(p, PX_B_DELAY,  0, PX_DLY_DIGITAL, 35, 30, 25, 50, 30);
    blk(p, PX_B_REVERB, 0, PX_REV_HALL, 50, 50, 20, 40, 0);
}
static void setname(px_preset_t *p, const char *s)
{
    int i = 0;
    for (; s[i] && i < (int)sizeof(p->name) - 1; i++) p->name[i] = s[i];
    p->name[i] = 0;
}
enum { NPRESETS = 42 };
int px_preset_count(void) { return NPRESETS; }
void px_preset_factory(px_preset_t *p, int idx)
{
    base_preset(p);
    if (idx < 0 || idx >= NPRESETS) idx = 0;
    switch (idx) {
    case 0: setname(p, "GLASS CLEAN"); p->on[PX_B_GATE] = 0;
        blk(p, PX_B_AMP, 1, PX_AMP_TWIN, 35, 55, 45, 65, 55); blk(p, PX_B_CAB, 1, PX_CAB_112JN, 50, 50, 55, 0, 0);
        blk(p, PX_B_COMP, 1, PX_CMP_STUDIO, 35, 40, 55, 60, 0); blk(p, PX_B_MOD, 1, PX_MOD_CHORUS, 25, 45, 50, 0, 0);
        blk(p, PX_B_REVERB, 1, PX_REV_PLATE, 45, 55, 25, 0, 0); break;
    case 1: setname(p, "JAZZ BOX"); p->on[PX_B_GATE] = 0;
        blk(p, PX_B_AMP, 1, PX_AMP_JC120, 30, 65, 55, 30, 30); blk(p, PX_B_CAB, 1, PX_CAB_212BLUE, 60, 35, 35, 0, 0);
        blk(p, PX_B_COMP, 1, PX_CMP_OPTO, 30, 55, 70, 0, 0); blk(p, PX_B_REVERB, 1, PX_REV_ROOM, 45, 40, 22, 0, 0); break;
    case 2: setname(p, "COUNTRY SNAP"); p->on[PX_B_GATE] = 0;
        blk(p, PX_B_AMP, 1, PX_AMP_DELUXE, 35, 45, 50, 70, 60); blk(p, PX_B_CAB, 1, PX_CAB_112JN, 45, 60, 60, 0, 0);
        blk(p, PX_B_COMP, 1, PX_CMP_SQUASH, 60, 35, 55, 100, 0);
        blk(p, PX_B_DELAY, 1, PX_DLY_TAPE, 10, 5, 30, 60, 20); blk(p, PX_B_REVERB, 1, PX_REV_SPRING, 40, 50, 18, 40, 0); break;
    case 3: setname(p, "TEXAS BLUES"); p->on[PX_B_GATE] = 0;
        blk(p, PX_B_DRIVE, 1, PX_DRV_TS808, 25, 55, 75, 0, 0);
        blk(p, PX_B_AMP, 1, PX_AMP_DELUXE, 60, 60, 55, 60, 55); blk(p, PX_B_CAB, 1, PX_CAB_410BASSMAN, 55, 50, 50, 0, 0);
        blk(p, PX_B_REVERB, 1, PX_REV_SPRING, 45, 45, 20, 40, 0); break;
    case 4: setname(p, "TWEED BLUES"); p->on[PX_B_GATE] = 0;
        blk(p, PX_B_AMP, 1, PX_AMP_BASSMAN, 65, 55, 60, 55, 50); blk(p, PX_B_CAB, 1, PX_CAB_410BASSMAN, 50, 50, 50, 0, 0);
        blk(p, PX_B_REVERB, 1, PX_REV_ROOM, 45, 45, 18, 0, 0); break;
    case 5: setname(p, "BRIT JANGLE"); p->on[PX_B_GATE] = 0;
        blk(p, PX_B_AMP, 1, PX_AMP_AC30, 45, 50, 50, 65, 50); blk(p, PX_B_CAB, 1, PX_CAB_212BLUE, 50, 50, 50, 0, 0);
        blk(p, PX_B_COMP, 1, PX_CMP_STUDIO, 40, 40, 55, 50, 0);
        blk(p, PX_B_DELAY, 1, PX_DLY_DIGITAL, 30, 20, 18, 55, 0); blk(p, PX_B_REVERB, 1, PX_REV_ROOM, 45, 50, 18, 0, 0); break;
    case 6: setname(p, "60S PSYCH"); p->on[PX_B_GATE] = 0;
        blk(p, PX_B_DRIVE, 1, PX_DRV_FUZZGE, 70, 55, 55, 0, 0);
        blk(p, PX_B_AMP, 1, PX_AMP_PLEXI, 45, 55, 55, 55, 50); blk(p, PX_B_CAB, 1, PX_CAB_412GB, 50, 50, 50, 0, 0);
        blk(p, PX_B_MOD, 1, PX_MOD_UNIVIBE, 35, 70, 50, 0, 0); blk(p, PX_B_REVERB, 1, PX_REV_HALL, 45, 45, 18, 0, 0); break;
    case 7: setname(p, "70S ROCK");
        blk(p, PX_B_AMP, 1, PX_AMP_PLEXI, 75, 55, 60, 60, 55); blk(p, PX_B_CAB, 1, PX_CAB_412GB, 50, 50, 50, 0, 0);
        blk(p, PX_B_REVERB, 1, PX_REV_HALL, 40, 50, 12, 0, 0); break;
    case 8: setname(p, "80S HAIR");
        blk(p, PX_B_DRIVE, 1, PX_DRV_SD1, 20, 55, 70, 0, 0);
        blk(p, PX_B_AMP, 1, PX_AMP_JCM800, 70, 55, 60, 60, 55); blk(p, PX_B_CAB, 1, PX_CAB_412T75, 50, 55, 50, 0, 0);
        blk(p, PX_B_MOD, 1, PX_MOD_CHORUS, 20, 35, 35, 0, 0);
        blk(p, PX_B_DELAY, 1, PX_DLY_DIGITAL, 40, 25, 18, 55, 0); blk(p, PX_B_REVERB, 1, PX_REV_HALL, 50, 50, 16, 0, 0); break;
    case 9: setname(p, "BRIT CRUNCH"); p->on[PX_B_GATE] = 0;
        blk(p, PX_B_DRIVE, 1, PX_DRV_KLON, 25, 55, 70, 0, 0);
        blk(p, PX_B_AMP, 1, PX_AMP_JTM45, 55, 55, 60, 55, 50); blk(p, PX_B_CAB, 1, PX_CAB_212GB, 50, 50, 50, 0, 0);
        blk(p, PX_B_REVERB, 1, PX_REV_ROOM, 40, 50, 15, 0, 0); break;
    case 10: setname(p, "SMOOTH LEAD");
        blk(p, PX_B_DRIVE, 1, PX_DRV_ZEN, 30, 50, 50, 65, 0);
        blk(p, PX_B_AMP, 1, PX_AMP_DUMBLE, 65, 50, 55, 55, 45); blk(p, PX_B_CAB, 1, PX_CAB_112CREAM, 55, 45, 50, 0, 0);
        blk(p, PX_B_DELAY, 1, PX_DLY_ANALOG, 40, 30, 22, 50, 0); blk(p, PX_B_REVERB, 1, PX_REV_HALL, 50, 45, 18, 0, 0); break;
    case 11: setname(p, "SPACE LEAD");
        blk(p, PX_B_AMP, 1, PX_AMP_SLO, 70, 55, 60, 55, 50); blk(p, PX_B_CAB, 1, PX_CAB_412V30, 50, 50, 50, 0, 0);
        blk(p, PX_B_DELAY, 1, PX_DLY_MEMORY, 50, 45, 30, 30, 40); blk(p, PX_B_REVERB, 1, PX_REV_HALL, 65, 45, 25, 0, 0); break;
    case 12: setname(p, "DOTTED 8TH"); p->on[PX_B_GATE] = 0;
        blk(p, PX_B_COMP, 1, PX_CMP_STUDIO, 45, 40, 55, 60, 0);
        blk(p, PX_B_AMP, 1, PX_AMP_AC30, 50, 50, 55, 60, 50); blk(p, PX_B_CAB, 1, PX_CAB_212BLUE, 50, 50, 50, 0, 0);
        blk(p, PX_B_DELAY, 1, PX_DLY_DUAL, 45, 40, 35, 60, 0); blk(p, PX_B_REVERB, 1, PX_REV_HALL, 55, 55, 22, 0, 0); break;
    case 13: setname(p, "MARK SOLO");
        blk(p, PX_B_AMP, 1, PX_AMP_MARK, 75, 55, 60, 55, 50); blk(p, PX_B_CAB, 1, PX_CAB_412V30, 50, 50, 50, 0, 0);
        blk(p, PX_B_EQ, 1, PX_EQ_GEQ, 55, 52, 42, 55, 55);
        blk(p, PX_B_DELAY, 1, PX_DLY_DIGITAL, 45, 35, 22, 50, 0); break;
    case 14: setname(p, "THRASH"); p->p[PX_B_GATE][0] = 50;
        blk(p, PX_B_DRIVE, 1, PX_DRV_TS9, 0, 60, 80, 0, 0);
        blk(p, PX_B_AMP, 1, PX_AMP_5150, 70, 60, 45, 60, 55); blk(p, PX_B_CAB, 1, PX_CAB_412V30, 50, 50, 50, 0, 0); break;
    case 15: setname(p, "GROOVE METAL"); p->p[PX_B_GATE][0] = 55;
        blk(p, PX_B_AMP, 1, PX_AMP_5150III, 75, 65, 35, 60, 55); blk(p, PX_B_CAB, 1, PX_CAB_412V30, 55, 50, 50, 0, 0);
        blk(p, PX_B_EQ, 1, PX_EQ_GEQ, 60, 50, 35, 55, 55); break;
    case 16: setname(p, "DJENT"); p->p[PX_B_GATE][0] = 60;
        blk(p, PX_B_DRIVE, 1, PX_DRV_PRECISION, 10, 70, 55, 75, 0);
        blk(p, PX_B_AMP, 1, PX_AMP_REVV, 70, 55, 50, 60, 55); blk(p, PX_B_CAB, 1, PX_CAB_412V30, 45, 55, 50, 0, 0); break;
    case 17: setname(p, "DOOM SLUDGE");
        blk(p, PX_B_DRIVE, 1, PX_DRV_RUSSIAN, 60, 40, 60, 0, 0);
        blk(p, PX_B_AMP, 1, PX_AMP_ORANGE, 70, 70, 55, 45, 40); blk(p, PX_B_CAB, 1, PX_CAB_412GB, 65, 40, 45, 0, 0);
        blk(p, PX_B_REVERB, 1, PX_REV_HALL, 55, 35, 18, 0, 0); break;
    case 18: setname(p, "SWEDE DEATH"); p->p[PX_B_GATE][0] = 55;
        blk(p, PX_B_DRIVE, 1, PX_DRV_HM2, 80, 100, 100, 60, 0);
        blk(p, PX_B_AMP, 1, PX_AMP_JCM800, 45, 50, 55, 50, 45); blk(p, PX_B_CAB, 1, PX_CAB_412V30, 50, 50, 50, 0, 0); break;
    case 19: setname(p, "SHOEGAZE"); p->on[PX_B_GATE] = 0;
        blk(p, PX_B_DRIVE, 1, PX_DRV_MUFF, 70, 55, 55, 0, 0);
        blk(p, PX_B_AMP, 1, PX_AMP_AC30, 40, 55, 50, 55, 50); blk(p, PX_B_CAB, 1, PX_CAB_212BLUE, 50, 50, 50, 0, 0);
        blk(p, PX_B_DELAY, 1, PX_DLY_REVERSE, 40, 35, 35, 0, 0); blk(p, PX_B_REVERB, 1, PX_REV_CATHEDRAL, 65, 40, 35, 0, 0); break;
    case 20: setname(p, "SWELL PAD"); p->on[PX_B_GATE] = 0;
        blk(p, PX_B_COMP, 1, PX_CMP_SWELL, 45, 60, 60, 0, 0);
        blk(p, PX_B_AMP, 1, PX_AMP_TWIN, 30, 50, 45, 60, 50); blk(p, PX_B_CAB, 1, PX_CAB_112JN, 50, 50, 50, 0, 0);
        blk(p, PX_B_DELAY, 1, PX_DLY_TAPE, 55, 45, 30, 45, 30); blk(p, PX_B_REVERB, 1, PX_REV_SHIMMER, 70, 45, 40, 50, 0); break;
    case 21: setname(p, "FUNK WAH"); p->on[PX_B_GATE] = 0;
        blk(p, PX_B_COMP, 1, PX_CMP_SQUASH, 55, 25, 55, 100, 0); blk(p, PX_B_SFX, 1, PX_SFX_AUTOWAH, 60, 65, 60, 35, 0);
        blk(p, PX_B_AMP, 1, PX_AMP_TWIN, 40, 50, 50, 60, 50); blk(p, PX_B_CAB, 1, PX_CAB_112JN, 50, 50, 50, 0, 0); break;
    case 22: setname(p, "SURF"); p->on[PX_B_GATE] = 0;
        blk(p, PX_B_AMP, 1, PX_AMP_TWIN, 40, 45, 50, 70, 60); blk(p, PX_B_CAB, 1, PX_CAB_212BLUE, 45, 60, 55, 0, 0);
        blk(p, PX_B_REVERB, 1, PX_REV_SPRING, 70, 60, 45, 80, 0); break;
    case 23: setname(p, "SWAMP TREM"); p->on[PX_B_GATE] = 0;
        blk(p, PX_B_AMP, 1, PX_AMP_DELUXE, 50, 55, 50, 55, 50); blk(p, PX_B_CAB, 1, PX_CAB_112JN, 50, 50, 50, 0, 0);
        blk(p, PX_B_MOD, 1, PX_MOD_OPTOTREM, 45, 70, 0, 0, 0); blk(p, PX_B_REVERB, 1, PX_REV_SPRING, 50, 45, 25, 50, 0); break;
    case 24: setname(p, "VIBE ROCK"); p->on[PX_B_GATE] = 0;
        blk(p, PX_B_DRIVE, 1, PX_DRV_FUZZSI, 60, 55, 55, 0, 0);
        blk(p, PX_B_AMP, 1, PX_AMP_PLEXI, 55, 55, 55, 55, 50); blk(p, PX_B_CAB, 1, PX_CAB_412GB, 50, 50, 50, 0, 0);
        blk(p, PX_B_MOD, 1, PX_MOD_UNIVIBE, 45, 75, 50, 0, 0); break;
    case 25: setname(p, "ROTARY ORGAN"); p->on[PX_B_GATE] = 0;
        blk(p, PX_B_AMP, 1, PX_AMP_DELUXE, 45, 55, 55, 55, 45); blk(p, PX_B_CAB, 1, PX_CAB_112JN, 50, 50, 50, 0, 0);
        blk(p, PX_B_MOD, 1, PX_MOD_ROTARY, 70, 70, 50, 0, 0); blk(p, PX_B_REVERB, 1, PX_REV_ROOM, 45, 45, 18, 0, 0); break;
    case 26: setname(p, "HARM LEAD");
        blk(p, PX_B_PITCH, 1, PX_PIT_HARMONY, 85, 45, 0, 0, 0);
        blk(p, PX_B_AMP, 1, PX_AMP_JCM800, 70, 55, 60, 55, 50); blk(p, PX_B_CAB, 1, PX_CAB_412T75, 50, 50, 50, 0, 0);
        blk(p, PX_B_DELAY, 1, PX_DLY_DIGITAL, 40, 25, 20, 50, 0); blk(p, PX_B_REVERB, 1, PX_REV_HALL, 50, 50, 15, 0, 0); break;
    case 27: setname(p, "WHAMMY DIVE");
        blk(p, PX_B_PITCH, 1, PX_PIT_WHAMMY, 75, 100, 40, 0, 0);
        blk(p, PX_B_AMP, 1, PX_AMP_RECTO, 70, 60, 45, 60, 55); blk(p, PX_B_CAB, 1, PX_CAB_412V30, 50, 50, 50, 0, 0); break;
    case 28: setname(p, "OCTA FUZZ"); p->on[PX_B_GATE] = 0;
        blk(p, PX_B_DRIVE, 1, PX_DRV_OCTAVIA, 65, 60, 55, 0, 0);
        blk(p, PX_B_AMP, 1, PX_AMP_PLEXI, 50, 55, 55, 55, 50); blk(p, PX_B_CAB, 1, PX_CAB_412GB, 50, 50, 50, 0, 0); break;
    case 29: setname(p, "SQUARE SYNTH"); p->on[PX_B_GATE] = 0;
        blk(p, PX_B_SFX, 1, PX_SFX_SYNTH, 70, 70, 40, 100, 0);
        blk(p, PX_B_AMP, 1, PX_AMP_JC120, 30, 55, 50, 55, 50); blk(p, PX_B_CAB, 1, PX_CAB_212BLUE, 50, 50, 50, 0, 0);
        blk(p, PX_B_DELAY, 1, PX_DLY_DIGITAL, 35, 30, 25, 60, 0); break;
    case 30: setname(p, "RING MOD"); p->on[PX_B_GATE] = 0;
        blk(p, PX_B_SFX, 1, PX_SFX_RING, 45, 80, 20, 0, 0);
        blk(p, PX_B_AMP, 1, PX_AMP_AC30, 50, 50, 50, 55, 50); blk(p, PX_B_CAB, 1, PX_CAB_212BLUE, 50, 50, 50, 0, 0);
        blk(p, PX_B_DELAY, 1, PX_DLY_ANALOG, 45, 40, 30, 50, 0); break;
    case 31: setname(p, "LO-FI TAPE"); p->on[PX_B_GATE] = 0;
        blk(p, PX_B_AMP, 1, PX_AMP_PRINCETON, 40, 50, 55, 45, 40); blk(p, PX_B_CAB, 1, PX_CAB_110, 50, 45, 45, 0, 0);
        blk(p, PX_B_MOD, 1, PX_MOD_VIBRATO, 10, 30, 0, 0, 0);
        blk(p, PX_B_DELAY, 1, PX_DLY_LOFI, 45, 45, 35, 60, 0); blk(p, PX_B_REVERB, 1, PX_REV_ROOM, 50, 30, 25, 0, 0); break;
    case 32: setname(p, "STUTTER FUZZ"); p->on[PX_B_GATE] = 0;
        blk(p, PX_B_DRIVE, 1, PX_DRV_MUFF, 75, 50, 55, 0, 0); blk(p, PX_B_SFX, 1, PX_SFX_SLICER, 50, 100, 30, 0, 0);
        blk(p, PX_B_AMP, 1, PX_AMP_CHAMP, 45, 50, 50, 55, 50); blk(p, PX_B_CAB, 1, PX_CAB_112JN, 50, 50, 50, 0, 0); break;
    case 33: setname(p, "BITCRUSH"); p->on[PX_B_GATE] = 0;
        blk(p, PX_B_SFX, 1, PX_SFX_CRUSH, 70, 30, 80, 0, 0); blk(p, PX_B_DRIVE, 1, PX_DRV_OCD, 50, 50, 55, 0, 0);
        blk(p, PX_B_AMP, 1, PX_AMP_JCM800, 50, 55, 55, 55, 50); blk(p, PX_B_CAB, 1, PX_CAB_412GB, 50, 50, 50, 0, 0); break;
    case 34: setname(p, "BASS DRIVE"); p->on[PX_B_GATE] = 0;
        blk(p, PX_B_COMP, 1, PX_CMP_STUDIO, 45, 30, 55, 60, 0); blk(p, PX_B_DRIVE, 1, PX_DRV_BASSDRV, 50, 55, 50, 55, 0);
        blk(p, PX_B_AMP, 1, PX_AMP_SVT, 40, 60, 50, 50, 40); blk(p, PX_B_CAB, 1, PX_CAB_810SVT, 50, 50, 50, 0, 0); break;
    case 35: setname(p, "BASS OCTAVE"); p->on[PX_B_GATE] = 0;
        blk(p, PX_B_PITCH, 1, PX_PIT_POLYOCT, 70, 60, 0, 0, 0); blk(p, PX_B_COMP, 1, PX_CMP_OPTO, 45, 55, 100, 0, 0);
        blk(p, PX_B_AMP, 1, PX_AMP_SVT, 30, 60, 50, 50, 40); blk(p, PX_B_CAB, 1, PX_CAB_410BASS, 50, 50, 50, 0, 0); break;
    case 36: setname(p, "TWEED CHAMP"); p->on[PX_B_GATE] = 0;
        blk(p, PX_B_AMP, 1, PX_AMP_CHAMP, 60, 50, 50, 55, 50); blk(p, PX_B_CAB, 1, PX_CAB_110, 50, 50, 50, 0, 0);
        blk(p, PX_B_REVERB, 1, PX_REV_ROOM, 40, 45, 15, 0, 0); break;
    case 37: setname(p, "HIWATT STACK"); p->on[PX_B_GATE] = 0;
        blk(p, PX_B_DRIVE, 1, PX_DRV_FUZZSI, 55, 55, 60, 0, 0);
        blk(p, PX_B_AMP, 1, PX_AMP_HIWATT, 65, 55, 55, 60, 55); blk(p, PX_B_CAB, 1, PX_CAB_412T75, 50, 50, 50, 0, 0);
        blk(p, PX_B_REVERB, 1, PX_REV_HALL, 40, 50, 14, 0, 0); break;
    case 38: setname(p, "MODERN HIGH"); p->p[PX_B_GATE][0] = 50;
        blk(p, PX_B_AMP, 1, PX_AMP_POWERBALL, 72, 55, 50, 60, 55); blk(p, PX_B_CAB, 1, PX_CAB_412V30, 50, 50, 50, 0, 0);
        blk(p, PX_B_DELAY, 1, PX_DLY_DIGITAL, 35, 20, 12, 50, 0); break;
    case 39: setname(p, "SUPRO GRIT"); p->on[PX_B_GATE] = 0;
        blk(p, PX_B_AMP, 1, PX_AMP_SUPRO, 65, 55, 60, 50, 50); blk(p, PX_B_CAB, 1, PX_CAB_112JN, 50, 50, 50, 0, 0);
        blk(p, PX_B_DELAY, 1, PX_DLY_SPACE, 30, 30, 22, 30, 30); break;
    case 40: setname(p, "STONER FUZZ");
        blk(p, PX_B_DRIVE, 1, PX_DRV_FFACTORY, 75, 20, 45, 55, 0);
        blk(p, PX_B_AMP, 1, PX_AMP_ORANGE, 55, 65, 55, 50, 45); blk(p, PX_B_CAB, 1, PX_CAB_412GB, 55, 45, 50, 0, 0); break;
    default: setname(p, "CHIME CLEAN"); p->on[PX_B_GATE] = 0;
        blk(p, PX_B_AMP, 1, PX_AMP_MATCHLESS, 40, 50, 55, 60, 50); blk(p, PX_B_CAB, 1, PX_CAB_212BLUE, 50, 50, 50, 0, 0);
        blk(p, PX_B_MOD, 1, PX_MOD_PHASE90, 20, 60, 0, 0, 0); blk(p, PX_B_REVERB, 1, PX_REV_PLATE, 45, 55, 20, 0, 0); break;
    }
}

/* ============================================================ recalc ======= */
static const int RV_COMB[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
static const int RV_AP[4] = {556, 441, 341, 225};
static const int RV_AP_SPRING[4] = {700, 880, 990, 1090};
static const uint16_t SLICE_PAT[8] = {0xAAAA, 0xCCCC, 0xF0F0, 0xB6B6, 0x9249, 0xEEEE, 0xDB6D, 0x8888};

static float role_val(const drv_v *d, const uint8_t *q, int role, float def)
{
    for (int k = 0; k < d->np; k++) if (d->role[k] == role) return f01(q[k]);
    return def;
}

static void rc_dyn(px_engine *e)
{
    const px_preset_t *p = &e->cur;
    float sr = e->sr;
    const uint8_t *q = p->p[PX_B_GATE];
    e->gate_thr = db2lin(-80.0f + f01(q[0]) * 60.0f);
    e->gate_rel = expf(-1.0f / (0.001f * (5.0f + f01(q[1]) * 495.0f) * sr));

    q = p->p[PX_B_COMP];
    int t = p->type[PX_B_COMP] % PX_CMP_COUNT;
    e->cmp_rel = expf(-1.0f / (0.15f * sr));
    e->cmp_mix = 1.0f;
    switch (t) {
    case PX_CMP_SQUASH:
        e->cmp_k = f01(q[0]) * 10.0f;
        e->cmp_att = expf(-1.0f / (0.001f * (1.0f + f01(q[1]) * 49.0f) * sr));
        e->cmp_make = db2lin(-6.0f + f01(q[2]) * 18.0f) * (1.0f + e->cmp_k * 0.15f);
        break;
    case PX_CMP_STUDIO:
        e->cmp_k = f01(q[0]) * 4.0f;
        e->cmp_att = expf(-1.0f / (0.001f * (1.0f + f01(q[1]) * 49.0f) * sr));
        e->cmp_rel = expf(-1.0f / (0.2f * sr));
        e->cmp_make = db2lin(-6.0f + f01(q[2]) * 18.0f) * (1.0f + e->cmp_k * 0.15f);
        e->cmp_mix = f01(q[3]);
        break;
    case PX_CMP_OPTO:                                  /* optical cell: 10 ms attack, slow program-like release */
        e->cmp_k = f01(q[0]) * 6.0f;
        e->cmp_att = expf(-1.0f / (0.010f * sr));
        e->cmp_rel = expf(-1.0f / (0.6f * sr));
        e->cmp_make = db2lin(-6.0f + f01(q[1]) * 18.0f) * (1.0f + e->cmp_k * 0.15f);
        e->cmp_mix = f01(q[2]);
        break;
    default:                                           /* slow gear: volume swell after each pick attack */
        e->cmp_rise = onepole_k(sr, 0.05f + f01(q[0]) * f01(q[0]) * 1.95f);
        e->cmp_thr = db2lin(-30.0f - f01(q[1]) * 30.0f);
        e->cmp_make = db2lin(-6.0f + f01(q[2]) * 18.0f);
        e->cmp_att = expf(-1.0f / (0.001f * sr));
        e->cmp_rel = expf(-1.0f / (0.05f * sr));
        e->cmp_slowk = onepole_k(sr, 0.03f);
        break;
    }
}

static void rc_pitch(px_engine *e)
{
    const px_preset_t *p = &e->cur;
    float sr = e->sr;
    const uint8_t *q = p->p[PX_B_PITCH];
    int t = p->type[PX_B_PITCH] % PX_PIT_COUNT;
    e->ps_nv = 0; e->ps_glide = 1.0f;
    switch (t) {
    case PX_PIT_OCTAVER:                               /* analog: flip-flop divider (-1, -2) + rectifier (+1) */
        bq_lowpass(&e->oct_lp, sr, 500.0f, 0.7f);
        bq_lowpass(&e->oct_lp2, sr, 500.0f, 0.7f);
        bq_lowpass(&e->oct_sublp, sr, 150.0f + f01(q[4]) * 650.0f, 0.7f);
        bq_lowpass(&e->oct_sublp2, sr, 100.0f + f01(q[4]) * 400.0f, 0.7f);
        bq_bandpass(&e->oct_bp, sr, 900.0f, 0.7f);
        bq_highpass(&e->oct_uphp, sr, 60.0f, 0.7f);
        e->oct_dry = f01(q[0]); e->oct_s1 = f01(q[1]) * 2.5f; e->oct_s2 = f01(q[2]) * 2.5f; e->oct_up = f01(q[3]) * 2.0f;
        break;
    case PX_PIT_POLYOCT:                               /* two delay-line shifters: -12 and +12 */
        e->ps_nv = 2; e->ps_dry = f01(q[0]);
        e->ps_W[0] = 1400.0f; e->ps_inc[0] = 0.5f / 1400.0f; e->ps_lvl[0] = f01(q[1]) * 1.2f;
        e->ps_W[1] = 1000.0f; e->ps_inc[1] = -1.0f / 1000.0f; e->ps_lvl[1] = f01(q[2]);
        break;
    case PX_PIT_WHAMMY: {
        float st = (f01(q[0]) - 0.5f) * 48.0f, m = f01(q[1]);
        e->ps_nv = 1; e->ps_dry = 1.0f - m; e->ps_lvl[0] = m;
        e->ps_W[0] = 1400.0f; e->ps_invW = 1.0f / 1400.0f;
        e->ps_rt = expf(st * 0.05776227f);             /* 2^(st/12) */
        if (e->ps_r <= 0.0f) e->ps_r = e->ps_rt;
        e->ps_glide = onepole_k(sr, 0.003f + f01(q[2]) * f01(q[2]) * 0.6f);
    } break;
    case PX_PIT_HARMONY: {
        float r = expf((float)HARM_ST[harm_idx(q[0])] * 0.05776227f);
        e->ps_nv = 1; e->ps_dry = 1.0f; e->ps_lvl[0] = f01(q[1]) * 1.1f;
        e->ps_W[0] = r < 1.0f ? 1400.0f : 1100.0f; e->ps_inc[0] = (1.0f - r) / e->ps_W[0];
    } break;
    default: {                                         /* doubler: +c and -c cents */
        float c = 3.0f + f01(q[0]) * 27.0f, m = f01(q[1]);
        float r0 = expf(c * 0.000577623f), r1 = expf(-c * 0.000577623f);
        e->ps_nv = 2; e->ps_dry = 1.0f - 0.5f * m;
        e->ps_W[0] = 1200.0f; e->ps_inc[0] = (1.0f - r0) / 1200.0f; e->ps_lvl[0] = 0.6f * m;
        e->ps_W[1] = 1100.0f; e->ps_inc[1] = (1.0f - r1) / 1100.0f; e->ps_lvl[1] = 0.6f * m;
    } break;
    }
    if (e->ps_ph[1] == 0.0f && e->ps_ph[0] == 0.0f) e->ps_ph[1] = 0.37f;
}

static void rc_sfx(px_engine *e)
{
    const px_preset_t *p = &e->cur;
    float sr = e->sr;
    const uint8_t *q = p->p[PX_B_SFX];
    int t = p->type[PX_B_SFX] % PX_SFX_COUNT;
    switch (t) {
    case PX_SFX_AUTOWAH:                               /* envelope filter (Mu-Tron style) */
        e->sfx_c = 1.0f + f01(q[0]) * 15.0f;           /* sensitivity */
        e->sfx_qd = 1.0f - f01(q[1]) * 0.85f;
        e->sfx_d = 800.0f + f01(q[2]) * 2700.0f;       /* range */
        e->sfx_a = onepole_k(sr, 0.008f);
        e->sfx_b = onepole_k(sr, 0.05f + f01(q[3]) * 0.45f);
        break;
    case PX_SFX_LFOWAH:
        e->sfx_inc = (0.1f + f01(q[0]) * f01(q[0]) * 7.9f) / sr;
        e->sfx_c = f01(q[1]);
        e->sfx_qd = 1.0f - f01(q[2]) * 0.85f;
        e->sfx_d = f01(q[3]);
        break;
    case PX_SFX_RING:
        e->sfx_inc = 20.0f * powf(100.0f, f01(q[0])) / sr;
        e->sfx_mix = f01(q[1]);
        e->sfx_c = f01(q[2]) * 0.5f;
        e->sfx_d = 1.3f / sr;                          /* wobble LFO */
        break;
    case PX_SFX_CRUSH: {
        int bits = 16 - (int)(f01(q[0]) * 14.0f + 0.5f);
        e->crush_q = powf(2.0f, (float)(bits - 1));
        e->crush_k = 1 + (int)(f01(q[1]) * 39.0f + 0.5f);
        e->sfx_mix = f01(q[2]);
    } break;
    case PX_SFX_SLICER:
        e->sfx_inc = (2.0f + f01(q[0]) * 14.0f) / (16.0f * sr);
        e->sfx_c = f01(q[1]);
        e->sfx_pat = SLICE_PAT[(int)(f01(q[2]) * 7.99f)];
        e->sfx_a = onepole_k(sr, 0.002f);
        break;
    case PX_SFX_LOFI:
        bq_highpass(&e->sfx_f1, sr, 350.0f + f01(q[0]) * 250.0f, 0.7f);
        bq_lowpass(&e->sfx_f2, sr, 1500.0f + f01(q[0]) * 2500.0f, 1.4f);
        e->sfx_c = 1.0f + f01(q[1]) * 8.0f;
        e->sfx_mix = f01(q[2]);
        break;
    default:                                           /* square synth: square + sub through a swept low-pass */
        bq_lowpass(&e->sfx_f1, sr, 700.0f, 0.7f);
        e->sfx_c = f01(q[0]);
        e->sfx_qd = 1.0f - f01(q[1]) * 0.85f;
        e->sfx_d = f01(q[2]);
        e->sfx_mix = f01(q[3]);
        e->sfx_a = onepole_k(sr, 0.002f);
        e->sfx_b = onepole_k(sr, 0.15f);
        if (e->sfx_ff == 0.0f) e->sfx_ff = 1.0f;
        break;
    }
}

static void rc_drive(px_engine *e)
{
    const px_preset_t *p = &e->cur;
    float sr = e->sr, osr = sr * PX_OVERSAMPLE;
    int t = p->type[PX_B_DRIVE] % PX_DRV_COUNT;
    const drv_v *d = &DV[t];
    const uint8_t *q = p->p[PX_B_DRIVE];
    float dr = role_val(d, q, R_DRIVE, -1.0f);
    if (dr < 0.0f) dr = role_val(d, q, R_DIST, -1.0f);
    if (dr < 0.0f) dr = role_val(d, q, R_FUZZ, -1.0f);
    if (dr < 0.0f) dr = role_val(d, q, R_SUST, -1.0f);
    if (dr < 0.0f) dr = role_val(d, q, R_BOOST, 0.5f);
    float tone = role_val(d, q, R_TONE, -1.0f);
    if (tone < 0.0f) tone = role_val(d, q, R_TREBLE, 0.5f);
    float level = role_val(d, q, R_LEVEL, 0.5f);

    float gdb = (float)d->g0 + (float)(d->g1 - d->g0) * dr;
    e->drv_gain = db2lin(gdb);

    /* pre-clip, base rate */
    float hp = (float)d->hp;
    if (t == PX_DRV_TIMMY) hp = 700.0f * powf(30.0f / 700.0f, role_val(d, q, R_BASS, 0.5f));
    if (t == PX_DRV_PRECISION) hp = 100.0f + role_val(d, q, R_TIGHT, 0.5f) * 600.0f;
    bq_hp1(&e->drv_hp, sr, hp);
    e->drv_pre = 0;
    if (d->shelf) {                                    /* TS-style: lows bypass the clipping gain */
        bq_hp1(&e->drv_shelf, sr, (float)d->shelf);
        e->drv_shelf_lo = 1.0f / e->drv_gain;
        e->drv_pre |= 1;
    }
    if (d->lp) { bq_lowpass(&e->drv_lp, sr, (float)d->lp, 0.7f); e->drv_pre |= 2; }
    if (d->mid) {
        float f = (float)d->mid, db = (float)d->mid_db;
        if (t == PX_DRV_ZEN) f = 900.0f * powf(3.3f, role_val(d, q, R_VOICE, 0.5f));
        bq_peak(&e->drv_mid, sr, f, (float)d->mid_q10 * 0.1f, db);
        e->drv_pre |= 4;
    }
    if (t == PX_DRV_PRECISION) {                       /* BRIGHT = pre-clip treble lift */
        bq_highshelf(&e->drv_mid, sr, 2000.0f, role_val(d, q, R_BRIGHT, 0.5f) * 9.0f);
        e->drv_pre |= 4;
    }

    /* clipper */
    e->drv_clip = d->clip;
    e->drv_bias = (float)d->bias * 0.01f;
    e->drv_gate = 0.0f;
    if (t == PX_DRV_FFACTORY) {
        e->drv_bias = 0.05f + role_val(d, q, R_BIAS, 0.5f) * 0.6f;
        e->drv_gate = role_val(d, q, R_GATE, 0.0f) * 0.25f;
    }
    e->drv_off = px_tanh(e->drv_bias);
    e->drv_dcb = (d->clip == CL_ASYM || d->clip == CL_FUZZ || d->clip == CL_OCTA);
    e->drv_dck = 2.0f * PI_F * 12.0f / osr;

    /* post tone, oversampled */
    e->drv_tm = d->tm; e->drv_np = 1;
    switch (d->tm) {
    case TM_MUFF:                                      /* LP / HP crossfade: the classic mid scoop */
        bq_lowpass(&e->drv_t1, osr, (float)d->t0, 0.6f);
        bq_highpass(&e->drv_t2, osr, (float)d->t1, 0.6f);
        e->drv_mix = tone;
        e->drv_np = 2;
        break;
    case TM_HM2:
        bq_peak(&e->drv_t1, osr, (float)d->t0, 0.9f, role_val(d, q, R_LOW, 0.5f) * 15.0f);
        bq_peak(&e->drv_t2, osr, (float)d->t1, 0.7f, role_val(d, q, R_HIGH, 0.5f) * 15.0f);
        e->drv_np = 2;
        break;
    case TM_MT2:
        bq_peak(&e->drv_t1, osr, (float)d->t0, 0.8f, (role_val(d, q, R_LOW, 0.5f) - 0.5f) * 24.0f);
        bq_highshelf(&e->drv_t2, osr, (float)d->t1, (role_val(d, q, R_HIGH, 0.5f) - 0.5f) * 24.0f);
        e->drv_np = 2;
        break;
    default:
        bq_lowpass(&e->drv_t1, osr, (float)d->t0 * powf((float)d->t1 / (float)d->t0, tone), 0.7f);
        break;
    }
    if (d->p2) {
        if (d->p2db == 0) bq_lowpass(&e->drv_t3, osr, (float)d->p2, 0.7f);
        else bq_peak(&e->drv_t3, osr, (float)d->p2, 0.8f, (float)d->p2db);
        e->drv_np |= 4;
    }

    float bl = (float)d->blend * 0.01f;
    if (t == PX_DRV_BASSDRV) bl = role_val(d, q, R_BLEND, 0.0f);
    else bl *= (1.0f - 0.8f * dr);                     /* Klon: clean path fades as gain rises */
    e->drv_blend = bl;
    e->drv_level = DRV_TRIM[t] * 2.0f * level;
}

static void rc_amp(px_engine *e)
{
    const px_preset_t *p = &e->cur;
    float sr = e->sr, osr = sr * PX_OVERSAMPLE;
    int t = p->type[PX_B_AMP] % PX_AMP_COUNT;
    const amp_v *v = &AV[t];
    const uint8_t *q = p->p[PX_B_AMP];
    float gain = f01(q[0]);
    float userg = 0.3f + gain * gain * 3.2f;
    e->amp_n = v->n; e->amp_hard = v->hard;
    for (int i = 0; i < v->n; i++) {
        e->amp_g[i] = v->g[i] * userg;
        e->amp_bias[i] = v->bias[i];
        e->amp_boff[i] = px_tanh(v->bias[i]);
        op_lp(&e->amp_lp[i], osr, (float)v->lp[i] * 0.85f);      /* tube stage: Miller-cap roll-off */
        op_hp(&e->amp_hp[i], osr, (float)v->hp[i]);      /* coupling cap */
    }
    bq_highpass(&e->amp_tight, sr, (float)v->tight, 0.7f);
    if (v->bright) bq_highshelf(&e->amp_bright, sr, (float)v->bright, (float)v->bright_db * (1.0f - gain));
    else bq_ident(&e->amp_bright);
    bq_lowshelf(&e->ts_low, sr, (float)v->bass, (f01(q[1]) - 0.5f) * 2.0f * (float)v->rb);
    bq_peak(&e->ts_mid, sr, (float)v->mid, (float)v->midq10 * 0.1f, (f01(q[2]) - 0.5f) * 2.0f * (float)v->rm + (float)v->scoop);
    bq_highshelf(&e->ts_high, sr, (float)v->treb, (f01(q[3]) - 0.5f) * 2.0f * (float)v->rt);
    bq_highshelf(&e->amp_pres, sr, (float)v->pres, f01(q[4]) * 7.0f - 1.0f);
    if (v->res_db) bq_lowshelf(&e->amp_res, sr, (float)v->res, (float)v->res_db); else bq_ident(&e->amp_res);
    if (v->vdb) bq_peak(&e->amp_voice, sr, (float)v->vf, 0.8f, (float)v->vdb); else bq_ident(&e->amp_voice);
    e->amp_sagk = (float)v->sag100 * 0.012f;
    e->amp_pw = (float)v->pw10 * 0.1f;
    e->amp_pwn = 1.0f / px_tanh(e->amp_pw);
    e->amp_out = AMP_TRIM[t] * 0.84f;
}

static void rc_cab(px_engine *e)
{
    const px_preset_t *p = &e->cur;
    float sr = e->sr;
    int t = p->type[PX_B_CAB] % PX_CAB_COUNT;
    const cab_v *c = &CV[t];
    const uint8_t *q = p->p[PX_B_CAB];
    if (t == PX_CAB_DI) { e->cab_n = 0; return; }
    px_bq f[8];
    int n = 0;
    float body = (f01(q[0]) - 0.5f) * 10.0f, bright = (f01(q[1]) - 0.5f) * 10.0f, mic = 0.65f + f01(q[2]) * 0.7f;
    for (int k = 0; k < 8; k++) f[k] = e->cab[k];
    bq_highpass(&f[n++], sr, (float)c->hp, (float)c->hpq10 * 0.1f);
    bq_peak(&f[n++], sr, (float)c->bump, 1.0f, (float)c->bump_db + body);
    if (c->d1) bq_peak(&f[n++], sr, (float)c->d1, 1.2f, (float)c->d1db);
    if (c->d2) bq_peak(&f[n++], sr, (float)c->d2, 1.5f, (float)c->d2db);
    if (c->p1) bq_peak(&f[n++], sr, (float)c->p1, 1.4f, (float)c->p1db + bright);
    if (c->p2) bq_peak(&f[n++], sr, (float)c->p2, 1.6f, (float)c->p2db + bright * 0.6f);
    bq_lowpass(&f[n++], sr, (float)c->lp * mic, 0.9f);
    bq_lowpass(&f[n++], sr, (float)c->lp * mic * 1.15f, 0.8f);
    px_bq g[8];
    float en = 0.0f;
    for (int k = 0; k < n; k++) { g[k] = f[k]; bq_reset(&g[k]); }
    for (int i = 0; i < 4096; i++) {                   /* energy normalisation: every cab plays at the same level */
        float x = (i == 0) ? 1.0f : 0.0f;
        for (int k = 0; k < n; k++) x = bq_run(&g[k], x);
        en += x * x;
    }
    e->cab_g = (en > 1e-12f) ? 1.0f / sqrtf(en * 2.0f) : 1.0f;
    if (e->cab_n != n) for (int k = 0; k < n; k++) bq_reset(&f[k]);
    for (int k = 0; k < n; k++) e->cab[k] = f[k];
    e->cab_n = n;
}

static void rc_eq(px_engine *e)
{
    const px_preset_t *p = &e->cur;
    float sr = e->sr;
    const uint8_t *q = p->p[PX_B_EQ];
    int t = p->type[PX_B_EQ] % PX_EQ_COUNT;
#define EQDB(k) ((f01(q[k]) - 0.5f) * 24.0f)
    if (t == PX_EQ_5BAND) {
        bq_lowshelf(&e->eq[0], sr, 100.0f, EQDB(0));
        bq_peak(&e->eq[1], sr, 400.0f, 1.0f, EQDB(1));
        bq_peak(&e->eq[2], sr, 1000.0f, 1.0f, EQDB(2));
        bq_peak(&e->eq[3], sr, 2500.0f, 1.0f, EQDB(3));
        bq_highshelf(&e->eq[4], sr, 6000.0f, EQDB(4));
        e->eq_n = 5;
    } else if (t == PX_EQ_GEQ) {                       /* the 5-band graphic of the Californian lead amps */
        bq_peak(&e->eq[0], sr, 80.0f, 1.4f, EQDB(0));
        bq_peak(&e->eq[1], sr, 240.0f, 1.4f, EQDB(1));
        bq_peak(&e->eq[2], sr, 750.0f, 1.4f, EQDB(2));
        bq_peak(&e->eq[3], sr, 2200.0f, 1.4f, EQDB(3));
        bq_peak(&e->eq[4], sr, 6600.0f, 1.4f, EQDB(4));
        e->eq_n = 5;
    } else {
        bq_lowshelf(&e->eq[0], sr, 100.0f, EQDB(0));
        bq_peak(&e->eq[1], sr, 150.0f * powf(33.0f, f01(q[1])), 0.4f + f01(q[3]) * 5.6f, EQDB(2));
        bq_highshelf(&e->eq[2], sr, 5000.0f, EQDB(4));
        e->eq_n = 3;
    }
#undef EQDB
}

static void rc_mod(px_engine *e)
{
    const px_preset_t *p = &e->cur;
    float sr = e->sr;
    const uint8_t *q = p->p[PX_B_MOD];
    int t = p->type[PX_B_MOD] % PX_MOD_COUNT;
    float r = f01(q[0]);
    e->mod_fb = 0.0f; e->mod_mix = 1.0f;
    switch (t) {
    case PX_MOD_CHORUS:                                /* bucket-brigade chorus: triangle LFO, dark wet path */
        e->mod_inc = (0.1f + r * r * 6.0f) / sr;
        e->mod_base = 0.006f * sr; e->mod_sweep = (0.0005f + 0.0055f * f01(q[1])) * sr;
        e->mod_mix = f01(q[2]);
        bq_lowpass(&e->mod_lp, sr, 6600.0f, 0.7f);
        break;
    case PX_MOD_DIMENSION: {
        static const float RATE[4] = {0.25f, 0.25f, 0.5f, 0.5f}, DEP[4] = {0.0005f, 0.0009f, 0.0007f, 0.0012f};
        int m = (int)(r * 3.99f);
        e->mod_inc = RATE[m] / sr; e->mod_base = 0.006f * sr; e->mod_sweep = DEP[m] * sr;
        e->mod_mix = f01(q[1]);
        bq_lowpass(&e->mod_lp, sr, 8000.0f, 0.7f);
    } break;
    case PX_MOD_FLANGER:
        e->mod_inc = (0.05f + r * r * 4.95f) / sr;
        e->mod_sweep = f01(q[1]) * 0.008f * sr;
        e->mod_base = (0.0003f + f01(q[2]) * 0.0047f) * sr;
        e->mod_fb = f01(q[3]) * 0.92f;
        e->mod_mix = f01(q[4]);
        break;
    case PX_MOD_PHASE90:                               /* 4 identical JFET stages, no feedback (script) */
        e->mod_inc = (0.1f + r * r * 7.9f) / sr;
        e->ph_lo = 60.0f; e->ph_hi = 300.0f + f01(q[1]) * 1700.0f;
        e->mod_fb = 0.0f;
        break;
    case PX_MOD_STONE:
        e->mod_inc = (0.05f + r * r * 7.0f) / sr;
        e->mod_fb = f01(q[1]) * 0.7f;
        e->ph_lo = 100.0f; e->ph_hi = 400.0f + f01(q[2]) * 3100.0f;
        break;
    case PX_MOD_UNIVIBE:                               /* 4 stages with staggered caps, light-bulb LFO */
        e->mod_inc = (0.5f + r * r * 11.5f) / sr;
        e->mod_depth = f01(q[1]);
        e->mod_mix = f01(q[2]);
        e->uv_k[0] = 1.0f; e->uv_k[1] = 0.068f; e->uv_k[2] = 31.9f; e->uv_k[3] = 3.19f;
        e->uv_up = onepole_k(sr, 0.006f); e->uv_dn = onepole_k(sr, 0.03f);
        break;
    case PX_MOD_OPTOTREM: {
        float hz = 1.0f + r * r * 9.0f;
        e->mod_inc = hz / sr; e->mod_depth = f01(q[1]);
        e->trem_att = onepole_k(sr, 0.005f);
        e->trem_rel = onepole_k(sr, 0.2f - (hz - 1.0f) * 0.0167f);
    } break;
    case PX_MOD_BIASTREM:
        e->mod_inc = (1.0f + r * r * 11.0f) / sr; e->mod_depth = f01(q[1]); e->mod_shape = f01(q[2]);
        break;
    case PX_MOD_HARMTREM:
        e->mod_inc = (1.0f + r * r * 9.0f) / sr; e->mod_depth = f01(q[1]);
        bq_lowpass(&e->mod_lp, sr, 300.0f + f01(q[2]) * 900.0f, 0.6f);
        break;
    case PX_MOD_ROTARY:
        e->rot_ht = (q[0] < 50) ? 0.83f : 6.7f;
        e->rot_dt = (q[0] < 50) ? 0.67f : 5.7f;
        if (e->rot_hs <= 0.0f) { e->rot_hs = e->rot_ht; e->rot_ds = e->rot_dt; }
        e->rot_hk = onepole_k(sr, 1.0f); e->rot_dk = onepole_k(sr, 4.0f);
        e->mod_depth = f01(q[1]); e->rot_bal = f01(q[2]);
        bq_lowpass(&e->mod_lp, sr, 800.0f, 0.7f);
        break;
    default:                                           /* vibrato: wet only */
        e->mod_inc = (2.0f + r * r * 12.0f) / sr;
        e->mod_base = 0.005f * sr; e->mod_sweep = f01(q[1]) * 0.004f * sr;
        break;
    }
}

static void rc_delay(px_engine *e)
{
    const px_preset_t *p = &e->cur;
    float sr = e->sr;
    const uint8_t *q = p->p[PX_B_DELAY];
    int t = p->type[PX_B_DELAY] % PX_DLY_COUNT;
    float len = dly_ms(t, q[0]) * 0.001f * sr;
    if (len > (float)(PX_DLY_N - 1200)) len = (float)(PX_DLY_N - 1200);
    if (e->dly_len < 2.0f || e->dly_type != t) { e->dly_len = len; e->rev_cnt = 0; e->rev_start = e->dly_w; }
    e->dly_type = t;
    e->dly_tgt = len;
    e->dly_fb = f01(q[1]) * 0.95f;
    e->dly_satn = 1.0f;
    e->dly_mix = f01(q[2]);
    e->dly_wow = 0.0f; e->dly_flut = 0.0f; e->dly_sat = 0.0f; e->dly_q = 0.0f; e->dly_mod = 0.0f;
    e->dly_inc = 0.7f / sr; e->dly_inc2 = 8.0f / sr;
    float tone = f01(q[3]);
    switch (t) {
    case PX_DLY_DIGITAL: case PX_DLY_DUAL:
        bq_lowpass(&e->dly_lp, sr, 3000.0f + tone * 13000.0f, 0.7f); bq_highpass(&e->dly_hp, sr, 40.0f, 0.7f); break;
    case PX_DLY_ANALOG:                                /* bucket brigade: dark repeats that saturate */
        bq_lowpass(&e->dly_lp, sr, 1500.0f + tone * 2000.0f, 0.7f); bq_highpass(&e->dly_hp, sr, 80.0f, 0.7f);
        e->dly_sat = 1.0f; e->dly_mod = 1.5f; break;
    case PX_DLY_MEMORY:                                /* analog with modulated repeats */
        bq_lowpass(&e->dly_lp, sr, 2800.0f, 0.7f); bq_highpass(&e->dly_hp, sr, 80.0f, 0.7f);
        e->dly_sat = 1.0f; e->dly_inc = (0.5f + f01(q[3]) * 4.5f) / sr; e->dly_mod = 2.0f + f01(q[4]) * 0.004f * sr; break;
    case PX_DLY_TAPE:                                  /* tape echo: wow 0.7 Hz + flutter 8 Hz */
        bq_lowpass(&e->dly_lp, sr, 4000.0f + tone * 4000.0f, 0.7f); bq_highpass(&e->dly_hp, sr, 120.0f, 0.7f);
        e->dly_sat = 2.0f; e->dly_wow = 1.0f + f01(q[4]) * (len * 0.004f + 6.0f); e->dly_flut = f01(q[4]) * 3.0f; break;
    case PX_DLY_SPACE: {                               /* 3 playback heads 1:2:3 */
        static const uint8_t HM[7] = {1, 2, 4, 3, 6, 5, 7};
        int h = HM[(int)(tone * 6.99f)], nh = (h & 1) + ((h >> 1) & 1) + ((h >> 2) & 1);
        e->dly_heads = h; e->dly_hn = 1.0f / sqrtf((float)nh); e->dly_hf = 1.0f / (float)nh;
        bq_lowpass(&e->dly_lp, sr, 4500.0f, 0.7f); bq_highpass(&e->dly_hp, sr, 150.0f, 0.7f);
        e->dly_sat = 2.0f; e->dly_wow = 1.0f + f01(q[4]) * (len * 0.003f + 5.0f); e->dly_flut = f01(q[4]) * 2.5f;
    } break;
    case PX_DLY_REVERSE:
        bq_lowpass(&e->dly_lp, sr, 9000.0f, 0.7f); bq_highpass(&e->dly_hp, sr, 60.0f, 0.7f);
        e->rev_xf = 1.0f / (0.01f * sr); break;
    default:                                           /* lo-fi: few bits, narrow band, wobbly */
        bq_lowpass(&e->dly_lp, sr, 4000.0f - tone * 2000.0f, 0.9f); bq_highpass(&e->dly_hp, sr, 300.0f, 0.7f);
        e->dly_q = powf(2.0f, 11.0f - tone * 6.0f); e->dly_wow = 4.0f + tone * 8.0f; e->dly_inc = 0.5f / sr; e->dly_sat = 1.5f; break;
    }
    if (e->dly_sat > 0.0f) {                           /* saturating loop: tops out at 1.08 = self-oscillation, like the real units */
        e->dly_sat = 1.0f + e->dly_sat * 0.5f;         /* drive into the loop clipper */
        e->dly_satn = 1.0f / e->dly_sat;
        float f = f01(q[1]);
        e->dly_fb = f < 0.85f ? f * 0.95f : 0.8075f + (f - 0.85f) * 1.817f;
    }
}

static void rc_reverb(px_engine *e)
{
    const px_preset_t *p = &e->cur;
    float sr = e->sr;
    const uint8_t *q = p->p[PX_B_REVERB];
    int t = p->type[PX_B_REVERB] % PX_REV_COUNT;
    float d = f01(q[0]), tone = f01(q[1]);
    float scale = 1.0f, rt = 2.0f, damp = 0.3f;
    const int *ap = RV_AP;
    e->rv_ag = 0.5f; e->rv_shim = 0.0f; e->rv_gate = 0; e->sp_on = 0;
    switch (t) {
    case PX_REV_ROOM:      scale = 0.5f;  rt = 0.3f + d * 0.9f;  damp = 0.2f + (1.0f - tone) * 0.5f; break;
    case PX_REV_HALL:      scale = 1.3f;  rt = 1.2f + d * 2.8f;  damp = 0.2f + (1.0f - tone) * 0.5f; break;
    case PX_REV_CATHEDRAL: scale = 1.7f;  rt = 3.0f + d * 7.0f;  damp = 0.3f + (1.0f - tone) * 0.45f; break;
    case PX_REV_PLATE:     scale = 0.85f; rt = 0.6f + d * 4.4f;  damp = 0.05f + (1.0f - tone) * 0.3f; e->rv_ag = 0.7f; break;
    case PX_REV_SPRING:    scale = 0.45f; rt = 1.5f + d * 2.5f;  damp = 0.35f + (1.0f - tone) * 0.35f; e->rv_ag = 0.65f;
        ap = RV_AP_SPRING; e->sp_on = 1; e->sp_a = 0.5f + f01(q[3]) * 0.25f; break;
    case PX_REV_SHIMMER:   scale = 1.3f;  rt = 2.0f + d * 6.0f;  damp = 0.15f + (1.0f - tone) * 0.4f;
        e->rv_shim = f01(q[3]) * 0.6f; break;
    case PX_REV_GATED:     scale = 0.9f;  rt = 2.5f;             damp = 0.15f + (1.0f - tone) * 0.4f;
        e->rv_gate = 1; e->rv_hold = (0.15f + d * 0.45f) * sr; e->rv_gate_rel = onepole_k(sr, 0.02f); break;
    default:               scale = 1.7f;  rt = 6.0f + d * 14.0f; damp = 0.45f + (1.0f - tone) * 0.4f; e->rv_ag = 0.6f; break;
    }
    float srs = sr / 44100.0f;
    for (int i = 0; i < 8; i++) {
        int l = (int)((float)RV_COMB[i] * scale * srs);
        if (l > PX_RV_C - 1) l = PX_RV_C - 1;
        if (l < 64) l = 64;
        e->rv_cl[i] = l;
        if (e->rv_cw[i] >= l) e->rv_cw[i] = 0;
        float g = expf(-6.9077553f * (float)l / (rt * sr));
        e->rv_g[i] = g > 0.985f ? 0.985f : g;
    }
    for (int i = 0; i < 4; i++) {
        int l = (int)((float)ap[i] * srs);
        if (l > PX_RV_A - 1) l = PX_RV_A - 1;
        e->rv_al[i] = l;
        if (e->rv_aw[i] >= l) e->rv_aw[i] = 0;
    }
    e->rv_damp = damp; e->rv_mix = f01(q[2]);
    if (t == PX_REV_SPRING) bq_lowpass(&e->rv_tone, sr, 2500.0f + tone * 2500.0f, 0.8f);
    else bq_lowpass(&e->rv_tone, sr, 3000.0f + tone * 9000.0f, 0.7f);
    bq_highpass(&e->rv_hp, sr, t == PX_REV_SPRING ? 120.0f : 50.0f, 0.7f);
    e->sh_k = onepole_k(sr, 1.0f / (2.0f * PI_F * 5000.0f));
}

static void px_recalc(px_engine *e)
{
    float sr = e->sr, osr = sr * PX_OVERSAMPLE;
    rc_dyn(e);
    rc_pitch(e);
    rc_sfx(e);
    bq_lowpass(&e->os_up, osr, sr * 0.47f, 0.54f);    /* 4th-order Butterworth anti-alias (up and down) */
    bq_lowpass(&e->os_up2, osr, sr * 0.47f, 1.31f);
    bq_lowpass(&e->os_dn, osr, sr * 0.47f, 0.54f);
    bq_lowpass(&e->os_dn2, osr, sr * 0.47f, 1.31f);
    rc_drive(e);
    rc_amp(e);
    rc_cab(e);
    rc_eq(e);
    rc_mod(e);
    rc_delay(e);
    rc_reverb(e);
}

void px_engine_init(px_engine *e, float sr)
{
    memset(e, 0, sizeof(*e));
    e->sr = sr;
    e->oct_ff = 1.0f; e->oct_ff2 = 1.0f;
    e->dly_type = -1;
    px_preset_t p; px_preset_factory(&p, 0);
    px_engine_set(e, &p);
}
void px_engine_set(px_engine *e, const px_preset_t *p)
{
    e->cur = *p;
    px_recalc(e);
}

/* ========================================================== processing ===== */
static inline float clip_run(const px_engine *e, float d)
{
    float a, y;
    switch (e->drv_clip) {
    case CL_SOFT: return px_tanh(d);
    case CL_ASYM: return px_tanh(d + e->drv_bias) - e->drv_off;
    case CL_HARD:
        a = px_abs(d); y = a < 0.7f ? a : 1.0f - 0.09f / (a - 0.4f);
        return d < 0.0f ? -y : y;
    case CL_GE: return d / (1.0f + px_abs(d));
    case CL_LED:
        a = px_abs(d); y = a >= 1.0f ? 1.0f : a * (1.5f - 0.5f * a * a);
        return d < 0.0f ? -y : y;
    case CL_MOS:
        a = px_abs(d); y = a < 0.7f ? a : 1.0f - 0.09f / (a - 0.4f);
        return 0.5f * (px_tanh(d) + (d < 0.0f ? -y : y));
    case CL_MUFF:                                      /* two cascaded clipping stages */
        d = px_tanh(d) * 3.0f;
        a = px_abs(d); y = a < 0.7f ? a : 1.0f - 0.09f / (a - 0.4f);
        return d < 0.0f ? -y : y;
    case CL_FUZZ:                                      /* starved transistor: asymmetric, optional gate */
        if (e->drv_gate > 0.0f) { a = px_abs(d); if (a < e->drv_gate) d *= a / e->drv_gate; }
        d += e->drv_bias;
        return d >= 0.0f ? px_tanh(d) : 0.7f * px_tanh(1.4f * d);
    case CL_OCTA:                                      /* full-wave rectifier: octave up, then fuzz */
        a = px_abs(d); y = a < 0.7f ? a : 1.0f - 0.09f / (a - 0.4f);
        return 0.7f * px_tanh(1.5f * a) + 0.3f * (d < 0.0f ? -y : y);
    default: return px_tanh(d * 0.4f) * 2.5f;          /* clean boost */
    }
}
static inline float drive_pre(px_engine *e, float x)   /* base rate */
{
    x = bq_run(&e->drv_hp, x);
    if (e->drv_pre & 2) x = bq_run(&e->drv_lp, x);
    if (e->drv_pre & 4) x = bq_run(&e->drv_mid, x);
    if (e->drv_pre & 1) x = x * e->drv_shelf_lo + bq_run(&e->drv_shelf, x);
    return x;
}
static inline float drive_run(px_engine *e, float x)   /* oversampled */
{
    float d = clip_run(e, x * e->drv_gain) + e->dn;
    if (e->drv_dcb) { e->drv_dcs += (d - e->drv_dcs) * e->drv_dck; d -= e->drv_dcs; }
    if (e->drv_tm == TM_MUFF) {
        float lo = bq_run(&e->drv_t1, d), hi = bq_run(&e->drv_t2, d);
        d = lo + (hi - lo) * e->drv_mix;
    } else {
        d = bq_run(&e->drv_t1, d);
        if (e->drv_np & 2) d = bq_run(&e->drv_t2, d);
    }
    if (e->drv_np & 4) d = bq_run(&e->drv_t3, d);
    return d * e->drv_level + x * e->drv_blend;
}
static inline float amp_stage(px_engine *e, int i, float x)
{
    x = op_run(&e->amp_hp[i], x);
    x = x * e->amp_g[i] + e->amp_bias[i];
    x = px_tanh(x) - e->amp_boff[i] + e->dn;       /* the bias swallows tiny signals: re-inject the offset */
    return op_run(&e->amp_lp[i], x);
}
static inline float rd_frac(const float *buf, int mask, float pos)   /* pos in [0, mask+1) */
{
    int i = (int)pos; float fr = pos - (float)i;
    return buf[i] + (buf[(i + 1) & mask] - buf[i]) * fr;
}
static inline float dly_read(const float *buf, float pos)
{
    int i = (int)pos; float fr = pos - (float)i;
    int j = i + 1; if (j >= PX_DLY_N) j = 0;
    return buf[i] + (buf[j] - buf[i]) * fr;
}
static inline float dly_tap(const px_engine *e, float d)
{
    float pos = (float)e->dly_w - d;
    if (pos < 0.0f) pos += (float)PX_DLY_N;
    return dly_read(e->dly_buf, pos);
}
static inline float mod_tap(const px_engine *e, float d)
{
    float pos = (float)e->mod_w - d;
    if (pos < 0.0f) pos += 2048.0f;
    return rd_frac(e->mod_buf, 2047, pos);
}
static inline float ps_voice(const float *buf, int w, float *ph, float inc, float W)
{                                                      /* plain 2-tap shifter (shimmer: smeared anyway) */
    float a = *ph + inc;
    a = wrap1(a);
    *ph = a;
    float b = a + 0.5f; if (b >= 1.0f) b -= 1.0f;
    float pa = (float)w - (3.0f + a * W); if (pa < 0.0f) pa += (float)PX_PS_N;
    float pb = (float)w - (3.0f + b * W); if (pb < 0.0f) pb += (float)PX_PS_N;
    float s = lfo_sin(a * 0.5f), g = s * s;            /* sin^2 / cos^2 crossfade: sums to 1 */
    return rd_frac(buf, PX_PS_N - 1, pa) * g + rd_frac(buf, PX_PS_N - 1, pb) * (1.0f - g);
}
#define PS_OFFMAX 512                                  /* one period of low E */
static float ps_align(const float *buf, int w, float d_ref, float d_new)
{                                                      /* SOLA: offset that puts the fading-in tap in phase */
    int r0 = w - (int)d_ref, best = 0;
    float bc = -1e30f;
    for (int pass = 0; pass < 2; pass++) {
        int lo = pass ? best - 3 : 0, hi = pass ? best + 3 : PS_OFFMAX, st = pass ? 1 : 4, b2 = best;
        if (lo < 0) lo = 0;
        for (int o = lo; o <= hi; o += st) {
            int c0 = w - (int)(d_new + (float)o);
            float acc = 0.0f, en = 1e-9f;
            for (int j = 0; j < 64; j += 2) {
                float x = buf[(r0 - j) & (PX_PS_N - 1)], y = buf[(c0 - j) & (PX_PS_N - 1)];
                acc += x * y; en += y * y;
            }
            acc = acc / sqrtf(en);                     /* normalised: loudness does not bias the choice */
            if (acc > bc) { bc = acc; b2 = o; }
        }
        best = b2;
    }
    return (float)best;
}
static inline float ps_voice_al(float *buf, int w, float *ph, float *off, float inc, float W)
{
    float a0 = *ph, a = wrap1(a0 + inc);
    *ph = a;
    float b = a + 0.5f; if (b >= 1.0f) b -= 1.0f;
    int wa = inc > 0.0f ? (a < a0) : (a > a0);                                  /* tap A restarts (silent) */
    int wb = inc > 0.0f ? (a0 < 0.5f && a >= 0.5f) : (a0 >= 0.5f && a < 0.5f);   /* tap B restarts (silent) */
    float da = 3.0f + a * W, db = 3.0f + b * W;
    if (wa) off[0] = ps_align(buf, w, db + off[1], da);
    if (wb) off[1] = ps_align(buf, w, da + off[0], db);
    float pa = (float)w - (da + off[0]); if (pa < 0.0f) pa += (float)PX_PS_N;
    float pb = (float)w - (db + off[1]); if (pb < 0.0f) pb += (float)PX_PS_N;
    float s = lfo_sin(a * 0.5f), g = s * s;
    return rd_frac(buf, PX_PS_N - 1, pa) * g + rd_frac(buf, PX_PS_N - 1, pb) * (1.0f - g);
}

void px_engine_process(px_engine *e, const float *in, float *out, int n)
{
    const px_preset_t *p = &e->cur;
    const float osg = (float)PX_OVERSAMPLE, sr = e->sr;
    const int cmp_t = p->type[PX_B_COMP] % PX_CMP_COUNT;
    const int pit_t = p->type[PX_B_PITCH] % PX_PIT_COUNT;
    const int sfx_t = p->type[PX_B_SFX] % PX_SFX_COUNT;
    const int mod_t = p->type[PX_B_MOD] % PX_MOD_COUNT;
    const int dly_t = p->type[PX_B_DELAY] % PX_DLY_COUNT;
    const int drv_on = p->on[PX_B_DRIVE], amp_on = p->on[PX_B_AMP], amp_os = amp_on && e->amp_n > 0;
    const int os_on = drv_on || amp_os;

    for (int k = 0; k < n; k++) {
        e->dn = (k & 1) ? 1e-15f : -1e-15f;            /* anti-denormal (-300 dB): digital silence never decays */
        float x = in[k] + e->dn;
        { float y = x - e->dc_x1 + 0.9995f * e->dc_y1; e->dc_x1 = x; e->dc_y1 = y; x = y; }

        /* ---- gate ---- */
        if (p->on[PX_B_GATE]) {
            float a = px_abs(x);
            e->gate_env = (a > e->gate_env) ? a : a + (e->gate_env - a) * 0.9995f;
            float tg = (e->gate_env > e->gate_thr) ? 1.0f : 0.0f;
            e->gate_g = (tg > e->gate_g) ? tg : tg + (e->gate_g - tg) * e->gate_rel;
            if (e->gate_g < 1e-6f) e->gate_g = 0.0f;   /* closed: exact zero, never a denormal */
            x *= e->gate_g;
        }
        x += e->dn;

        /* ---- compressor ---- */
        if (p->on[PX_B_COMP]) {
            float a = px_abs(x);
            float c = (a > e->cmp_env) ? e->cmp_att : e->cmp_rel;
            e->cmp_env = a + (e->cmp_env - a) * c;
            if (cmp_t == PX_CMP_SWELL) {
                e->cmp_slow += (e->cmp_env - e->cmp_slow) * e->cmp_slowk;
                if (e->cmp_env > 1.8f * e->cmp_slow + e->cmp_thr) e->cmp_sw = 0.0f;   /* new pick attack */
                else e->cmp_sw += (1.0f - e->cmp_sw) * e->cmp_rise;
                x *= e->cmp_sw * e->cmp_sw * e->cmp_make;
            } else {
                float y = x * e->cmp_make / (1.0f + e->cmp_k * e->cmp_env * 4.0f);
                x = x + (y - x) * e->cmp_mix;
            }
        }

        /* ---- pitch ---- */
        if (p->on[PX_B_PITCH]) {
            if (pit_t == PX_PIT_OCTAVER) {
                float b = bq_run(&e->oct_lp2, bq_run(&e->oct_lp, x));
                float a = px_abs(b);
                e->oct_env = a + (e->oct_env - a) * 0.995f;
                if (e->oct_prev <= 0.0f && b > 0.0f && e->oct_env > 0.002f) {
                    e->oct_ff = -e->oct_ff;
                    if (e->oct_ff > 0.0f) e->oct_ff2 = -e->oct_ff2;
                }
                e->oct_prev = b;
                float s1 = bq_run(&e->oct_sublp, e->oct_ff * e->oct_env);
                float s2 = bq_run(&e->oct_sublp2, e->oct_ff2 * e->oct_env);
                float u = px_abs(bq_run(&e->oct_bp, x));
                u = bq_run(&e->oct_uphp, u);
                x = x * e->oct_dry + s1 * e->oct_s1 + s2 * e->oct_s2 + u * e->oct_up;
            } else {
                e->ps_buf[e->ps_w] = x;
                if (pit_t == PX_PIT_WHAMMY) {
                    e->ps_r += (e->ps_rt - e->ps_r) * e->ps_glide;
                    e->ps_inc[0] = (1.0f - e->ps_r) * e->ps_invW;
                }
                float y = x * e->ps_dry;
                for (int v = 0; v < e->ps_nv; v++)
                    y += ps_voice_al(e->ps_buf, e->ps_w, &e->ps_ph[v], e->ps_off[v], e->ps_inc[v], e->ps_W[v]) * e->ps_lvl[v];
                if (++e->ps_w >= PX_PS_N) e->ps_w = 0;
                x = y;
            }
        }

        /* ---- special fx ---- */
        if (p->on[PX_B_SFX]) {
            switch (sfx_t) {
            case PX_SFX_AUTOWAH: case PX_SFX_LFOWAH: {
                float t;
                if (sfx_t == PX_SFX_AUTOWAH) {
                    float a = px_abs(x);
                    e->sfx_env += (a - e->sfx_env) * (a > e->sfx_env ? e->sfx_a : e->sfx_b);
                    t = e->sfx_env * e->sfx_c; if (t > 1.0f) t = 1.0f;
                    t = 250.0f + t * t * e->sfx_d;
                } else {
                    e->sfx_ph = wrap1(e->sfx_ph + e->sfx_inc);
                    t = e->sfx_d + e->sfx_c * 0.5f * lfo_sin(e->sfx_ph);
                    if (t < 0.0f) t = 0.0f;
                    if (t > 1.0f) t = 1.0f;
                    t = 350.0f + t * t * 1900.0f;
                }
                float f = 2.0f * PI_F * t / sr;
                float hp = x - e->sfx_lo - e->sfx_qd * e->sfx_bp;
                e->sfx_bp += f * hp; e->sfx_lo += f * e->sfx_bp;
                x = e->sfx_bp * 1.6f + e->sfx_lo * 0.15f;
            } break;
            case PX_SFX_RING: {
                e->sfx_ph2 = wrap1(e->sfx_ph2 + e->sfx_d);
                e->sfx_ph = wrap1(e->sfx_ph + e->sfx_inc * (1.0f + e->sfx_c * lfo_sin(e->sfx_ph2)));
                float c = sin_hq(e->sfx_ph);
                x = x * (1.0f - e->sfx_mix) + x * c * e->sfx_mix * 1.4f;
            } break;
            case PX_SFX_CRUSH:
                if (++e->crush_n >= e->crush_k) {
                    float v = x * e->crush_q;
                    e->crush_v = (float)(int)(v + (v >= 0.0f ? 0.5f : -0.5f)) / e->crush_q;
                    e->crush_n = 0;
                }
                x = x + (e->crush_v - x) * e->sfx_mix;
                break;
            case PX_SFX_SLICER: {
                e->sfx_ph = wrap1(e->sfx_ph + e->sfx_inc);
                int step = (int)(e->sfx_ph * 16.0f) & 15;
                float tg = ((e->sfx_pat >> (15 - step)) & 1) ? 1.0f : 1.0f - e->sfx_c;
                e->sfx_g += (tg - e->sfx_g) * e->sfx_a;
                if (e->sfx_g < 1e-6f) e->sfx_g = 0.0f;
                x *= e->sfx_g;
            } break;
            case PX_SFX_LOFI: {
                float y = bq_run(&e->sfx_f2, bq_run(&e->sfx_f1, x));
                y = px_tanh(y * e->sfx_c) * (1.5f / (1.0f + e->sfx_c * 0.2f));
                x = x + (y - x) * e->sfx_mix;
            } break;
            default: {                                 /* square synth */
                float a = px_abs(x);
                e->sfx_env += (a - e->sfx_env) * (a > e->sfx_env ? e->sfx_a : e->sfx_b);
                float b = bq_run(&e->sfx_f1, x), h = e->sfx_env * 0.1f;
                if (e->sfx_prev <= 0.0f && b > h) {
                    e->sfx_prev = 1.0f; e->sfx_ff = -e->sfx_ff;
                } else if (e->sfx_prev > 0.0f && b < -h) e->sfx_prev = -1.0f;
                float sq = e->sfx_prev * e->sfx_env, sub = e->sfx_ff * e->sfx_env;
                float s = sq + sub * e->sfx_d;
                float t = e->sfx_env * (2.0f + e->sfx_c * 30.0f); if (t > 1.0f) t = 1.0f;
                float f = 2.0f * PI_F * (120.0f + t * t * (300.0f + e->sfx_c * 3500.0f)) / sr;
                float hp = s - e->sfx_lo - e->sfx_qd * e->sfx_bp;
                e->sfx_bp += f * hp; e->sfx_lo += f * e->sfx_bp;
                x = x * (1.0f - e->sfx_mix) + e->sfx_lo * 2.0f * e->sfx_mix;
            } break;
            }
        }

        /* ---- drive + amp, oversampled ---- */
        if (os_on) {
            float acc = 0.0f;
            if (drv_on) x = drive_pre(e, x);
            if (amp_os) { x = bq_run(&e->amp_tight, x); x = bq_run(&e->amp_bright, x); }
            for (int o = 0; o < PX_OVERSAMPLE; o++) {
                float u = (o == 0) ? x * osg : 0.0f;
                u = bq_run(&e->os_up, u);
                u = bq_run(&e->os_up2, u);
                if (drv_on) u = drive_run(e, u);
                if (amp_os) for (int i = 0; i < e->amp_n; i++) u = amp_stage(e, i, u);
                u = bq_run(&e->os_dn, u);
                acc = bq_run(&e->os_dn2, u);
            }
            x = acc;
        }
        if (amp_on) {
            if (!amp_os) x = bq_run(&e->amp_tight, x);
            x = bq_run(&e->ts_low, x);
            x = bq_run(&e->ts_mid, x);
            x = bq_run(&e->ts_high, x);
            x = bq_run(&e->amp_voice, x);
            x = bq_run(&e->amp_res, x);
            x = bq_run(&e->amp_pres, x);
            float a = px_abs(x);                       /* power amp + supply sag */
            e->amp_sag += (a - e->amp_sag) * 0.002f;
            x = px_tanh(x * e->amp_pw) * e->amp_pwn / (1.0f + e->amp_sag * e->amp_sagk);
            x = x * e->amp_out + e->dn;
        }

        /* ---- cabinet ---- */
        if (p->on[PX_B_CAB] && e->cab_n) {
            for (int c = 0; c < e->cab_n; c++) x = bq_run(&e->cab[c], x);
            x *= e->cab_g;
        }

        /* ---- eq ---- */
        if (p->on[PX_B_EQ]) for (int c = 0; c < e->eq_n; c++) x = bq_run(&e->eq[c], x);

        /* ---- modulation ---- */
        if (p->on[PX_B_MOD]) {
            e->mod_ph = wrap1(e->mod_ph + e->mod_inc);
            float l = lfo_sin(e->mod_ph);
            switch (mod_t) {
            case PX_MOD_CHORUS: case PX_MOD_DIMENSION: {
                float tri = lfo_tri(e->mod_ph), wet;
                e->mod_buf[e->mod_w] = x;
                if (mod_t == PX_MOD_CHORUS) wet = mod_tap(e, e->mod_base + e->mod_sweep * (0.5f + 0.5f * tri));
                else wet = 0.5f * (mod_tap(e, e->mod_base + e->mod_sweep * tri) + mod_tap(e, e->mod_base - e->mod_sweep * tri));
                wet = bq_run(&e->mod_lp, wet);
                if (++e->mod_w >= 2048) e->mod_w = 0;
                x = x * (1.0f - 0.5f * e->mod_mix) + wet * 0.5f * e->mod_mix;
            } break;
            case PX_MOD_FLANGER: case PX_MOD_VIBRATO: {
                float d = e->mod_base + e->mod_sweep * (0.5f + 0.5f * (mod_t == PX_MOD_FLANGER ? lfo_tri(e->mod_ph) : l));
                float wet = mod_tap(e, d);
                e->mod_buf[e->mod_w] = x + wet * e->mod_fb;
                if (++e->mod_w >= 2048) e->mod_w = 0;
                x = (mod_t == PX_MOD_VIBRATO) ? wet : x * (1.0f - 0.5f * e->mod_mix) + wet * 0.5f * e->mod_mix;
            } break;
            case PX_MOD_PHASE90: case PX_MOD_STONE: {
                float u = 0.5f + 0.5f * l;
                float fc = e->ph_lo + (e->ph_hi - e->ph_lo) * u * u;
                float w = PI_F * fc / sr, a = (1.0f - w) / (1.0f + w);
                float s = x + e->ph_fb * e->mod_fb;
                for (int i = 0; i < 4; i++) { float y = -a * s + e->ph_z[i]; e->ph_z[i] = s + a * y; s = y; }
                e->ph_fb = s;
                x = 0.5f * (x + s);
            } break;
            case PX_MOD_UNIVIBE: {
                float tg = 0.5f + 0.5f * l;
                e->uv_lamp += (tg - e->uv_lamp) * (tg > e->uv_lamp ? e->uv_up : e->uv_dn);
                float fc0 = 60.0f + e->uv_lamp * e->mod_depth * 1200.0f, s = x;
                for (int i = 0; i < 4; i++) {
                    float fc = fc0 * e->uv_k[i]; if (fc > sr * 0.45f) fc = sr * 0.45f;
                    float w = PI_F * fc / sr, a = (1.0f - w) / (1.0f + w);
                    float y = -a * s + e->ph_z[i]; e->ph_z[i] = s + a * y; s = y;
                }
                x = x * (1.0f - e->mod_mix) + s * e->mod_mix;
            } break;
            case PX_MOD_OPTOTREM: {
                float tg = 1.0f - e->mod_depth * (0.5f + 0.5f * l);
                e->trem_g += (tg - e->trem_g) * (tg < e->trem_g ? e->trem_att : e->trem_rel);
                x *= e->trem_g;
            } break;
            case PX_MOD_BIASTREM: {
                float sq = l > 0.0f ? 1.0f : -1.0f, sh = l + (sq - l) * e->mod_shape;
                float g = 1.0f - e->mod_depth * (0.5f + 0.5f * sh);
                x = px_tanh(x * g * 1.2f) * 0.85f;
            } break;
            case PX_MOD_HARMTREM: {
                float lo = bq_run(&e->mod_lp, x), hi = x - lo;
                float m = 0.5f + 0.5f * l;
                x = lo * (1.0f - e->mod_depth * m) + hi * (1.0f - e->mod_depth * (1.0f - m));
                x *= 1.0f + 0.5f * e->mod_depth;
            } break;
            case PX_MOD_ROTARY: {
                e->rot_hs += (e->rot_ht - e->rot_hs) * e->rot_hk;
                e->rot_ds += (e->rot_dt - e->rot_ds) * e->rot_dk;
                e->rot_h = wrap1(e->rot_h + e->rot_hs / sr);
                e->rot_d = wrap1(e->rot_d + e->rot_ds / sr);
                float lo = bq_run(&e->mod_lp, x), hi = x - lo;
                e->mod_buf[e->mod_w] = hi;
                float hs = lfo_sin(e->rot_h);
                float horn = mod_tap(e, (0.001f + 0.00044f * e->mod_depth * hs) * sr);
                if (++e->mod_w >= 2048) e->mod_w = 0;
                float hc = lfo_sin(wrap1(e->rot_h + 0.25f));
                horn *= 1.0f - 0.35f * e->mod_depth * (0.5f + 0.5f * hc);
                lo *= 1.0f - 0.25f * e->mod_depth * (0.5f + 0.5f * lfo_sin(e->rot_d));
                x = lo * (1.5f - e->rot_bal) + horn * (0.5f + e->rot_bal);
            } break;
            default: break;
            }
        }

        /* ---- delay ---- */
        if (p->on[PX_B_DELAY]) {
            e->dly_len += (e->dly_tgt - e->dly_len) * 0.0004f;   /* time glides (no click) */
            float rd = e->dly_len, d, fsrc;
            if (e->dly_wow > 0.0f || e->dly_mod > 0.0f) {
                e->dly_ph = wrap1(e->dly_ph + e->dly_inc);
                rd += (e->dly_wow + e->dly_mod) * (1.0f + lfo_sin(e->dly_ph));
                if (e->dly_flut > 0.0f) { e->dly_ph2 = wrap1(e->dly_ph2 + e->dly_inc2); rd += e->dly_flut * (1.0f + lfo_sin(e->dly_ph2)); }
            }
            if (dly_t == PX_DLY_REVERSE) {
                int G = (int)e->dly_len;
                if (e->rev_cnt >= G) { e->rev_cnt = 0; e->rev_start = e->dly_w; }
                int rp = e->rev_start - 1 - e->rev_cnt; if (rp < 0) rp += PX_DLY_N;
                float c = (float)e->rev_cnt, wg = c * e->rev_xf, w2 = (float)(G - e->rev_cnt) * e->rev_xf;
                if (wg > w2) wg = w2;
                if (wg > 1.0f) wg = 1.0f;
                d = e->dly_buf[rp] * wg;
                e->rev_cnt++;
                fsrc = d;
            } else if (dly_t == PX_DLY_SPACE) {
                float s = 0.0f;
                if (e->dly_heads & 1) s += dly_tap(e, rd * 0.33333f);
                if (e->dly_heads & 2) s += dly_tap(e, rd * 0.66667f);
                if (e->dly_heads & 4) s += dly_tap(e, rd);
                d = s * e->dly_hn; fsrc = s * e->dly_hf;   /* loop gain stays <= FDBK whatever the heads */
            } else if (dly_t == PX_DLY_DUAL) {
                float a = dly_tap(e, rd), b = dly_tap(e, rd * 0.75f);
                d = 0.6f * (a + 0.8f * b); fsrc = a;
            } else {
                d = dly_tap(e, rd); fsrc = d;
            }
            if (e->dly_q > 0.0f) {
                float v = d * e->dly_q;
                d = (float)(int)(v + (v >= 0.0f ? 0.5f : -0.5f)) / e->dly_q; fsrc = d;
            }
            float fbk = bq_run(&e->dly_hp, bq_run(&e->dly_lp, fsrc)) * e->dly_fb;
            if (e->dly_sat > 0.0f) fbk = px_tanh(fbk * e->dly_sat) * e->dly_satn;   /* unity for small signals */
            e->dly_buf[e->dly_w] = x + fbk;
            if (++e->dly_w >= PX_DLY_N) e->dly_w = 0;
            x = x + d * e->dly_mix;
        }

        /* ---- reverb ---- */
        if (p->on[PX_B_REVERB]) {
            float inr = x * 0.25f + e->sh_out, acc = 0.0f;
            if (e->sp_on) {                            /* spring dispersion: chirpy all-pass chain */
                float a = e->sp_a;
                for (int i = 0; i < 6; i++) { float y = -a * inr + e->sp_z[i]; e->sp_z[i] = inr + a * y; inr = y; }
            }
            for (int i = 0; i < 8; i++) {
                float y = e->rv_c[i][e->rv_cw[i]];
                e->rv_cs[i] = y + (e->rv_cs[i] - y) * e->rv_damp;
                e->rv_c[i][e->rv_cw[i]] = inr + e->rv_cs[i] * e->rv_g[i];
                if (++e->rv_cw[i] >= e->rv_cl[i]) e->rv_cw[i] = 0;
                acc += y;
            }
            acc *= 0.2f;
            for (int i = 0; i < 4; i++) {
                float b = e->rv_a[i][e->rv_aw[i]];
                float o = b - e->rv_ag * acc;
                e->rv_a[i][e->rv_aw[i]] = acc + e->rv_ag * o;
                if (++e->rv_aw[i] >= e->rv_al[i]) e->rv_aw[i] = 0;
                acc = o;
            }
            acc = bq_run(&e->rv_hp, bq_run(&e->rv_tone, acc));
            if (e->rv_shim > 0.0f) {                   /* octave-up shifter in the feedback loop */
                e->sh_buf[e->sh_w] = acc;
                float s = ps_voice(e->sh_buf, e->sh_w, &e->sh_ph, -1.0f / 1024.0f, 1024.0f);
                if (++e->sh_w >= PX_PS_N) e->sh_w = 0;
                e->sh_lp += (s - e->sh_lp) * e->sh_k;
                e->sh_out = e->sh_lp * e->rv_shim;
            } else e->sh_out = 0.0f;
            if (e->rv_gate) {
                float a = px_abs(x);
                e->rv_genv = a > e->rv_genv ? a : a + (e->rv_genv - a) * 0.999f;
                if (e->rv_genv > 0.01f) e->rv_hold_n = (int)e->rv_hold;
                float tg = 0.0f;
                if (e->rv_hold_n > 0) { e->rv_hold_n--; tg = 1.0f; }
                e->rv_gg += (tg - e->rv_gg) * (tg > e->rv_gg ? 0.05f : e->rv_gate_rel);
                if (e->rv_gg < 1e-6f) e->rv_gg = 0.0f;
                acc *= e->rv_gg;
            }
            x = x * (1.0f - 0.4f * e->rv_mix) + acc * e->rv_mix * 1.4f;
        }

        { float y = x - e->odc_x1 + 0.9990f * e->odc_y1; e->odc_x1 = x; e->odc_y1 = y; x = y; }
        if (x > 1.2f) x = 1.2f; else if (x < -1.2f) x = -1.2f;
        out[k] = x;
    }
}
