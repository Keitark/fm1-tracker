/* Compile the same generated core to directly exercise its static renderers. */
#include FM1_RENDER_CORE_FILE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL case=%u line=%u: %s\n",trial,__LINE__,#x);exit(1);}}while(0)
static uint32_t random_state=0x192937;
static uint8_t random_byte(void){random_state=random_state*1664525u+1013904223u;return random_state>>24;}
int main(void){
    static nes_t a,b;static uint8_t nt[4][1024],chr[8][1024];
    unsigned trial,i,j,scan;unsigned hits=0,misses=0;
    for(i=0;i<4;i++)for(j=0;j<1024;j++)nt[i][j]=random_byte();
    for(i=0;i<8;i++)for(j=0;j<1024;j++)chr[i][j]=random_byte();
    for(trial=0;trial<6000;trial++){
        sprite_line_t sprites;
        memset(&a,0,sizeof(a));
        for(i=0;i<4;i++)a.nes_ppu.name_table[i]=nt[i];
        for(i=0;i<8;i++)a.nes_ppu.pattern_table[i]=chr[i];
        a.nes_rom.mapper_number=trial%2?3:0;
        a.nes_ppu.MASK_b=a.nes_ppu.MASK_s=1;
        a.nes_ppu.MASK_m=trial&1;a.nes_ppu.MASK_M=(trial>>1)&1;
        a.nes_ppu.CTRL_H=(trial>>2)&1;a.nes_ppu.CTRL_B=(trial>>3)&1;
        a.nes_ppu.CTRL_S=(trial>>4)&1;
        a.nes_ppu.x=trial%8;a.nes_ppu.v.coarse_x=(trial/8)%32;
        a.nes_ppu.v.coarse_y=(trial/256)%32;a.nes_ppu.v.fine_y=(trial/32)%8;
        a.nes_ppu.v.nametable=(trial/64)%4;
        for(i=0;i<64;i++){
            a.nes_ppu.sprite_info[i].y=255;
            a.nes_ppu.sprite_info[i].x=random_byte();
            a.nes_ppu.sprite_info[i].tile_index_number=random_byte();
            a.nes_ppu.sprite_info[i].flip_h=random_byte()&1;
            a.nes_ppu.sprite_info[i].flip_v=random_byte()&1;
        }
        a.nes_ppu.sprite_info[0].y=(uint8_t)(trial%240);
        a.nes_ppu.sprite_info[0].x=(uint8_t)trial;
        scan=(unsigned)a.nes_ppu.sprite_info[0].y+(trial%19);
        /* Include overflow and sprite-zero overlap with other sprites. */
        if(trial&8)for(i=1;i<10;i++)a.nes_ppu.sprite_info[i].y=a.nes_ppu.sprite_info[0].y;
        memcpy(&b,&a,sizeof(a));
        fm1_render_pixels=1;
        nes_prepare_sprite_line(&a,scan,&sprites);
        nes_render_background_line(&a,scan,a.nes_draw_data);
        nes_render_sprite_line(&a,&sprites,a.nes_draw_data);
        fm1_render_pixels=0;
        nes_prepare_sprite_line(&b,scan,&sprites);
        nes_render_background_line(&b,scan,b.nes_draw_data);
        nes_render_sprite_line(&b,&sprites,b.nes_draw_data);
        CHECK(a.nes_ppu.ppu_status==b.nes_ppu.ppu_status);
        if(a.nes_ppu.STATUS_S)++hits;else ++misses;
        for(i=0;i<256;i++)CHECK(!b.nes_draw_data[i]);
    }
    CHECK(hits && misses);
    printf("PASS 6000 sprite-zero/overflow cases: hits=%u misses=%u; clipping, scroll, flips, 8x16, bank selection and x=255\n",hits,misses);
    return 0;
}
