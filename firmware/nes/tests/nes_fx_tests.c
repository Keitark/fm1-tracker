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
static void prepared_and_partition(void) {
    fm1_fx_state regular,prepared,split;fm1_fx_controls c;
    int16_t in[64],expected[64],actual[64],partition[64];unsigned block,i;
    uint32_t random=79;
    fm1_fx_controls_reset(&c);fm1_fx_reset(&regular);prepared=split=regular;
    for(block=0;block<1200;++block) {
        /* Exercise rate changes, bypass, fades between all topologies and all
           control ranges while each render consumes the same sample stream. */
        c.mode=(uint8_t)((block/19)%4);c.cutoff=(uint8_t)(block%128);
        c.resonance=(uint8_t)((block*13)%128);c.rate=(uint8_t)((block/7)%128);
        c.depth=(uint8_t)((block*17)%128);c.mix=(uint8_t)((block*23)%128);
        for(i=0;i<64;++i){random=random*1664525u+1013904223u;in[i]=(int16_t)(random>>16);}
        CHECK(!fm1_fx_prepare(&prepared,&c) && !fm1_fx_prepare(&split,&c));
        CHECK(!fm1_fx_process(&regular,&c,in,expected,64));
        memcpy(actual,in,sizeof(actual));
        CHECK(!fm1_fx_process_prepared(&prepared,&c,actual,actual,64));
        for(i=0;i<64;++i)CHECK(!fm1_fx_process_prepared(&split,&c,in+i,partition+i,1));
        CHECK(!memcmp(expected,actual,sizeof(actual)) && !memcmp(actual,partition,sizeof(actual)));
        CHECK(!memcmp(&regular,&prepared,sizeof(regular)) && !memcmp(&prepared,&split,sizeof(prepared)));
    }
    {
        fm1_fx_state before=prepared;
        CHECK(!fm1_fx_process_prepared(&prepared,&c,0,0,0));
        CHECK(!memcmp(&before,&prepared,sizeof(before)));
    }
    /* Preparation changes only the cached increment/rate, with exact original
       quantization at every supported rate. */
    for(i=0;i<128;++i) {
        fm1_fx_state expected_state=prepared;c.rate=(uint8_t)i;
        expected_state.phase_increment=(uint32_t)(((uint64_t)(i+1u)*10u<<32)/4410000u);
        expected_state.rate_cache=c.rate;
        CHECK(!fm1_fx_prepare(&prepared,&c));
        CHECK(!memcmp(&expected_state,&prepared,sizeof(prepared)));
    }
}
static void rejected_inputs_atomic(void) {
    fm1_fx_controls c,invalid;fm1_fx_state state,before;
    int16_t in[64]={0},out[64],saved[64];unsigned field;
    fm1_fx_controls_reset(&c);fm1_fx_reset(&state);
    memset(out,0x5a,sizeof(out));memcpy(saved,out,sizeof(out));before=state;
    CHECK(fm1_fx_process_prepared(&state,&c,in,out,64)<0);
    CHECK(fm1_fx_process_prepared(&state,&c,0,0,0)<0);
    CHECK(!memcmp(&state,&before,sizeof(state)) && !memcmp(out,saved,sizeof(out)));
    CHECK(!fm1_fx_prepare(&state,&c));before=state;c.rate++;
    CHECK(fm1_fx_process_prepared(&state,&c,in,out,64)<0);
    CHECK(!memcmp(&state,&before,sizeof(state)) && !memcmp(out,saved,sizeof(out)));
    CHECK(fm1_fx_process(&state,&c,0,out,64)<0);
    CHECK(fm1_fx_process(&state,&c,in,0,64)<0);
    CHECK(fm1_fx_process(&state,&c,in,out,65)<0);
    CHECK(!memcmp(&state,&before,sizeof(state)) && !memcmp(out,saved,sizeof(out)));
    c.rate--;CHECK(fm1_fx_process_prepared(&state,&c,0,out,64)<0);
    CHECK(fm1_fx_process_prepared(&state,&c,in,0,64)<0);
    CHECK(fm1_fx_process_prepared(&state,&c,in,out,65)<0);
    CHECK(fm1_fx_prepare(0,&c)<0 && fm1_fx_prepare(&state,0)<0);
    CHECK(fm1_fx_process_prepared(0,&c,in,out,64)<0);
    CHECK(fm1_fx_process_prepared(&state,0,in,out,64)<0);
    CHECK(!memcmp(&state,&before,sizeof(state)) && !memcmp(out,saved,sizeof(out)));
    for(field=0;field<6;++field) {
        invalid=c;
        switch(field) {
        case 0:invalid.mode=4;break;case 1:invalid.cutoff=128;break;
        case 2:invalid.resonance=128;break;case 3:invalid.rate=128;break;
        case 4:invalid.depth=128;break;default:invalid.mix=128;break;
        }
        CHECK(fm1_fx_prepare(&state,&invalid)<0);
        CHECK(fm1_fx_process(&state,&invalid,in,out,64)<0);
        CHECK(fm1_fx_process_prepared(&state,&invalid,in,out,64)<0);
        CHECK(!memcmp(&state,&before,sizeof(state)) && !memcmp(out,saved,sizeof(out)));
    }
}
static int32_t reference_limit(int32_t v,uint32_t *limited) {
    if(v>8388352){++*limited;return 8388352;}
    if(v<-8388352){++*limited;return -8388352;}
    return v;
}
static void check_reference_products(fm1_fx_state state,fm1_fx_controls c,int16_t input) {
    fm1_fx_state before;int16_t actual;int32_t low,high,band,filtered;uint32_t limited;
    CHECK(!fm1_fx_prepare(&state,&c));before=state;
    CHECK(!fm1_fx_process_prepared(&state,&c,&input,&actual,1));
    limited=before.limited;
    /* Independent64-bit oracle exercises the production split products in
       all three filter updates, including signed division toward zero. */
    low=reference_limit(before.low+(int32_t)(((int64_t)state.frequency*before.band)/4096),&limited);
    high=reference_limit((int32_t)input*256-low-(int32_t)(((int64_t)state.damping*before.band)/4096),&limited);
    band=reference_limit(before.band+(int32_t)(((int64_t)state.frequency*high)/4096),&limited);
    filtered=(c.mode==FM1_FX_LP?low:c.mode==FM1_FX_BP?band:high)/256;
    CHECK(state.low==low && state.band==band && state.limited==limited && actual==filtered);
}
static void exact_product_reference(void) {
    static const int32_t states[]={-8388352,-8388351,-8384513,-8384512,-8191,-4097,-4096,-4095,-1,
        0,1,4095,4096,4097,8191,8384512,8384513,8388351,8388352};
    static const int16_t samples[]={INT16_MIN,-1,0,1,INT16_MAX};
    fm1_fx_controls c;fm1_fx_state s;unsigned mode,i,j,k,coefficient;uint32_t random=953;
    fm1_fx_controls_reset(&c);c.mix=127;c.depth=0;
    /* Every supported damping coefficient, both possible smoothing directions,
       and both coefficient/state extrema and Q12 truncation boundaries. */
    for(coefficient=1023;coefficient<=7373;++coefficient)for(i=0;i<sizeof(states)/sizeof(states[0]);++i) {
        fm1_fx_reset(&s);c.mode=FM1_FX_LP;c.cutoff=(uint8_t)(coefficient%128);
        c.resonance=(uint8_t)((coefficient&1)?0:127);
        s.mode=c.mode;s.wet=256;s.low=states[(i+7)%19];s.band=states[i];
        s.frequency=(coefficient&1)?47:2857;s.damping=(int32_t)coefficient;
        check_reference_products(s,c,samples[i%5]);
    }
    for(mode=1;mode<=3;++mode)for(i=0;i<19;++i)for(j=0;j<19;++j)for(k=0;k<5;++k) {
        fm1_fx_reset(&s);c.mode=(uint8_t)mode;c.cutoff=(uint8_t)((i&1)?0:127);
        c.resonance=(uint8_t)((j&1)?0:127);s.mode=c.mode;s.wet=256;
        s.low=states[i];s.band=states[j];s.frequency=(i&1)?47:2857;s.damping=(j&1)?1023:7373;
        check_reference_products(s,c,samples[k]);
    }
    for(i=0;i<100000;++i) {
        fm1_fx_reset(&s);c.mode=(uint8_t)(1+i%3);s.mode=c.mode;s.wet=256;
        random=random*1664525u+1013904223u;s.low=(int32_t)(random%16776705u)-8388352;
        random=random*1664525u+1013904223u;s.band=(int32_t)(random%16776705u)-8388352;
        random=random*1664525u+1013904223u;s.frequency=47+(int32_t)(random%2811u);c.cutoff=(uint8_t)(random%128);
        random=random*1664525u+1013904223u;s.damping=1023+(int32_t)(random%6351u);c.resonance=(uint8_t)(random%128);
        check_reference_products(s,c,(int16_t)(random>>16));
    }
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
    clock_t start=clock();controls();bypass_and_partition();prepared_and_partition();rejected_inputs_atomic();
    exact_product_reference();response();modulation_and_bounds();
    printf("PASS live FX: mappings, prepared equivalence, atomic rejection,64-bit product oracle, exact bypass, partition invariance, filter response, LFO, extreme bounds (host %.3fs)\n",
        (double)(clock()-start)/CLOCKS_PER_SEC);return 0;
}
