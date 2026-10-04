#include "fm1_rom50.h"
#include <stdio.h>
static uint32_t current_frame;
static unsigned audio_min=255,audio_max;

static int video(void *ctx,unsigned y,unsigned rows,const uint16_t *p) {
    (void)ctx;
    return !p || y+rows>240;
}
static int audio(void *ctx,const uint8_t *p,size_t size) {
    size_t i;(void)ctx;
    if(!p || !size)return -1;
    for(i=0;i<size;i++){if(p[i]<audio_min)audio_min=p[i];if(p[i]>audio_max)audio_max=p[i];}
    return 0;
}
/* A fresh Start edge after the title has initialized, not held through reset. */
static uint8_t buttons(void *ctx) {(void)ctx;return current_frame>=120 && current_frame<124 ? FM1_PAD_START:0;}
static int frame(void *ctx,uint32_t count) {(void)ctx;current_frame=count;return 0;}
int main(void) {
    size_t size=0;
    const uint8_t *rom=fm1_rom50_data(&size);
    fm1_nes_stats stats;
    fm1_nes_platform p={0,video,audio,buttons,frame};
    if(size!=FM1_TEST_ROM_BYTES || rom!=fm1_rom50_data(0))return 1;
    if(fm1_nes_validate_rom(rom,size)!=FM1_NES_OK)return 2;
    /* Host target entry must stay gated even with a compiled-in cartridge. */
    if(fm1_rom50_start()!=FM1_NES_BSP_UNVERIFIED)return 3;
    if(fm1_nes_run(rom,size,&p,600,&stats)!=FM1_NES_OK)return 4;
    if(stats.frames!=600 || stats.video_blocks!=1200 || stats.audio_samples!=441000)return 5;
    printf("Timed Start: audio_min=%u audio_max=%u\n",audio_min,audio_max);
#if FM1_TEST_ROM_BYTES == 40976
    if(audio_max<=audio_min)return 6; /* SMB1 gameplay must not stay silent. */
#endif
    puts("Embedded cartridge: 600 frames passed; target entry remains gated.");
    return 0;
}
