/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — engine/px_nam.c on the PC: nam_test weights.f32 in.f32 out.f32 [block]   (tests/test_nam.py)
 * weights.f32: the 1871 A2-Lite weights (float32, file order); in/out: float32 samples. */
#include <stdio.h>
#include <stdlib.h>
#include "px_nam.h"
static pxn_model m;
static int32_t ring[PXN_RING_BYTES / 4 + 4096];   /* + 16 KB: two pieces never pack perfectly (NAM_SPLIT=1) */
static float *rd(const char *p, long *n)
{
    FILE *f = fopen(p, "rb");
    if (!f) { perror(p); exit(1); }
    fseek(f, 0, SEEK_END); *n = ftell(f) / 4; fseek(f, 0, SEEK_SET);
    float *b = malloc(*n * 4);
    if (fread(b, 4, *n, f) != (size_t)*n) exit(1);
    fclose(f);
    return b;
}
int main(int argc, char **argv)
{
    long nw, n;
    float *w = rd(argv[1], &nw), *x = rd(argv[2], &n);
    int blk = argc > 4 ? atoi(argv[4]) : 64;
    if (nw != PXN_NW) { fprintf(stderr, "%ld weights, want %d\n", nw, PXN_NW); return 1; }
    /* the rings in one piece of exactly PXN_RING_BYTES, as on the pedal; NAM_SPLIT=1: two pieces (3/4 + the rest) */
    unsigned n0 = getenv("NAM_SPLIT") ? (PXN_RING_BYTES * 3 / 4 & ~3u) : PXN_RING_BYTES;
    int r = pxn_load(&m, w, ring, n0, ring + n0 / 4, getenv("NAM_SPLIT") ? PXN_RING_BYTES + 16384u - n0 : 0);
    if (r) { fprintf(stderr, "load %d\n", r); return 1; }
    float *y = malloc(n * 4);
    for (long i = 0; i < n; i += blk) pxn_process(&m, x + i, y + i, (int)(n - i < blk ? n - i : blk));
    FILE *o = fopen(argv[3], "wb");
    fwrite(y, 4, n, o);
    fclose(o);
    return 0;
}
