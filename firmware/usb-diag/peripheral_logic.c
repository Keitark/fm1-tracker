#include "peripheral_logic.h"
/* FM-1_010 XIP 0x020462fc, seven A/B row/column pairs. ISR 0x01c046e4
 * combines active-low A as bit0, B as bit1; 0x2814/0x4182 direction masks.
 * Counts are contact edges, NOT detents or established clockwise direction. */
const uint8_t fm1_encoder_contacts[14]={0x10,0x20,0x30,0x40,0x91,0xa1,0x90,0xa0,0x70,0x80,0x50,0x60,0x15,0x25};
/* One-at-a-time physical tests, 2026-09-29; do not infer names from row order. */
const char *const fm1_encoder_names[7]={"SELECT","ALGORITHM","KNOB1","KNOB2","KNOB3","KNOB4","PRESETS"};
void fm1_encoders_sample(fm1_encoders *s,const uint8_t rows[11]) {
    unsigned i;
    for(i=0;i<7;i++) {
        unsigned a=fm1_encoder_contacts[2*i],b=fm1_encoder_contacts[2*i+1];
        unsigned value=!(rows[a>>4]&(1u<<(a&15))) | ((! (rows[b>>4]&(1u<<(b&15))))<<1);
        unsigned transition=(s->previous[i]<<2)|value;
        /* Stock 0x01c04750..a6: first differing sample updates candidate only.
           Decode only on its next identical scan. No guessed missing edges. */
        if(!s->valid) {
            s->previous[i]=s->candidate[i]=(uint8_t)value;
            continue;
        }
        if(value!=s->candidate[i]) {
            s->candidate[i]=(uint8_t)value;
            continue;
        }
        if(s->valid && value!=s->previous[i]) {
            if((value^s->previous[i])==3)++s->invalid[i];
            else if((0x2814u>>transition)&1u) {if(s->count[i]<2147483647)s->count[i]++;}
            else if((0x4182u>>transition)&1u) {if(s->count[i]>(-2147483647-1))s->count[i]--;}
        }
        s->previous[i]=(uint8_t)value;
    }
    s->valid=1;
}
void fm1_encoders_capture(fm1_encoders *s,uint32_t *last_sequence,
                         uint32_t sequence,const uint8_t rows[11]) {
    if(sequence==*last_sequence)return;
    fm1_encoders_sample(s,rows);*last_sequence=sequence;
}
int fm1_encoder_detents(int32_t count,int32_t *previous,int32_t *remainder) {
    int64_t difference=(int64_t)count-*previous;
    int edges,steps;
    if(difference>8)difference=8;if(difference< -8)difference=-8;
    *previous+=(int32_t)difference;
    edges=*remainder+(int)difference;steps=edges/4;*remainder=edges%4;
    return steps;
}
int32_t fm1_test_sample(uint32_t frame) {
    uint32_t t,phase,gain=441;
    int32_t wave;
    /* 1 second digital silence, 2 seconds triangle at 441 Hz, 10 ms ramps.
     * Signed low-24-bit format; peak 65536 / 8388607, about -42 dBFS. */
    if(frame<44100 || frame>=FM1_TONE_FRAMES)return 0;
    t=frame-44100;phase=t%100;
    wave=phase<50 ? (int32_t)phase*2621-65536 : 65536-(int32_t)(phase-50)*2621;
    if(t<gain)gain=t;
    if(FM1_TONE_FRAMES-1-frame<gain)gain=FM1_TONE_FRAMES-1-frame;
    return wave*(int32_t)gain/441;
}
