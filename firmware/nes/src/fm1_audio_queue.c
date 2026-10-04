#include "fm1_board.h"
#include <string.h>

void fm1_audio_startup_reset(fm1_audio_startup *s) {
    s->frames_left=44118u;s->gain_q7=0;s->target_q7=127;
}
static void startup_step(fm1_audio_startup *s) {
    if(s->frames_left) {
        s->frames_left=s->frames_left>64 ? s->frames_left-64 : 0;
        s->gain_q7=0;
    } else {
        unsigned target=s->target_q7>127?127:s->target_q7;
        if(s->gain_q7<target)++s->gain_q7;
        else if(s->gain_q7>target)--s->gain_q7;
    }
}
void fm1_audio_startup_process24(fm1_audio_startup *s,int32_t stereo[128]) {
    unsigned i;
    /* FM-1_010 0x020051ce and RAM 0x01c04b2c..0x01c04b5e.
       Clamp the countdown instead of copying stock's signed overshoot. */
    startup_step(s);
    for(i=0;i<128;++i) {
        /* Signed-24 * 127 fits int32. Explicit floor division reproduces
           arithmetic >>7 without an implementation-defined negative shift. */
        int32_t scaled=stereo[i]*(int32_t)s->gain_q7;
        stereo[i]=scaled>=0 ? scaled/128 : -((-scaled+127)/128);
    }
}
void fm1_audio_queue_reset(fm1_audio_queue *q) {memset(q,0,sizeof(*q));}
int fm1_audio_queue_push(fm1_audio_queue *q,const int16_t *p,size_t n) {
    size_t i;
    if((!p && n) || n>FM1_AUDIO_CAPACITY)return FM1_NES_INVALID;
    if(n>FM1_AUDIO_CAPACITY-(uint32_t)(q->write_pos-q->read_pos))return FM1_NES_BUSY;
    for(i=0;i<n;++i)q->mono[(q->write_pos++)&(FM1_AUDIO_CAPACITY-1)]=p[i];
    return 0;
}
void fm1_audio_queue_stereo(fm1_audio_queue *q,int16_t *out,size_t frames) {
    size_t i;
    for(i=0;i<frames;++i) {
        int16_t p=0;
        if(q->read_pos!=q->write_pos)p=q->mono[(q->read_pos++)&(FM1_AUDIO_CAPACITY-1)];
        else ++q->underrun_frames;
        out[i*2]=out[i*2+1]=p;
    }
}
void fm1_audio_queue_stereo24(fm1_audio_queue *q,int32_t *out,size_t frames) {
    size_t i;
    for(i=0;i<frames;++i) {
        int32_t p=0;
        if(q->read_pos!=q->write_pos)
            p=(int32_t)q->mono[(q->read_pos++)&(FM1_AUDIO_CAPACITY-1)]*256;
        else ++q->underrun_frames;
        /* Multiplication avoids undefined left-shift of negative samples. */
        out[i*2]=out[i*2+1]=p;
    }
}
void fm1_audio_queue_play24(fm1_audio_queue *q,fm1_audio_startup *s,int32_t out[128]) {
    uint32_t available=q->write_pos-q->read_pos,read=q->read_pos;
    unsigned i,count;
    startup_step(s); /* Keep the existing wall-clock mute/ramp duration. */
    if(!q->primed) {
        if(available<FM1_AUDIO_PRIME) {
            memset(out,0,128*sizeof(*out));
            if(q->rebuffer_events)q->underrun_frames+=64;
            else q->priming_frames+=64;
            return; /* Keep collected samples until there is a reserve. */
        }
        q->primed=1;
    }
    count=available<64u?(unsigned)available:64u;
    for(i=0;i<count;++i) {
        /* (signed16 *256 *gain_q7)/128 = signed16 *2 *gain_q7,
           exactly, including negatives. One multiply and duplicate instead
           of clearing, expanding and then scaling 128 separate words. */
        int32_t p=(int32_t)q->mono[read++&(FM1_AUDIO_CAPACITY-1)]*(int32_t)(2u*s->gain_q7);
        out[2*i]=out[2*i+1]=p;
    }
    q->read_pos=read;
    for(i=count;i<64;++i)out[2*i]=out[2*i+1]=0;
    if(count<64) {
        q->underrun_frames+=64-count;
        ++q->rebuffer_events;q->primed=0;
    }
}
