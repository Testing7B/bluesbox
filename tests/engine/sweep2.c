/* sweeps every block x type x setting (0/50/100) with the pedal's libm; changes settings while playing
 * (like the UI); detects NaN/Inf/blow-up */
#include <stdio.h>
#include <string.h>
#include "px_dsp.h"
static px_engine e;
static long bad;
static int run(int blocks, float amp){
  float in[64], out[64]; int nan = 0; static unsigned seed = 1;
  for (int b = 0; b < blocks; b++) {
    for (int i = 0; i < 64; i++) { seed = seed*1103515245u+12345u; float nz = ((seed>>9)&1023)/1024.0f - 0.5f;
      in[i] = amp * (((b*64+i) % 180 < 90) ? 0.5f : -0.5f) + amp*0.2f*nz; }
    px_engine_process(&e, in, out, 64);
    for (int i = 0; i < 64; i++) if (out[i] != out[i] || out[i] > 50 || out[i] < -50) nan = 1;
  }
  return nan;
}
int main(void){
  px_engine_init(&e, 44100.0f);
  static const int V[3] = {0, 50, 100}; long n = 0;
  for (int blk = 0; blk < PX_NBLK; blk++)
   for (int t = 0; t < px_ntypes(blk); t++)
    for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) for (int c = 0; c < 3; c++) {
      px_preset_t p; px_preset_factory(&p, (blk*7+t) % px_preset_count());
      for (int z = 0; z < PX_NBLK; z++) p.on[z] = (z == PX_B_AMP || z == PX_B_CAB || z == blk);
      p.type[blk] = (uint8_t)t;
      p.p[blk][0] = V[a]; p.p[blk][1] = V[b]; p.p[blk][2] = V[c]; p.p[blk][3] = V[(a+c)%3]; p.p[blk][4] = V[(b+c)%3];
      p.p[PX_B_AMP][0] = V[(a+b)%3];
      px_engine_set(&e, &p); n++;
      if (run(30, 1.0f) | run(10, 0.0f) | run(10, 3.0f)) {   /* signal, silence, very loud */
        bad++; if (bad < 15) printf("PROBLEM: %s / %s  p=%d %d %d\n", px_block_name(blk), px_type_name(blk,t), V[a],V[b],V[c]);
        px_engine_init(&e, 44100.0f);
      }
    }
  /* all blocks together, every preset, extreme settings */
  for (int pr = 0; pr < px_preset_count(); pr++) for (int x = 0; x < 3; x++) {
      px_preset_t p; px_preset_factory(&p, pr);
      for (int z = 0; z < PX_NBLK; z++) { p.on[z] = 1; for (int k = 0; k < 5; k++) p.p[z][k] = V[(x+k+z)%3]; }
      px_engine_set(&e, &p); n++;
      if (run(40, 1.0f) | run(20, 0.0f)) { bad++; printf("PROBLEM all ON preset %d x%d\n", pr, x); px_engine_init(&e,44100.0f); }
  }
  /* block order (v29): random orders, all blocks on, every preset; invalid orders fall back to the default */
  { unsigned r = 12345u;
    for (int pr = 0; pr < px_preset_count(); pr++) for (int x = 0; x < 4; x++) {
      px_preset_t p; px_preset_factory(&p, pr);
      for (int z = 0; z < PX_NBLK; z++) p.on[z] = 1;
      for (int i = PX_NBLK - 1; i > 0; i--) { r = r * 1103515245u + 12345u; int j = (int)((r >> 8) % (unsigned)(i + 1));
        uint8_t t = p.order[i]; p.order[i] = p.order[j]; p.order[j] = t; }
      if (x == 3) p.order[0] = p.order[1];        /* not a permutation */
      px_engine_set(&e, &p); n++;
      if (run(40, 1.0f) | run(20, 0.0f) | run(10, 3.0f)) { bad++; printf("PROBLEM order preset %d x%d\n", pr, x); px_engine_init(&e,44100.0f); }
    }
  }
  printf("%ld configurations tested, %ld problems\n", n, bad);
  return bad != 0;
}
