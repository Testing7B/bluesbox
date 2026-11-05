/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — render one factory preset on a DI (tools/level_presets.py): preset N in.wav out.f32 [in gain] */
#include <stdio.h>
#include <stdlib.h>
#include "px_dsp.h"
static px_engine e;
int main(int c, char **v)
{
    if (c < 4) return 1;
    FILE *f = fopen(v[2], "rb");
    if (!f) return 1;
    fseek(f, 0, SEEK_END); long nb = ftell(f) - 44; fseek(f, 44, SEEK_SET);
    int n = (int)(nb / 2);
    short *b = malloc((size_t)nb);
    if (fread(b, 2, (size_t)n, f) != (size_t)n) return 1;
    fclose(f);
    float g = c > 4 ? (float)atof(v[4]) : 0.15f, *x = malloc(4u * (size_t)n), *y = malloc(4u * (size_t)n);
    for (int i = 0; i < n; i++) x[i] = (float)b[i] / 32768.0f * g;
    px_preset_t p;
    px_preset_factory(&p, atoi(v[1]));
    if (getenv("LEVEL")) p.p[PX_B_GATE][PX_PATCH_LEVEL] = (uint8_t)atoi(getenv("LEVEL"));
    px_engine_init(&e, 44100.0f); px_engine_set(&e, &p);
    n = n / 64 * 64;
    for (int i = 0; i < n; i += 64) px_engine_process(&e, x + i, y + i, 64);
    f = fopen(v[3], "wb"); fwrite(y, 4, (size_t)n, f); fclose(f);
    printf("%s\n", p.name);
    return 0;
}
