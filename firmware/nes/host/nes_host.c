#include "fm1_nes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint16_t pixels[256*240];
    uint32_t video_hash,audio_hash;
    uint8_t buttons,amin,amax;
    int fail;
} host_t;
static uint32_t hash_byte(uint32_t h,uint8_t b) {return (h^b)*16777619u;}
static int video(void *v,unsigned y,unsigned rows,const uint16_t *p) {
    host_t *h=v;size_t i;
    if(y+rows>240) return -1;
    memcpy(h->pixels+y*256,p,rows*256*sizeof(*p));
    for(i=0;i<rows*256;++i) {h->video_hash=hash_byte(h->video_hash,(uint8_t)p[i]);h->video_hash=hash_byte(h->video_hash,(uint8_t)(p[i]>>8));}
    return h->fail;
}
static int audio(void *v,const uint8_t *p,size_t n) {
    host_t *h=v;size_t i;
    for(i=0;i<n;++i) {h->audio_hash=hash_byte(h->audio_hash,p[i]);if(p[i]<h->amin)h->amin=p[i];if(p[i]>h->amax)h->amax=p[i];}
    return 0;
}
static uint8_t buttons(void *v) {return ((host_t *)v)->buttons;}
static int frame(void *v,uint32_t n) {(void)v;(void)n;return 0;}
static int ppm(const char *path,const uint16_t *p) {
    FILE *f=fopen(path,"wb");size_t i;int failed=0;
    if(!f)return -1;
    if(fprintf(f,"P6\n256 240\n255\n")<0)failed=1;
    for(i=0;i<256*240;++i) {uint8_t b[3];b[0]=(uint8_t)((p[i]>>11)*255/31);b[1]=(uint8_t)(((p[i]>>5)&63)*255/63);b[2]=(uint8_t)((p[i]&31)*255/31);if(fwrite(b,1,3,f)!=3)failed=1;}
    if(fclose(f))failed=1;return failed?-1:0;
}
int main(int argc,char **argv) {
    FILE *f;long len;uint8_t *rom;uint32_t frames;int rc;static host_t h;
    fm1_nes_stats s;fm1_nes_platform p={&h,video,audio,buttons,frame};
    if(argc==2 && !strcmp(argv[1],"--target-gate"))return fm1_nes_target_start(NULL,0)==FM1_NES_BSP_UNVERIFIED?0:1;
    if(argc<3 || argc>6) {fprintf(stderr,"usage: nes_host ROM FRAMES [PAD_MASK] [FRAME.ppm] [fail-video]\n");return 2;}
    frames=(uint32_t)strtoul(argv[2],NULL,0);if(!frames || frames>36000)return 2;
    h.buttons=argc>3?(uint8_t)strtoul(argv[3],NULL,0):0;
    h.fail=argc>5;h.amin=255;h.video_hash=h.audio_hash=2166136261u;
    f=fopen(argv[1],"rb");if(!f)return 2;
    if(fseek(f,0,SEEK_END) || (len=ftell(f))<0 || len>131072 || fseek(f,0,SEEK_SET)) {fclose(f);return 2;}
    rom=malloc((size_t)len+1);if(!rom){fclose(f);return 2;}
    if(fread(rom,1,(size_t)len,f)!=(size_t)len){fclose(f);free(rom);return 2;}fclose(f);
    rc=fm1_nes_run(rom,(size_t)len,&p,frames,&s);
    if(!rc && argc>4 && ppm(argv[4],h.pixels))rc=FM1_NES_IO_ERROR;
    printf("{\"result\":%d,\"frames\":%u,\"video_blocks\":%u,\"audio_samples\":%u,\"state_bytes\":%zu,\"ram0\":%u,\"ram1\":%u,\"video_hash\":%u,\"audio_hash\":%u,\"audio_min\":%u,\"audio_max\":%u}\n",rc,s.frames,s.video_blocks,s.audio_samples,s.state_bytes,s.ram0,s.ram1,h.video_hash,h.audio_hash,h.amin,h.amax);
    free(rom);return rc?1:0;
}
