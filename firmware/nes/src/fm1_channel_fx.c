#include "fm1_channel_fx.h"
#include <string.h>
const char *fm1_channel_fx_name(unsigned id) {
    static const char *const names[]={"PULSE1","PULSE2","TRIANGLE","NOISE","ALL"};
    return id<FM1_VOICE_COUNT?names[id]:"INVALID";
}
void fm1_channel_fx_reset(fm1_channel_fx *b) {
    unsigned i;memset(b,0,sizeof(*b));b->selected=FM1_VOICE_MASTER;
    for(i=0;i<FM1_VOICE_COUNT;++i){fm1_fx_controls_reset(&b->controls[i]);fm1_fx_reset(&b->state[i]);}
}
void fm1_channel_fx_edges(fm1_channel_fx *b,unsigned id,int32_t edges) {
    if(edges>512)edges=512;if(edges<-512)edges=-512;
    if(id==0) {
        int delta=(int)edges+b->select_edges,index;
        b->select_edges=(int8_t)(delta%4);index=b->selected+delta/4;
        b->selected=(uint8_t)(index<0?0:index>=FM1_VOICE_COUNT?FM1_VOICE_MASTER:index);
    } else fm1_fx_control_edges(&b->controls[b->selected],id,edges);
}
static int16_t clipped(fm1_channel_fx *b,int32_t v) {
    if(v>32767){++b->limited;return 32767;}
    if(v<-32768){++b->limited;return -32768;}return (int16_t)v;
}
int fm1_channel_fx_process(fm1_channel_fx *b,const uint8_t *mixed,const uint8_t *const voices[4],
                           uint8_t gain,int16_t *out,size_t n) {
    static const unsigned weight[]={247,247,279,162};
    int32_t sum[64];int16_t dry[64],wet[64];size_t i;unsigned c;
    if(!b || !mixed || !voices || !out || n>64 || gain>128)return -1;
    for(c=0;c<FM1_VOICE_COUNT;++c) {
        const fm1_fx_controls *p=&b->controls[c];
        if(p->mode>3 || p->cutoff>127 || p->resonance>127 || p->rate>127 || p->depth>127 || p->mix>127)return -1;
    }
    for(c=0;c<4;++c){if(!voices[c])return -1;for(i=0;i<n;++i)if(voices[c][i]>15)return -1;}
    /* Original mono DC/gain/rounding, exactly, including startup sample.
       Add independent (wet-dry) voice corrections before the master effect.
       This preserves old mix rounding when all effects are bypassed. */
    for(i=0;i<n;++i) {
        int32_t x=(int32_t)mixed[i]*256;
        if(!b->started && !i)b->dc[4]=x;
        b->dc[4]+=(x-b->dc[4])/256;
        sum[i]=((x-b->dc[4])*gain)/256;
    }
    for(c=0;c<4;++c) {
        for(i=0;i<n;++i) {
            /* Weighted unipolar contribution in Q8 PCM units. */
            int32_t x=(int32_t)(voices[c][i]*weight[c]*gain*2u);
            if(!b->started && !i)b->dc[c]=x;
            b->dc[c]+=(x-b->dc[c])/256;
            dry[i]=(int16_t)((x-b->dc[c])/256);
        }
        if(fm1_fx_process(&b->state[c],&b->controls[c],dry,wet,n))return -1;
        for(i=0;i<n;++i)sum[i]+=(int32_t)wet[i]-dry[i];
    }
    if(n)b->started=1;
    for(i=0;i<n;++i)out[i]=clipped(b,sum[i]);
    return fm1_fx_process(&b->state[4],&b->controls[4],out,out,n);
}
