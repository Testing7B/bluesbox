/* Run only with -DPX_BLUES_LAB. Compare matched low-level probes after
 * short and sustained loud signals, using the real compressor block. */
#include <math.h>
#include <stdio.h>
#include "px_dsp.h"

static px_engine engine;

static int restored_drives_have_gain(void)
{
    px_preset_t preset;
    px_preset_factory(&preset, 0);
    for (int b = 0; b < PX_NBLK; b++) preset.on[b] = b == PX_B_DRIVE;
    preset.p[PX_B_DRIVE][0] = 50;
    preset.p[PX_B_DRIVE][1] = 50;
    preset.p[PX_B_DRIVE][2] = 50;
    preset.p[PX_B_DRIVE][3] = 50;
    for (int type = PX_DRV_V33; type < PX_DRV_BLUES_808; type++) {
        preset.type[PX_B_DRIVE] = (uint8_t)type;
        px_engine_init(&engine, 44100);
        px_engine_set(&engine, &preset);
        if (!(engine.drv_level > 0.0f)) {
            fprintf(stderr, "%s has no wet gain\n", px_type_name(PX_B_DRIVE, type));
            return 0;
        }
    }
    puts("All 27 restored legacy drives have nonzero wet gain.");
    return 1;
}

static void recovery(int type, int burst_frames, float *gain)
{
    px_preset_t preset;
    px_preset_factory(&preset, 0);
    for (int b = 0; b < PX_NBLK; b++) preset.on[b] = b == PX_B_COMP;
    preset.p[PX_B_GATE][PX_PATCH_LEVEL] = 50;
    preset.type[PX_B_COMP] = (uint8_t)type;
    preset.p[PX_B_COMP][0] = 60;
    preset.p[PX_B_COMP][1] = 50;
    preset.p[PX_B_COMP][2] = 100;
    px_engine_init(&engine, 44100);
    px_engine_set(&engine, &preset);

    /* 441 Hz puts each 100-sample cycle at an exact sample boundary. */
    const int start = 44800;
    const int windows[] = {512, 4416, 26464, 88256, 220544};
    float in[64], out[64], dry[5] = {0}, wet[5] = {0};
    for (int frame = 0; frame < start + burst_frames + 220800; frame += 64) {
        const int after = frame - start - burst_frames;
        const float level = frame >= start && after < 0 ? 0.42f : 0.015f;
        for (int i = 0; i < 64; i++) in[i] = level * sinf((float)((frame + i) % 100) * 0.062831853f);
        px_engine_process(&engine, in, out, 64);
        for (int w = 0; w < 5; w++) {
            if (after < windows[w] || after >= windows[w] + 256) continue;
            for (int i = 0; i < 64; i++) {
                dry[w] += in[i] * in[i];
                wet[w] += out[i] * out[i];
            }
        }
    }
    for (int w = 0; w < 5; w++) gain[w] = 10.0f * log10f(wet[w] / dry[w]);
}

int main(void)
{
    if (!restored_drives_have_gain()) return 1;
    float old_short[5], old_long[5], t4_short[5], t4_long[5];
    recovery(PX_CMP_OPTO, 4800, old_short);
    recovery(PX_CMP_OPTO, 176000, old_long);
    recovery(PX_CMP_OPTO_T4, 4800, t4_short);
    recovery(PX_CMP_OPTO_T4, 176000, t4_long);
    const char *when[] = {"12 ms", "100 ms", "600 ms", "2 s", "5 s"};
    puts("Probe gain in dB: legacy short/long, T4 short/long");
    for (int w = 0; w < 5; w++)
        printf("%6s %7.2f %7.2f %7.2f %7.2f\n", when[w], old_short[w], old_long[w], t4_short[w], t4_long[w]);
    /* A short hit should open sooner with T4, while a long one leaves
     * extra attenuation after two seconds and then approaches the same level. */
    if (!(t4_short[1] > old_short[1] + 1.0f &&
          t4_short[2] > t4_long[2] + 1.0f &&
          t4_long[3] < old_long[3] - 0.5f &&
          fabsf(t4_long[4] - old_long[4]) < 0.7f)) {
        fputs("Opto recovery does not exhibit the intended short/long response\n", stderr);
        return 1;
    }
    return 0;
}
