/* level calibration -> px_trim.h : every amp (amp+cab) to 0.16 RMS, every drive at noon to unity */
#include <stdio.h>
#include <math.h>
#include "px_dsp.h"
static px_engine e;
static float sig[44100*3];
static void mksig(void){ int pos=0; float f0s[3]={82.4f,110.0f,146.8f}; unsigned s=7;
  for(int n=0;n<3;n++){ int N=(int)(44100.0f/f0s[n]); float buf[600]; for(int i=0;i<N;i++){ s=s*1103515245u+12345u; buf[i]=((s>>8)&65535)/32768.0f-1.0f; }
    for(int r=0;r<3;r++) for(int i=0;i<N;i++) buf[i]=0.5f*(buf[i]+buf[(i+1)%N]);
    for(int i=0;i<44100;i++){ sig[pos++]=0.35f*buf[i%N]; int j=i%N; buf[j]=0.996f*0.5f*(buf[j]+buf[(j+1)%N]); } } }
static double rms_of(px_preset_t *p){ px_engine_init(&e,44100.0f); px_engine_set(&e,p); float out[64]; double acc=0; long n=0;
  for(int b=0;b<44100*3/64;b++){ px_engine_process(&e,sig+b*64,out,64); if(b>40) for(int i=0;i<64;i++){acc+=out[i]*out[i];n++;} }
  return sqrt(acc/n); }
#include "px_trim.h"
int main(int argc,char**argv){ mksig(); double in=0; for(int i=2560;i<44100*3;i++) in+=sig[i]*sig[i]; in=sqrt(in/(44100*3-2560));
  printf("input rms %.4f\n",in);
  double ra[PX_AMP_COUNT], rd[PX_DRV_COUNT];
  for(int a=0;a<PX_AMP_COUNT;a++){ px_preset_t p; px_preset_factory(&p,7); for(int z=0;z<PX_NBLK;z++) p.on[z]=(z==PX_B_AMP||z==PX_B_CAB);
    p.type[PX_B_AMP]=a; p.p[PX_B_AMP][0]=55; for(int k=1;k<5;k++) p.p[PX_B_AMP][k]=50; ra[a]=rms_of(&p);
    printf("AMP %-13s %.4f (%.1f dB)\n",px_type_name(PX_B_AMP,a),ra[a],20*log10(ra[a])); }
  for(int d=0;d<PX_DRV_COUNT;d++){ px_preset_t p; px_preset_factory(&p,7); for(int z=0;z<PX_NBLK;z++) p.on[z]=(z==PX_B_DRIVE);
    p.type[PX_B_DRIVE]=d; for(int k=0;k<5;k++) p.p[PX_B_DRIVE][k]=50; rd[d]=rms_of(&p);
    printf("DRV %-13s %.4f (%.1f dB)\n",px_type_name(PX_B_DRIVE,d),rd[d],20*log10(rd[d]/in)); }
  if(argc>1){ FILE*f=fopen(argv[1],"w");
    fprintf(f,"/* level matching, generated on PC by calib3.c with the pedal's libm (px_math.c):\n * amps (amp + cab, gain 55) to 0.16 RMS on a plucked string; drives (all knobs at noon) to unity. */\n");
    fprintf(f,"static const float AMP_TRIM[PX_AMP_COUNT] = {"); for(int a=0;a<PX_AMP_COUNT;a++) fprintf(f,"%s%s%.3ff",a?", ":"",a%8==0?"\n    ":"",AMP_TRIM[a]*0.16/ra[a]); fprintf(f,"};\n");
    fprintf(f,"static const float DRV_TRIM[PX_DRV_COUNT] = {"); for(int d=0;d<PX_DRV_COUNT;d++) fprintf(f,"%s%s%.3ff",d?", ":"",d%8==0?"\n    ":"",DRV_TRIM[d]*in*((d==PX_DRV_EPBOOST||d==PX_DRV_RANGEM||d==PX_DRV_PRECISION)?2.0:1.0)/rd[d]); fprintf(f,"};\n");
    fclose(f); }
}
