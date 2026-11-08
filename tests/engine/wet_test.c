/* v39: the wet lanes (px_wet_mode, px_wet_swap, px_wet_run), run the firmware's way: per 128-frame block, the lanes
 * swap, the chain runs (two calls of 64), then the lanes make their wet output (in the firmware: on the other core).
 *  1. a digital or dual delay through its lane: the same output as inline, bit for bit (the taps read the lag less far
 *     back), on every factory preset (its reverb off);
 *  2. each reverb type through its lane: its tail is the inline tail 256 frames later;
 *  3. the two-core pipeline (PRE / NAM / POST) with the lanes: the plain engine's output with the lanes, one block later,
 *     bit for bit, on every factory preset with a NAM amp;
 *  4. a lane stopped every time (out of time): the dry sound goes on, finite, and each cut is counted. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "px_dsp.h"
#define FR 128
#define NB 400
#define LAG (2 * FR)
static px_engine e;
static float sig[FR * NB], ref[FR * NB], got[FR * NB];

static void start(const px_preset_t *p)
{
    px_engine_init(&e, 44100.0f); px_engine_set(&e, p);
    e.dly_len = e.dly_tgt;                             /* (no glide from the factory preset's delay time: its glide is
                                                          heard lag frames later through the lane) */
}
static void run_inline(const px_preset_t *p, float *out)
{
    start(p);
    for (int b = 0; b < NB * 2; b++) px_engine_process(&e, sig + 64 * b, out + 64 * b, 64);
}
static void run_lanes(const px_preset_t *p, float *out, volatile const uint8_t *stop)
{
    start(p);
    px_wet_mode(&e, 1, LAG);
    for (int b = 0; b < NB; b++) {
        px_wet_swap(&e);
        for (int o = 0; o < FR; o += 64) px_engine_process(&e, sig + FR * b + o, out + FR * b + o, 64);
        px_wet_run(&e, 0, stop);
    }
}
static int first_diff(const float *a, const float *b, int n)
{
    for (int i = 0; i < n; i++) if (memcmp(a + i, b + i, sizeof(float))) return i;
    return -1;
}

