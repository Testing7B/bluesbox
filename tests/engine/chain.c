/* SPDX-License-Identifier: GPL-3.0-only
 * OpenPocket — render one chain for tools/calib_levels.py.  chain in.wav out.f32 BLK TYPE p0 p1 p2 p3 p4 [BLK TYPE p0..p4]... : everything off except the blocks given (on) */
#include <stdio.h>
#include <stdlib.h>
#include "px_dsp.h"
static px_engine e;
int main(int c,char**v){ FILE*f=fopen(v[1],"rb"); fseek(f,0,SEEK_END); long nb=ftell(f)-44; fseek(f,44,SEEK_SET); int n=nb/2; short*b=malloc(nb); int g=fread(b,2,n,f); (void)g; fclose(f);
 float*x=malloc(4*n),*y=malloc(4*n); float gain=getenv("IN")?atof(getenv("IN")):0.15f; for(int i=0;i<n;i++) x[i]=b[i]/32768.0f*gain;
 px_preset_t p; px_preset_factory(&p,0); for(int z=0;z<PX_NBLK;z++) p.on[z]=0;
 for(int a=3;a+6<c;a+=7){ int bl=atoi(v[a]); p.on[bl]=1; p.type[bl]=atoi(v[a+1]); for(int k=0;k<5;k++) p.p[bl][k]=atoi(v[a+2+k]); }
 px_engine_init(&e,44100.0f); px_engine_set(&e,&p); n=n/64*64; for(int i=0;i<n;i+=64) px_engine_process(&e,x+i,y+i,64);
 FILE*o=fopen(v[2],"wb"); fwrite(y,4,n,o); fclose(o); }
