/* Reference/optimized full-game comparison with identical scripted inputs. */
#include "fm1_rom50.h"
#include "nes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define FRAMES 900
extern int fm1_tile_test_reference;
extern const void *fm1_nes_test_machine(void);
static unsigned mode,frame_no,blocks;
static uint32_t state_ref[FRAMES],audio_ref[FRAMES],video_ref[FRAMES],ah,vh;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"tile core line%u frame%u: %s\n",__LINE__,frame_no,#x);exit(1);}}while(0)
static uint32_t hash(uint32_t h,const void *data,size_t n){const uint8_t *p=data;while(n--)h=(h^*p++)*16777619u;return h;}
static int video(void *c,unsigned y,unsigned rows,const uint16_t *p){(void)c;CHECK(y+rows<=240);blocks++;vh=hash(vh,p,rows*512);return 0;}
static int audio(void *c,const uint8_t *p,size_t n){(void)c;ah=hash(ah,p,n);return 0;}
static uint8_t buttons(void *c){(void)c;if(frame_no>=120 && frame_no<124)return FM1_PAD_START;
    return frame_no>=180?FM1_PAD_RIGHT|FM1_PAD_B|((frame_no%90)<35?FM1_PAD_A:0):0;}
static int frame(void *c,uint32_t f){
    uint32_t h;(void)c;CHECK(blocks==2);
    h=hash(2166136261u,fm1_nes_test_machine(),offsetof(nes_t,nes_draw_data));
    if(!mode){state_ref[frame_no]=h;audio_ref[frame_no]=ah;video_ref[frame_no]=vh;}
    else {CHECK(h==state_ref[frame_no]);CHECK(ah==audio_ref[frame_no]);CHECK(vh==video_ref[frame_no]);}
    frame_no=f;blocks=0;vh=2166136261u;return 0;
}
int main(void){
    size_t size;const uint8_t *rom=fm1_rom50_data(&size);fm1_nes_stats stats;
    fm1_nes_platform p={0,video,audio,buttons,frame,0,0};
    for(mode=0;mode<2;mode++){
        fm1_tile_test_reference=!mode;frame_no=blocks=0;ah=vh=2166136261u;
        CHECK(!fm1_nes_run(rom,size,&p,FRAMES,&stats));
        CHECK(stats.frames==FRAMES && stats.audio_samples==FRAMES*735u);
    }
    puts("PASS tile reference/optimized SMB1 900 frames: complete CPU/PPU/APU/mapper state, audio and all pixels identical");return 0;
}
