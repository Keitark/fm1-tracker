#include "fm1_channel_fx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint8_t lanes[4][64],mix[64];static const uint8_t *voices[]={lanes[0],lanes[1],lanes[2],lanes[3]};
static uint32_t rng=123;
static void generate(void) {
    unsigned c,i;for(i=0;i<64;++i) {
        for(c=0;c<4;++c){rng=rng*1664525u+1013904223u;lanes[c][i]=(uint8_t)(rng>>28);}
        mix[i]=(uint8_t)((247u*(lanes[0][i]+lanes[1][i])+279u*lanes[2][i]+162u*lanes[3][i])/128u);
    }
}
static void selection(void) {
    fm1_channel_fx b;fm1_fx_controls saved[5];unsigned i;
    fm1_channel_fx_reset(&b);CHECK(b.selected==4);
    fm1_channel_fx_edges(&b,0,-16);CHECK(b.selected==0);
    for(i=0;i<5;++i) {
        CHECK(b.selected==i);fm1_channel_fx_edges(&b,2,-(int)(10+i));
        fm1_channel_fx_edges(&b,3,(int)(20+i));fm1_channel_fx_edges(&b,4,(int)i);
        fm1_channel_fx_edges(&b,5,(int)(30+i));fm1_channel_fx_edges(&b,1,4);
        saved[i]=b.controls[i];fm1_channel_fx_edges(&b,0,4);
    }
    CHECK(b.selected==4 && !memcmp(saved,b.controls,sizeof(saved)));
    fm1_channel_fx_edges(&b,0,-12);CHECK(b.selected==1);
    fm1_channel_fx_edges(&b,6,8);CHECK(b.controls[1].preset==2);
    CHECK(!memcmp(&b.controls[0],&saved[0],sizeof(saved[0])));
    CHECK(!memcmp(&b.controls[2],&saved[2],3*sizeof(saved[0])));
    fm1_channel_fx_edges(&b,0,INT32_MIN);CHECK(b.selected==0);
    fm1_channel_fx_edges(&b,0,INT32_MAX);CHECK(b.selected==4);
    CHECK(!strcmp(fm1_channel_fx_name(2),"TRIANGLE"));
}
static void dry_reference(void) {
    fm1_channel_fx b;int16_t out[64];int32_t dc=0;unsigned block,i,gain;
    for(gain=0;gain<=128;gain+=4) {
        fm1_channel_fx_reset(&b);
        for(block=0;block<1000;++block) {
            generate();CHECK(!fm1_channel_fx_process(&b,mix,voices,(uint8_t)gain,out,64));
            for(i=0;i<64;++i) {
                int32_t x=(int32_t)mix[i]*256;
                if(!block && !i)dc=x;dc+=(x-dc)/256;
                CHECK(out[i]==((x-dc)*(int32_t)gain)/256);
            }
        }
    }
    {fm1_channel_fx before=b;lanes[3][63]=16;
     CHECK(fm1_channel_fx_process(&b,mix,voices,4,out,64)<0 && !memcmp(&b,&before,sizeof(b)));}
}
static void isolation(void) {
    fm1_channel_fx wet_a,dry_a,wet_b,dry_b;int16_t a[64],ad[64],b[64],bd[64];
    uint8_t saved[4][64],mix_saved[64];unsigned block,i,c,selected,changed;
    for(selected=0;selected<4;++selected) {
        fm1_channel_fx_reset(&wet_a);fm1_channel_fx_reset(&dry_a);
        wet_a.controls[selected].mode=FM1_FX_LP;wet_a.controls[selected].cutoff=40;
        wet_b=wet_a;dry_b=dry_a;changed=0;
        for(block=0;block<200;++block) {
            generate();memcpy(saved,lanes,sizeof(saved));memcpy(mix_saved,mix,sizeof(mix));
            CHECK(!fm1_channel_fx_process(&wet_a,mix,voices,4,a,64));
            CHECK(!fm1_channel_fx_process(&dry_a,mix,voices,4,ad,64));
            for(i=0;i<64;++i) {
                for(c=0;c<4;++c)if(c!=selected)lanes[c][i]=0;
                mix[i]=(uint8_t)((247u*(lanes[0][i]+lanes[1][i])+279u*lanes[2][i]+162u*lanes[3][i])/128u);
            }
            CHECK(!fm1_channel_fx_process(&wet_b,mix,voices,4,b,64));
            CHECK(!fm1_channel_fx_process(&dry_b,mix,voices,4,bd,64));
            for(i=0;i<64;++i){CHECK(a[i]-ad[i]==b[i]-bd[i]);if(a[i]!=ad[i])++changed;}
        }
        CHECK(changed>1000);
    }
}
static void master_and_partition(void) {
    fm1_channel_fx all,plain,split;fm1_fx_state master;fm1_fx_controls controls;
    int16_t a[64],p[64],expected[64],small[64];unsigned i,block;const uint8_t *part[4];
    fm1_channel_fx_reset(&all);fm1_channel_fx_reset(&plain);fm1_fx_reset(&master);fm1_fx_controls_reset(&controls);
    all.controls[4].mode=controls.mode=FM1_FX_BP;all.controls[4].depth=controls.depth=72;
    split=all;
    for(block=0;block<200;++block) {
        generate();CHECK(!fm1_channel_fx_process(&all,mix,voices,4,a,64));
        CHECK(!fm1_channel_fx_process(&plain,mix,voices,4,p,64));
        CHECK(!fm1_fx_process(&master,&controls,p,expected,64));CHECK(!memcmp(a,expected,sizeof(a)));
        for(i=0;i<64;++i) {
            unsigned c;for(c=0;c<4;++c)part[c]=voices[c]+i;
            CHECK(!fm1_channel_fx_process(&split,mix+i,part,4,small+i,1));
        }
        CHECK(!memcmp(a,small,sizeof(a)));
    }
    CHECK(!memcmp(&all,&split,sizeof(all)));
}
static void full_gain_effects(void) {
    fm1_channel_fx b,before;int16_t out[64];unsigned block,c;
    fm1_channel_fx_reset(&b);
    for(block=0;block<4096;block++) {
        generate();
        for(c=0;c<5;c++) {
            b.controls[c].mode=(block+c)%4;b.controls[c].resonance=127;
            b.controls[c].cutoff=block%128;b.controls[c].depth=127;
        }
        CHECK(!fm1_channel_fx_process(&b,mix,voices,128,out,64));
    }
    before=b;CHECK(fm1_channel_fx_process(&b,mix,voices,129,out,64)<0);
    CHECK(!memcmp(&before,&b,sizeof(b)));
}
static void benchmark(void) {
    unsigned active,block,i;int16_t out[64];volatile uint32_t check=0;
    generate();
    for(active=0;active<=5;active+=(active==0?1:4)) {
        fm1_channel_fx b;clock_t start;fm1_channel_fx_reset(&b);
        for(i=0;i<active;++i){b.controls[i].mode=1;b.controls[i].depth=90;b.controls[i].resonance=80;}
        start=clock();for(block=0;block<100000;++block){CHECK(!fm1_channel_fx_process(&b,mix,voices,4,out,64));check+=(uint16_t)out[0];}
        printf("HOST ONLY bank benchmark active=%u 6400000 samples %.3fs check=%u\n",active,(double)(clock()-start)/CLOCKS_PER_SEC,(unsigned)check);
    }
}
int main(int argc,char **argv) {
    selection();dry_reference();isolation();master_and_partition();full_gain_effects();
    if(argc>1 && !strcmp(argv[1],"--bench"))benchmark();
    puts("PASS channel selection, independent settings/isolation, exact dry mix, master order and partition equivalence");return 0;
}
