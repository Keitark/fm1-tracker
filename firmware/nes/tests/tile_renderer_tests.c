/* Include production-generated implementation plus host-only exact oracle. */
#include FM1_RENDER_CORE_FILE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static unsigned trial,hooks;
static uint32_t rng=0x9285193;
static uint8_t nt[4][1024],chr[8][1024],exram[1024];
static uint16_t events[256],saved_events[256];
#define CHECK(x) do{if(!(x)){fprintf(stderr,"tile trial%u line%u: %s\n",trial,__LINE__,#x);exit(1);}}while(0)
static uint8_t rnd(void){rng=rng*1664525u+1013904223u;return rng>>24;}
static void mapper_hook(nes_t *n,uint16_t address){
    unsigned i;CHECK(hooks<256);events[hooks++]=address;
    /* Exercise latching hooks while preserving pre-hook tile fetch semantics. */
    for(i=0;i<8;i++)n->nes_ppu.pattern_table[i]=chr[(i+hooks)%8];
    for(i=0;i<4;i++)n->nes_ppu.name_table[i]=nt[(i+hooks)%4];
}
int main(void){
    static nes_t a,b;nes_color_t guarded[258];unsigned i,j,m,x,scan,hits=0,misses=0,saved_hooks;
    sprite_line_t sa,sb;
    /* Every possible pair of bitplanes, all partial-tile starts and masks. */
    memset(&a,0,sizeof(a));
    for(i=0;i<16;i++)a.nes_ppu.background_palette[i]=(nes_color_t)(0x731*i);
    for(trial=0;trial<65536;trial++) {
        unsigned b0=trial&255,b1=trial>>8;
        unsigned packed=fm1_tile_expand[b0]|(fm1_tile_expand[b1]<<1);
        for(m=0;m<8;m++)CHECK(((packed>>(2*m))&3)==(((b0>>m)&1)|(((b1>>m)&1)<<1)));
        for(m=0;m<8;m++) {
            a.nes_ppu.MASK_m=trial&1;memset(a.nes_ppu.bg_opaque,0xa5,256);
            for(i=0;i<258;i++)guarded[i]=0xa55a;
            x=(trial&2)?8:0;
            CHECK(fm1_tile_background_span(&a,guarded+1,(uint8_t)x,12,(uint8_t)b0,(uint8_t)b1,m,0)==x+m+1);
            for(i=0;i<=m;i++) {
                unsigned c=((b0>>(m-i))&1)|(((b1>>(m-i))&1)<<1);
                if(x+i<8 && !a.nes_ppu.MASK_m){CHECK(!a.nes_ppu.bg_opaque[x+i]);CHECK(guarded[1+x+i]==a.nes_ppu.background_palette[0]);}
                else {CHECK(a.nes_ppu.bg_opaque[x+i]==(c!=0));CHECK(guarded[1+x+i]==a.nes_ppu.background_palette[12+c]);}
            }
            CHECK(guarded[0]==0xa55a && guarded[257]==0xa55a);
            CHECK(guarded[1+x+m+1]==0xa55a && a.nes_ppu.bg_opaque[x+m+1]==0xa5);
        }
    }
    for(i=0;i<4;i++)for(j=0;j<1024;j++)nt[i][j]=rnd();
    for(i=0;i<8;i++)for(j=0;j<1024;j++)chr[i][j]=rnd();
    for(i=0;i<1024;i++)exram[i]=rnd();
    for(trial=0;trial<20000;trial++) {
        memset(&a,0,sizeof(a));
        for(i=0;i<4;i++)a.nes_ppu.name_table[i]=nt[i];
        for(i=0;i<8;i++)a.nes_ppu.pattern_table[i]=chr[i];
        a.nes_rom.mapper_number=(trial&1)?3:0;
        a.nes_rom.chr_rom=&chr[0][0];a.nes_rom.chr_rom_size=1;
        if(trial%3==1){a.nes_mapper.mapper_ppu=mapper_hook;a.nes_mapper.mapper_ppu_tile_min=32;a.nes_mapper.mapper_ppu_tile_max=223;}
        if(trial%3==2)a.nes_mapper.mapper_exram=exram;
        a.nes_mapper.mapper_chr_hi=trial&3;
        a.nes_ppu.MASK_b=a.nes_ppu.MASK_s=1;
        a.nes_ppu.MASK_m=trial&1;a.nes_ppu.MASK_M=(trial>>1)&1;
        a.nes_ppu.CTRL_H=(trial>>2)&1;a.nes_ppu.CTRL_B=(trial>>3)&1;a.nes_ppu.CTRL_S=(trial>>4)&1;
        a.nes_ppu.x=rnd()&7;a.nes_ppu.v.coarse_x=rnd()&31;a.nes_ppu.v.coarse_y=rnd()&31;
        a.nes_ppu.v.fine_y=rnd()&7;a.nes_ppu.v.nametable=rnd()&3;
        for(i=0;i<32;i++)a.nes_ppu.palette_indexes[i]=rnd()&63;
        nes_palette_generate(&a); /* Mutating palettes are never cached by tiles. */
        scan=trial%240;
        for(i=0;i<64;i++) {
            sprite_info_t *s=&a.nes_ppu.sprite_info[i];s->y=(i<10)?(uint8_t)(scan-1):rnd();
            s->x=rnd();s->tile_index_number=rnd();s->flip_h=rnd()&1;s->flip_v=rnd()&1;
            s->sprite_palette=rnd()&3;s->priority=rnd()&1;
        }
        if(trial&8)a.nes_ppu.STATUS_S=1;
        memcpy(&b,&a,sizeof(a));hooks=0;fm1_render_pixels=1;fm1_tile_test_reference=1;
        nes_prepare_sprite_line(&a,scan,&sa);
        nes_render_background_line(&a,scan,a.nes_draw_data);
        nes_render_sprite_line(&a,&sa,a.nes_draw_data);
        saved_hooks=hooks;memcpy(saved_events,events,hooks*sizeof(*events));
        hooks=0;fm1_tile_test_reference=0;
        nes_prepare_sprite_line(&b,scan,&sb);
        nes_render_background_line(&b,scan,b.nes_draw_data);
        nes_render_sprite_line(&b,&sb,b.nes_draw_data);
        CHECK(hooks==saved_hooks && !memcmp(events,saved_events,hooks*sizeof(*events)));
        CHECK(!memcmp(&a,&b,sizeof(a)));
        if(a.nes_ppu.STATUS_S)hits++;else misses++;
        /* Live CHR RAM changes, including bytes used by banked fetches. */
        chr[rnd()&7][(unsigned)rnd()*4]=rnd();
    }
    CHECK(hits && misses);
    printf("PASS 65536 bitplane pairs plus20000 scanlines: pixels, opacity, mapper events, ExRAM, clipping, bank changes and sprite state identical (%u hits/%u misses)\n",hits,misses);
    return 0;
}
