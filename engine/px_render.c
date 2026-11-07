/* px_render.c — runs a guitar WAV (DI mono/stereo) through the OpenPocket engine
 * and writes the processed WAV. Lets you HEAR your actual tones on your own guitar.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 *   px_render in.wav out.wav [preset 1..52] [amp 1..8] [gain 0..100]   (list: docs/SOUNDS-AND-CONTROLS.md)
 *
 * Host build: cc -O2 px_render.c px_dsp.c -lm -o px_render
 */
#include "px_dsp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* --- minimal WAV (PCM16 or float32, mono/stereo) --- */
static int rd_u32(FILE *f, uint32_t *v){ uint8_t b[4]; if(fread(b,1,4,f)!=4) return 0; *v=b[0]|b[1]<<8|b[2]<<16|(uint32_t)b[3]<<24; return 1;}
static int rd_u16(FILE *f, uint16_t *v){ uint8_t b[2]; if(fread(b,1,2,f)!=2) return 0; *v=b[0]|b[1]<<8; return 1;}
static void wr_u32(FILE *f,uint32_t v){ uint8_t b[4]={v,v>>8,v>>16,v>>24}; fwrite(b,1,4,f);}
static void wr_u16(FILE *f,uint16_t v){ uint8_t b[2]={v,v>>8}; fwrite(b,1,2,f);}

static float *load_wav(const char *fn, int *n, float *sr)
{
    FILE *f = fopen(fn, "rb"); if(!f){ fprintf(stderr,"cannot open %s\n",fn); return 0; }
    char id[4]; uint32_t sz; fread(id,1,4,f); rd_u32(f,&sz); fread(id,1,4,f); /* RIFF....WAVE */
    uint16_t fmt=1, ch=1, bits=16; uint32_t rate=48000, datasz=0; long datapos=0;
    for(;;){ char ck[4]; uint32_t cs; if(fread(ck,1,4,f)!=4) break; if(!rd_u32(f,&cs)) break;
        if(!memcmp(ck,"fmt ",4)){ rd_u16(f,&fmt); rd_u16(f,&ch); rd_u32(f,&rate);
            uint32_t br; rd_u32(f,&br); uint16_t ba; rd_u16(f,&ba); rd_u16(f,&bits);
            if(cs>16) fseek(f,cs-16,SEEK_CUR);
        } else if(!memcmp(ck,"data",4)){ datasz=cs; datapos=ftell(f); fseek(f,cs,SEEK_CUR);
        } else fseek(f,(cs+1)&~1u,SEEK_CUR);
    }
    if(!datapos){ fclose(f); return 0; }
    int bytes = bits/8; int frames = datasz/(bytes*ch);
    float *buf = malloc(sizeof(float)*frames); fseek(f,datapos,SEEK_SET);
    for(int i=0;i<frames;i++){ double acc=0;
        for(int c=0;c<ch;c++){ double s=0;
            if(fmt==3 && bits==32){ float v; fread(&v,4,1,f); s=v; }
            else if(bits==16){ int16_t v; fread(&v,2,1,f); s=v/32768.0; }
            else if(bits==24){ uint8_t b[3]; fread(b,1,3,f); int32_t v=(b[0]|b[1]<<8|b[2]<<16); if(v&0x800000) v|=~0xFFFFFF; s=v/8388608.0; }
            else if(bits==32){ int32_t v; fread(&v,4,1,f); s=v/2147483648.0; }
            acc+=s;
        } buf[i]=(float)(acc/ch);
    }
    fclose(f); *n=frames; *sr=(float)rate; return buf;
}

static void save_wav(const char *fn, const float *buf, int n, float sr)
{
    FILE *f=fopen(fn,"wb"); uint32_t datasz=n*2;
    fwrite("RIFF",1,4,f); wr_u32(f,36+datasz); fwrite("WAVE",1,4,f);
    fwrite("fmt ",1,4,f); wr_u32(f,16); wr_u16(f,1); wr_u16(f,1);
    wr_u32(f,(uint32_t)sr); wr_u32(f,(uint32_t)sr*2); wr_u16(f,2); wr_u16(f,16);
    fwrite("data",1,4,f); wr_u32(f,datasz);
    for(int i=0;i<n;i++){ float v=buf[i]; if(v>1)v=1; if(v<-1)v=-1; wr_u16(f,(int16_t)(v*32767)); }
    fclose(f);
}

int main(int argc, char **argv)
{
    if(argc<3){ fprintf(stderr,"usage: %s in.wav out.wav [preset 1..%d] [amp 1..%d] [gain 0..100]\n",argv[0],px_preset_count(),PX_AMP_COUNT); return 1; }
    int npreset = argc>3 ? atoi(argv[3]) - 1 : 0;
    if (getenv("PX_FX_INT")) px_fx_int = 1;            /* the cabinet and drive filters in integers (slower on the pedal) */
    int n; float sr;
    float *in = load_wav(argv[1], &n, &sr);
    if(!in){ fprintf(stderr,"unreadable WAV\n"); return 1; }

    static px_engine e; px_engine_init(&e, sr);
    px_preset_t p; px_preset_factory(&p, npreset);
    if(argc>4){ p.type[PX_B_AMP] = (uint8_t)((atoi(argv[4]) - 1 + PX_AMP_COUNT) % PX_AMP_COUNT); p.on[PX_B_AMP] = 1; }
    if(argc>5) p.p[PX_B_AMP][0] = (uint8_t)atoi(argv[5]);
    px_engine_set(&e, &p);

    float *out = malloc(sizeof(float)*n);
    for(int i=0;i<n;i+=PX_MAX_BLOCK){ int b=n-i; if(b>PX_MAX_BLOCK) b=PX_MAX_BLOCK;
        px_engine_process(&e, in+i, out+i, b); }
    save_wav(argv[2], out, n, sr);
    printf("OK  %s -> %s  (%d samples @ %.0f Hz)  preset '%s'  amp '%s'  cab '%s'\n", argv[1], argv[2], n, sr, p.name,
        px_type_name(PX_B_AMP, p.type[PX_B_AMP]), p.on[PX_B_CAB] ? px_type_name(PX_B_CAB, p.type[PX_B_CAB]) : "off");
    free(in); free(out); return 0;
}
