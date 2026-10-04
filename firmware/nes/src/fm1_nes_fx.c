#include "fm1_nes_fx.h"
#include <string.h>

/* Chamberlin state-variable filter, deliberately capped below the high-cutoff
   instability region. f=2*sin(pi*Fc/44100), Q12; logarithmic80..5000Hz.
   Background: earlevel.com/main/2003/03/02/the-digital-state-variable-filter/
   No floating point, transcendental calls or per-sample division by a variable.
   Q8 states retain low-level detail; bounded +/-8388352 (32767*256).
   Products use signed64; sums fit signed32. No signed shifts/overflow. */
static const uint16_t frequency_table[128]={47,48,50,51,53,55,57,59,61,63,65,67,69,71,74,76,79,81,84,87,90,92,96,99,102,105,109,112,116,120,124,128,132,137,141,146,151,156,161,166,172,177,183,189,196,202,209,216,223,230,238,246,254,262,271,280,289,299,308,319,329,340,351,363,375,387,400,413,427,441,456,471,486,503,519,536,554,572,591,611,631,652,673,696,719,742,767,792,818,845,873,902,932,962,994,1027,1060,1095,1131,1169,1207,1247,1287,1330,1373,1418,1465,1513,1562,1613,1666,1720,1776,1834,1894,1955,2018,2084,2151,2221,2292,2366,2442,2520,2601,2683,2769,2857};
static unsigned bound(int n,unsigned max) {return n<0?0:(unsigned)n>max?max:(unsigned)n;}
static int32_t smooth(int32_t current,int32_t target) {
    int32_t d=target-current;
    return current+(d/64 ? d/64 : (d>0 ? 1 : d<0 ? -1 : 0));
}
static int32_t limit(fm1_fx_state *s,int32_t v) {
    if(v>8388352){++s->limited;return 8388352;}
    if(v<-8388352){++s->limited;return -8388352;}
    return v;
}
static void preset(fm1_fx_controls *c,unsigned index) {
    /* mode,cutoff,resonance,rate,depth,mix: dry / warm / slow wah / acid sweep */
    static const uint8_t settings[4][6]={
        {0,112,0,19,0,127},{1,92,24,9,0,127},
        {1,76,60,9,72,127},{2,84,100,29,96,100}};
    const uint8_t *p=settings[index];
    c->mode=p[0];c->cutoff=p[1];c->resonance=p[2];c->rate=p[3];c->depth=p[4];c->mix=p[5];c->preset=(uint8_t)index;
}
void fm1_fx_controls_reset(fm1_fx_controls *c) {memset(c,0,sizeof(*c));preset(c,0);}
void fm1_fx_control_edges(fm1_fx_controls *c,unsigned id,int32_t edges) {
    int delta;
    /* Reject impossible accumulated jumps without overflow. No wrap at ends. */
    if(edges>512)edges=512;if(edges<-512)edges=-512;
    delta=(int)edges;
    switch(id) {
    case 0:c->mix=(uint8_t)bound(c->mix+delta,127);break;
    case 1:
        delta+=c->mode_edges;c->mode_edges=(int8_t)(delta%4);
        c->mode=(uint8_t)bound(c->mode+delta/4,3);break;
    case 2:c->cutoff=(uint8_t)bound(c->cutoff+delta,127);break;
    case 3:c->resonance=(uint8_t)bound(c->resonance+delta,127);break;
    case 4:c->rate=(uint8_t)bound(c->rate+delta,127);break;
    case 5:c->depth=(uint8_t)bound(c->depth+delta,127);break;
    case 6:
        delta+=c->preset_edges;c->preset_edges=(int8_t)(delta%4);
        if(delta/4){preset(c,bound(c->preset+delta/4,3));c->mode_edges=0;}
        break;
    default:break;
    }
}
void fm1_fx_reset(fm1_fx_state *s) {
    memset(s,0,sizeof(*s));s->frequency=frequency_table[112];s->damping=7373;s->rate_cache=255;
}
int fm1_fx_process(fm1_fx_state *s,const fm1_fx_controls *c,const int16_t *in,int16_t *out,size_t n) {
    size_t i;int target_wet;
    if(!s || !c || (!in && n) || (!out && n) || n>64 || c->mode>3 ||
       c->cutoff>127 || c->resonance>127 || c->rate>127 || c->depth>127 || c->mix>127)return -1;
    if(s->rate_cache!=c->rate) {
        /* 0.1..12.8Hz in0.1Hz steps, computed only when rate changes. */
        s->phase_increment=(uint32_t)(((uint64_t)(c->rate+1u)*10u<<32)/4410000u);
        s->rate_cache=c->rate;
    }
    target_wet=c->mode?(c->mix*256+63)/127:0;
    if(!target_wet && !s->wet) {
        /* Exact bypass fast path, including phase continuity across blocks. */
        s->phase+=s->phase_increment*(uint32_t)n;s->mode=c->mode;s->low=s->band=0;
        if(n && out!=in)memcpy(out,in,n*sizeof(*out));return 0;
    }
    for(i=0;i<n;++i) {
        int32_t dry=in[i],p,triangle,index,high,filtered,wet_goal;
        s->phase+=s->phase_increment;
        /* Switching filter topology fades fully to dry before selecting it. */
        wet_goal=c->mode==s->mode?target_wet:0;
        if(s->wet<wet_goal)++s->wet;else if(s->wet>wet_goal)--s->wet;
        if(!s->wet && s->mode!=c->mode){s->mode=c->mode;s->low=s->band=0;}
        if(!s->wet && !target_wet){out[i]=(int16_t)dry;s->low=s->band=0;continue;}
        p=(int32_t)(s->phase>>16);
        triangle=p<32768 ? p*2-32768 : 98303-p*2;
        index=(int32_t)c->cutoff+(triangle*(int32_t)c->depth)/65536;
        index=(int32_t)bound(index,127);
        s->frequency=smooth(s->frequency,frequency_table[index]);
        s->damping=smooth(s->damping,7373-(int32_t)c->resonance*50); /* 1/Q:1.8..0.25 */
        s->low=limit(s,s->low+(int32_t)(((int64_t)s->frequency*s->band)/4096));
        high=limit(s,dry*256-s->low-(int32_t)(((int64_t)s->damping*s->band)/4096));
        s->band=limit(s,s->band+(int32_t)(((int64_t)s->frequency*high)/4096));
        filtered=(s->mode==FM1_FX_LP?s->low:s->mode==FM1_FX_BP?s->band:high)/256;
        out[i]=(int16_t)(dry+((filtered-dry)*s->wet)/256);
    }
    return 0;
}
