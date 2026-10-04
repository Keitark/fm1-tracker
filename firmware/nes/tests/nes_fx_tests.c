#include "fm1_nes_fx.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void controls(void) {
    fm1_fx_controls c;unsigned i;fm1_fx_controls_reset(&c);
    CHECK(!c.mode && c.mix==127 && c.cutoff==112 && !c.depth);
    fm1_fx_control_edges(&c,1,3);CHECK(!c.mode);
    fm1_fx_control_edges(&c,1,1);CHECK(c.mode==1);
    fm1_fx_control_edges(&c,1,4);CHECK(c.mode==2);
    fm1_fx_control_edges(&c,1,4);CHECK(c.mode==3);
    fm1_fx_control_edges(&c,1,-12);CHECK(!c.mode);
    fm1_fx_control_edges(&c,0,-27);CHECK(c.mix==100);
    fm1_fx_control_edges(&c,2,-12);CHECK(c.cutoff==100);
    fm1_fx_control_edges(&c,3,33);CHECK(c.resonance==33);
    fm1_fx_control_edges(&c,4,1);CHECK(c.rate==20);
    fm1_fx_control_edges(&c,5,64);CHECK(c.depth==64);
    for(i=0;i<3;++i)fm1_fx_control_edges(&c,6,4);
    CHECK(c.preset==3 && c.mode==2 && c.resonance==100);
    fm1_fx_control_edges(&c,6,-12);CHECK(!c.preset && !c.mode);
    for(i=0;i<7;++i)fm1_fx_control_edges(&c,i,INT32_MAX);
    CHECK(c.mode<=3 && c.cutoff<=127 && c.resonance<=127 && c.rate<=127 && c.depth<=127 && c.mix<=127);
    for(i=0;i<7;++i)fm1_fx_control_edges(&c,i,INT32_MIN);
    CHECK(!c.mode && !c.preset);
    {fm1_fx_controls before=c;fm1_fx_control_edges(&c,7,2);CHECK(!memcmp(&before,&c,sizeof(c)));}
}
static void bypass_and_partition(void) {
    fm1_fx_state a,b;fm1_fx_controls c;int16_t in[64],out[64],split[64];unsigned i,j;
    fm1_fx_controls_reset(&c);fm1_fx_reset(&a);
    for(i=0;i<64;++i)in[i]=(int16_t)(i*1040-32768);
    CHECK(!fm1_fx_process(&a,&c,in,out,64) && !memcmp(in,out,sizeof(in)));
    CHECK(!fm1_fx_process(&a,&c,0,0,0));
    CHECK(fm1_fx_process(&a,&c,in,out,65)<0);
    c.cutoff=128;CHECK(fm1_fx_process(&a,&c,in,out,64)<0);c.cutoff=55;
    c.mode=1;c.depth=90;c.resonance=50;fm1_fx_reset(&a);b=a;
    for(j=0;j<100;++j) {
        CHECK(!fm1_fx_process(&a,&c,in,out,64));
        for(i=0;i<64;++i)CHECK(!fm1_fx_process(&b,&c,in+i,split+i,1));
        CHECK(!memcmp(out,split,sizeof(out)) && !memcmp(&a,&b,sizeof(a)));
    }
    c.mode=0;
    for(i=0;i<5;++i)CHECK(!fm1_fx_process(&a,&c,in,out,64));
    CHECK(!a.wet && !memcmp(in,out,sizeof(in)));
    c.mode=1;c.mix=0;CHECK(!fm1_fx_process(&a,&c,in,out,64) && !memcmp(in,out,sizeof(in)));
}
static double tone(unsigned mode,unsigned res,double hz) {
    fm1_fx_controls c;fm1_fx_state s;unsigned block,i;int16_t in[64],out[64];double energy=0;
    fm1_fx_controls_reset(&c);fm1_fx_reset(&s);c.mode=(uint8_t)mode;c.cutoff=55;c.resonance=(uint8_t)res;
    for(block=0;block<1400;++block) {
        for(i=0;i<64;++i)in[i]=(int16_t)(2000*sin(6.283185307179586*hz*(block*64u+i)/44100));
        CHECK(!fm1_fx_process(&s,&c,in,out,64));
        if(block>=700)for(i=0;i<64;++i)energy+=(double)out[i]*out[i];
    }
    CHECK(!s.limited);return sqrt(energy/(700*64));
}
static void response(void) {
    double lp_low=tone(1,0,200),lp_high=tone(1,0,4000);
    double hp_low=tone(3,0,50),hp_high=tone(3,0,4000);
    double bp=tone(2,0,480),bp_low=tone(2,0,50),bp_high=tone(2,0,4000);
    double resonant=tone(1,100,480),plain=tone(1,0,480);
    printf("RMS response: LP200=%.2f LP4k=%.2f HP50=%.2f HP4k=%.2f BP480=%.2f resonance=%.2f/%.2f\n",
        lp_low,lp_high,hp_low,hp_high,bp,resonant,plain);
    CHECK(lp_low>lp_high*10 && hp_high>hp_low*10 && bp>bp_low*4 && bp>bp_high*4 && resonant>plain*2);
}
static void modulation_and_bounds(void) {
    fm1_fx_controls c;fm1_fx_state a,b;int16_t in[64],out[64],plain[64];uint32_t random=17;unsigned mode,k,i,different=0;
    fm1_fx_controls_reset(&c);fm1_fx_reset(&a);fm1_fx_reset(&b);c.mode=1;c.cutoff=75;
    for(k=0;k<2000;++k) {
        for(i=0;i<64;++i)in[i]=(int16_t)(1500*sin(6.283185307179586*1500*(k*64u+i)/44100));
        c.depth=0;CHECK(!fm1_fx_process(&a,&c,in,plain,64));
        c.depth=100;CHECK(!fm1_fx_process(&b,&c,in,out,64));
        if(memcmp(out,plain,sizeof(out)))different++;
    }
    CHECK(different>1000);
    for(mode=1;mode<=3;++mode)for(k=0;k<4;++k) {
        unsigned block;fm1_fx_reset(&a);c.mode=(uint8_t)mode;c.cutoff=(k&1)?127:0;c.resonance=(k&2)?127:0;c.depth=127;c.rate=127;
        for(block=0;block<2000;++block) {
            for(i=0;i<64;++i){random=random*1664525u+1013904223u;in[i]=(int16_t)(random>>16);}
            CHECK(!fm1_fx_process(&a,&c,in,out,64));
            CHECK(a.low>=-8388352 && a.low<=8388352 && a.band>=-8388352 && a.band<=8388352);
            CHECK(a.frequency>=47 && a.frequency<=2857 && a.damping>=1023 && a.damping<=7373);
        }
    }
    for(k=0;k<128;++k) {
        double rate;c.rate=(uint8_t)k;CHECK(!fm1_fx_process(&a,&c,in,out,1));
        rate=(double)a.phase_increment*44100/4294967296.0;
        CHECK(fabs(rate-(k+1)*0.1)<0.000011);
    }
}
int main(void) {
    clock_t start=clock();controls();bypass_and_partition();response();modulation_and_bounds();
    printf("PASS live FX: mappings, exact bypass, partition invariance, filter response, LFO, extreme bounds (host %.3fs)\n",
        (double)(clock()-start)/CLOCKS_PER_SEC);return 0;
}