int main(void)
{
    unsigned s = 7;
    for (int i = 0; i < FR * NB; i++) {                /* notes, then silence for the tails */
        s = s * 1103515245u + 12345u;
        const float t = (float)(i % 6000) / 44100.0f;
        sig[i] = i < 12000 ? 0.3f * sinf((float)i * 0.0279f) * expf(-t * 6.0f) + 0.01f * (((s >> 8) & 65535) / 32768.0f - 1.0f)
                                 : 0.0f;
    }
    int bad = 0;

    /* 1. the delay's lane: bit for bit */
    int n1 = 0;
    static const int DT[2] = {PX_DLY_DIGITAL, PX_DLY_DUAL};
    for (int f = 0; f < 24; f++) for (int d = 0; d < 2; d++) {
        px_preset_t p;
        px_preset_factory(&p, f);
        p.on[PX_B_REVERB] = 0;
        p.on[PX_B_DELAY] = 1; p.type[PX_B_DELAY] = (uint8_t)DT[d];
        p.p[PX_B_DELAY][0] = 20; p.p[PX_B_DELAY][1] = 45; p.p[PX_B_DELAY][2] = 50;
        run_inline(&p, ref);
        run_lanes(&p, got, 0);
        n1++;
        const int i = first_diff(ref, got, FR * NB);
        if (i >= 0) { bad++; printf("delay, preset %02d type %d: differs from frame %d (%g against %g)\n", f + 1, DT[d], i, ref[i], got[i]); }
    }
    printf("delay lane: %d runs, the same as inline bit for bit%s\n", n1, bad ? " (NOT)" : "");

    /* 2. the reverb's lane: the tail, 256 frames later */
    int bad2 = 0;
    for (int t = 0; t < PX_REV_COUNT; t++) {
        px_preset_t p;
        px_preset_factory(&p, 0);
        for (int b = 0; b < PX_NBLK; b++) p.on[b] = 0;
        p.on[PX_B_REVERB] = 1; p.type[PX_B_REVERB] = (uint8_t)t;
        p.p[PX_B_REVERB][0] = 70; p.p[PX_B_REVERB][1] = 50; p.p[PX_B_REVERB][2] = 60;
        run_inline(&p, ref);
        run_lanes(&p, got, 0);
        float err = 0.0f, pk = 0.0f;
        for (int i = 25000; i < FR * NB; i++) {        /* (the dry part has decayed to ~1e-7 there) */
            const float a = got[i], b = ref[i - LAG], d = fabsf(a - b);
            if (d > err) err = d;
            if (fabsf(b) > pk) pk = fabsf(b);
        }
        const int ok = pk > 1e-4f && err <= 1e-5f * pk + 1e-9f;
        if (!ok) { bad2++; printf("reverb type %d: tail differs by %g (peak %g)\n", t, err, pk); }
    }
    printf("reverb lane: %d types, the tail %d frames later%s\n", PX_REV_COUNT, LAG, bad2 ? " (NOT)" : "");
    bad += bad2;

    /* 3. the pipeline with the lanes: the plain run with the lanes, one block later */
    int n3 = 0, bad3 = 0;
    for (int f = 0; f < 24; f++) {
        px_preset_t p;
        px_preset_factory(&p, f);
        if (!p.on[PX_B_AMP] || p.type[PX_B_AMP] != PX_AMP_NAM) continue;
        run_lanes(&p, ref, 0);
        start(&p);
        px_wet_mode(&e, 1, LAG);
        static float nb[2][FR];
        int cur = 0;
        memset(got, 0, sizeof got);
        for (int b = 0; b < NB; b++) {
            px_wet_swap(&e);
            px_wet_run(&e, 0, 0);                      /* (as the firmware: first in POST's share, post_job) */
            if (b) for (int o = 0; o < FR; o += 64) px_engine_part(&e, nb[cur ^ 1] + o, got + FR * (b - 1) + o, 64, PX_PART_POST);
            for (int o = 0; o < FR; o += 64) px_engine_part(&e, sig + FR * b + o, nb[cur] + o, 64, PX_PART_PRE);
            for (int o = 0; o < FR; o += 64) px_engine_part(&e, nb[cur] + o, nb[cur] + o, 64, PX_PART_NAM);
            cur ^= 1;
        }
        n3++;
        const int i = first_diff(ref, got, FR * (NB - 1));
        if (i >= 0) { bad3++; printf("pipeline, preset %02d: differs from frame %d (%g against %g)\n", f + 1, i, ref[i], got[i]); }
    }
    printf("pipeline with the lanes: %d NAM presets, %d differ\n", n3, bad3);
    bad += bad3;

    /* 4. out of time every block: the dry sound only, counted */
    {
        px_preset_t p;
        px_preset_factory(&p, 3);
        p.on[PX_B_DELAY] = 1; p.on[PX_B_REVERB] = 1;
        static const uint8_t stop = 1;
        run_lanes(&p, got, &stop);
        int fin = 1;
        float pk = 0.0f;
        for (int i = 0; i < FR * NB; i++) { if (!(fabsf(got[i]) < 10.0f)) fin = 0; if (fabsf(got[i]) > pk) pk = fabsf(got[i]); }
        const int ok = fin && pk > 1e-3f && e.wl_late >= (uint32_t)(NB - 2);
        if (!ok) bad++;
        printf("lanes stopped every block: output %s (peak %.3f), %u cuts counted%s\n", fin ? "finite" : "NOT finite", pk,
               (unsigned)e.wl_late, ok ? "" : " (NOT as expected)");
    }

    printf("wet lanes: %s\n", bad ? "FAIL" : "ok");
    return bad != 0;
}
