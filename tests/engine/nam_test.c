/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — engine/px_nam.c on the PC: nam_test weights.f32 in.f32 out.f32 [block]   (tests/test_nam.py)
 * weights.f32: the 1871 A2-Lite weights (float32, file order); in/out: float32 samples.
 * NAM_GATHER=1: the CPU's layers through gathered X rows instead of rdot.
 * NAM_PIPE=m: mode m (2..8: the vector unit beside the CPU, a second core) with a simulated unit and core (built with
 * -DPXN_EMUVEC).
 * NAM_SRV=1|2: the two-core pipeline's mode (CPU1 serving, the last frames of each block, CPU0 given 0..12 more frames
 * as CPU1's lateness cycles), CPU1 simulated running each layer as soon as allowed (1) or only when waited for (2). */
#include <stdio.h>
#include <stdlib.h>
#include "px_nam.h"
static pxn_model m;
void pxn_emu_on(int mode);
void pxn_emu_helper(void);
void pxn_emu_gather(void);
void pxn_emu_srv(int eager);
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
    if (getenv("NAM_GATHER")) pxn_emu_gather();
    if (getenv("NAM_PIPE")) { pxn_emu_on(atoi(getenv("NAM_PIPE"))); pxn_emu_helper(); }
    const int srv = getenv("NAM_SRV") ? atoi(getenv("NAM_SRV")) : 0;
    if (srv) { pxn_emu_on(5); pxn_emu_helper(); pxn_emu_srv(srv == 1); }
    unsigned n0 = getenv("NAM_SPLIT") ? (PXN_RING_BYTES * 3 / 4 & ~3u) : PXN_RING_BYTES;
    int r = pxn_load(&m, w, ring, n0, ring + n0 / 4, getenv("NAM_SPLIT") ? PXN_RING_BYTES + 16384u - n0 : 0);
    if (r) { fprintf(stderr, "load %d\n", r); return 1; }
    float *y = malloc(n * 4);
    for (long i = 0, k = 0; i < n; i += blk, k++) {
        if (srv) pxn_srv_begin((int32_t)(k % 9) * 8);    /* lateness: 0 .. 64 frames x 2 */
        pxn_process(&m, x + i, y + i, (int)(n - i < blk ? n - i : blk));
        if (srv) pxn_srv_end();
    }
    FILE *o = fopen(argv[3], "wb");
    fwrite(y, 4, n, o);
    fclose(o);
    return 0;
}
