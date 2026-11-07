/* v37: the two-core pipeline (firmware/px_audio.h, dsp_pipe) against the plain engine: per 128-frame block, the blocks
 * after the amp on the previous block's network output (POST), then the blocks before it (PRE) and the network (NAM) on
 * this block. Every factory preset with a NAM amp: the same output bit for bit, one block (128 frames) later. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "px_dsp.h"
#define FR 128
#define NB 120
static px_engine e;
static float sig[FR * NB], ref[FR * NB], got[FR * NB];
int main(void)
{
    unsigned s = 7;
    for (int i = 0; i < FR * NB; i++) {
        s = s * 1103515245u + 12345u;
        sig[i] = 0.3f * sinf((float)i * 0.0157f) * (i % 9000 < 5000) + 0.02f * (((s >> 8) & 65535) / 32768.0f - 1.0f);
    }
    int bad = 0, n = 0;
    for (int f = 0; f < 24; f++) {
        px_preset_t p;
        px_preset_factory(&p, f);
        if (!p.on[PX_B_AMP] || p.type[PX_B_AMP] != PX_AMP_NAM) continue;
        px_engine_init(&e, 44100.0f); px_engine_set(&e, &p);
        for (int b = 0; b < NB * 2; b++) px_engine_process(&e, sig + 64 * b, ref + 64 * b, 64);
        px_engine_init(&e, 44100.0f); px_engine_set(&e, &p);
        static float nb[2][FR];
        int cur = 0;
        memset(got, 0, sizeof got);
        for (int b = 0; b < NB; b++) {
            if (b) for (int o = 0; o < FR; o += 64) px_engine_part(&e, nb[cur ^ 1] + o, got + FR * (b - 1) + o, 64, PX_PART_POST);
            for (int o = 0; o < FR; o += 64) px_engine_part(&e, sig + FR * b + o, nb[cur] + o, 64, PX_PART_PRE);
            for (int o = 0; o < FR; o += 64) px_engine_part(&e, nb[cur] + o, nb[cur] + o, 64, PX_PART_NAM);
            cur ^= 1;
        }
        n++;
        if (memcmp(ref, got, sizeof(float) * FR * (NB - 1))) {
            bad++;
            int i = 0;
            while (ref[i] == got[i]) i++;
            printf("preset %02d: differs from frame %d (%g against %g)\n", f + 1, i, ref[i], got[i]);
        }
    }
    printf("pipeline: %d NAM presets, %d differ\n", n, bad);
    return bad != 0;
}
