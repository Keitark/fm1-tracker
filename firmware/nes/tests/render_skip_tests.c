#include "fm1_rom50.h"
#include "nes.h"
#include "fm1_profile.h"
#ifdef FM1_NES_PROFILE
static uint32_t profile_tick;
static uint32_t profile_clock(void){return ++profile_tick;}
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %u frame=%u: %s\n",__LINE__,frame_no,#x);exit(1);}}while(0)
extern const void *fm1_nes_test_machine(void);
#define FRAMES 900
static unsigned frame_no,mode,blocks;
static uint32_t audio_hash,video_hash,state_ref[FRAMES],audio_ref[FRAMES],video_ref[FRAMES];
static uint32_t bytes_hash(uint32_t h,const void *data,size_t size){
    const uint8_t *p=data;while(size--)h=(h^*p++)*16777619u;return h;
}
static int video(void *ctx,unsigned y,unsigned rows,const uint16_t *p){
    (void)ctx;CHECK(y+rows<=240);++blocks;
    video_hash=bytes_hash(video_hash,p,rows*256u*2u);return 0;
}
static int audio(void *ctx,const uint8_t *p,size_t size){
    (void)ctx;audio_hash=bytes_hash(audio_hash,p,size);return 0;
}
static uint8_t buttons(void *ctx){
    (void)ctx;
    if(frame_no>=120 && frame_no<124)return FM1_PAD_START;
    if(frame_no>=180)return FM1_PAD_RIGHT|FM1_PAD_B|((frame_no%90)<35?FM1_PAD_A:0);
    return 0;
}
static int render(void *ctx){(void)ctx;return mode==0 || frame_no%6==0;}
static int frame(void *ctx,uint32_t f){
    static nes_t snapshot;uint32_t h;(void)ctx;
    memcpy(&snapshot,fm1_nes_test_machine(),sizeof(snapshot));
    /* Only these two caches may differ on hidden frames. CPU/PPU/APU/mapper
       state, including CPU RAM and joypad state, must match every frame. */
    memset(snapshot.nes_ppu.bg_opaque,0,sizeof(snapshot.nes_ppu.bg_opaque));
    h=bytes_hash(2166136261u,&snapshot,offsetof(nes_t,nes_draw_data));
    if(!mode){state_ref[frame_no]=h;audio_ref[frame_no]=audio_hash;video_ref[frame_no]=video_hash;}
    else {
        CHECK(h==state_ref[frame_no]);CHECK(audio_hash==audio_ref[frame_no]);
        if(frame_no%6==0)CHECK(video_hash==video_ref[frame_no] && blocks==2);
        else CHECK(blocks==0);
    }
    frame_no=f;blocks=0;video_hash=2166136261u;return 0;
}
int main(void){
    size_t size;const uint8_t *rom=fm1_rom50_data(&size);fm1_nes_stats stats;
    fm1_nes_platform p={0,video,audio,buttons,frame,0,render};
    for(mode=0;mode<2;++mode){
#ifdef FM1_NES_PROFILE
        fm1_profile_start(mode?profile_clock:0);
#endif
        frame_no=blocks=0;audio_hash=video_hash=2166136261u;
        CHECK(fm1_nes_run(rom,size,&p,FRAMES,&stats)==0);
        CHECK(stats.frames==FRAMES && stats.audio_samples==FRAMES*735u);
        CHECK(stats.video_blocks==(mode?FRAMES/6:FRAMES)*2u);
    }
    printf("PASS SMB1 %u frames: CPU/PPU/APU/mapper state every frame, all audio, and every presented pixel identical; hidden draw callbacks eliminated\n",FRAMES);
    return 0;
}
