#include "forge.h"
#include <string.h>
#ifdef FM1_TARGET_PI32V2
#define FORGE_AUDIO_STAGE __attribute__((noinline,used))
#else
#define FORGE_AUDIO_STAGE
#endif
static const char *const names[41]={
    "OCT-","OCT+","FX","SCL","ENV","LFO","EDIT","GLO","HOME","SAVE","ARP","SEQ","PLAY","REC",
    "F3","F#3","G3","G#3","A3","A#3","B3","C4","C#4","D4","D#4","E4",
    "F4","F#4","G4","G#4","A4","A#4","B4","C5","C#5","D5","D#5","E5","F5","F#5","G5"};
const char *fm1_forge_key_name(unsigned slot){return slot<41?names[slot]:"?";}
void fm1_forge_controls_init(fm1_forge_controls *s){memset(s,0,sizeof(*s));s->input.last_key=255;s->input.volume_min=1023;}
void fm1_forge_controls_scan(fm1_forge_controls *s,uint64_t keys,uint32_t now){
    unsigned i;keys&=(UINT64_C(1)<<41)-1;s->last_good_ms=now;
    if(!s->initialized){s->candidate=keys;for(i=0;i<41;i++)s->changed_at[i]=now;s->initialized=1;}
    for(i=0;i<41;i++){
        uint64_t bit=UINT64_C(1)<<i;
        if((keys^s->candidate)&bit){s->candidate^=bit;s->changed_at[i]=now;}
        if(((s->input.keys^s->candidate)&bit) && (uint32_t)(now-s->changed_at[i])>=10){
            s->input.keys^=bit;
            if(s->input.keys&bit){s->input.seen|=bit;s->input.last_key=(uint8_t)i;}
        }
    }
    s->input.scans++;
}
void fm1_forge_controls_stale(fm1_forge_controls *s,uint32_t now){
    if(s->initialized && (uint32_t)(now-s->last_good_ms)>=100){s->input.keys=s->candidate=0;s->initialized=0;}
}
void fm1_forge_controls_encoders(fm1_forge_controls *s,const int32_t counts[7],const uint32_t invalid[7]){
    unsigned i;for(i=0;i<7;i++){if(counts[i]!=s->input.encoder[i])s->input.encoder_seen|=(uint8_t)(1u<<i);
        s->input.encoder[i]=counts[i];s->input.encoder_invalid[i]=invalid[i];}
}
void fm1_forge_controls_volume(fm1_forge_controls *s,unsigned raw,unsigned gain,unsigned valid){
    s->input.volume_valid=(uint8_t)(valid && raw<=1023 && gain<=127);
    if(!s->input.volume_valid){s->input.volume_gain=0;return;}
    s->input.volume_raw=(uint16_t)raw;s->input.volume_gain=(uint8_t)gain;
    if(raw<s->input.volume_min)s->input.volume_min=(uint16_t)raw;
    if(raw>s->input.volume_max)s->input.volume_max=(uint16_t)raw;
}
unsigned fm1_forge_note_for_keys(uint64_t keys){unsigned i;for(i=41;i>14;i--)if(keys&(UINT64_C(1)<<(i-1)))return i-1;return 255;}
/* Exact rounded Q32 phase steps for MIDI53..79 at44100Hz. No floating point or
 * division in the sample loop. A held key stops after2 seconds until retrigger. */
static const uint32_t steps[27]={
    17005939,18017165,19088521,20223584,21426141,22700205,24050030,25480119,26995246,
    28600467,30301139,32102938,34011878,36034330,38177043,40447168,42852281,45400411,
    48100060,50960238,53990491,57200933,60602278,64205876,68023757,72068660,76354085};
void fm1_forge_voice_init(fm1_forge_voice *v){memset(v,0,sizeof(*v));v->note=255;}
void fm1_forge_voice_note(fm1_forge_voice *v,unsigned slot){
    if(slot<14 || slot>40){fm1_forge_voice_release(v);return;}
    if(v->note==slot && !v->releasing)return;
    v->note=(uint8_t)slot;v->increment=steps[slot-14];v->frames=0;v->phase=0;v->envelope=0;v->releasing=0;
}
void fm1_forge_voice_release(fm1_forge_voice *v){v->releasing=1;}
FORGE_AUDIO_STAGE void fm1_forge_audio_limit(int32_t *out,unsigned frames){unsigned i;if(!out || frames>64)return;
    for(i=0;i<frames*2;i++){if(out[i]>8388607)out[i]=8388607;else if(out[i]<-8388608)out[i]=-8388608;}}
FORGE_AUDIO_STAGE void fm1_forge_voice_render(fm1_forge_voice *v,int32_t *out,unsigned n){
    unsigned i;if(!out || n>64)return;
    for(i=0;i<n;i++){
        int32_t sample=0;
        if(v->note!=255){
            uint32_t p;int32_t triangle;
            if(v->frames>=FM1_FORGE_TONE_LIMIT)v->releasing=1;
            if(v->releasing){if(v->envelope)v->envelope--;else v->note=255;}
            else if(v->envelope<441)v->envelope++;
            p=v->phase>>16;triangle=p<32768?(int32_t)p*8-131072:393208-(int32_t)p*8;
            sample=triangle*(int32_t)v->envelope/441;v->phase+=v->increment;v->frames++;
        }
        out[2*i]=out[2*i+1]=sample;
    }
}
