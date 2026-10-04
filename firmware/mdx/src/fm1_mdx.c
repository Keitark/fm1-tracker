#include "fm1_mdx.h"
#include <string.h>
static void write_reg(fm1_mdx_player *p,unsigned reg,unsigned value) {
    p->registers[reg]=(uint8_t)value;
    ym2151_write_reg(&p->opm,(int)reg,(int)value);
}
static void live_pitch(fm1_mdx_player *p,unsigned ch) {
    static const uint8_t kc[12]={0,1,2,4,5,6,8,9,10,12,13,14};
    int note=p->live_note[ch]-13;
    int offset=0;
    const retrofm_mdx_track_state *t=&p->sequence.tracks[ch];
    /* Keep the played pitch, with the song's detune/portamento/software LFO.
       MDX note 0 corresponds to OPM C#0 (MIDI 13). */
    if(t->note>=0) {
        int64_t delta=(int64_t)t->pitch-(int64_t)t->note*16384;
        if(t->pitch_lfo.enabled)delta+=t->pitch_lfo.pitch;
        if(delta>1572863)delta=1572863;
        if(delta< -1572863)delta= -1572863;
        offset=(int)delta;
    }
    note=note*16384+offset;
    if(note<0)note=0;
    if(note>96*16384-1)note=96*16384-1;
    write_reg(p,0x28+ch,(unsigned)((note/16384/12)*16+kc[(note/16384)%12]));
    write_reg(p,0x30+ch,(unsigned)((note>>6)&0xfc));
}
void fm1_mdx_song_write(fm1_mdx_player *p,uint8_t reg,uint8_t value) {
    unsigned ch;
    p->song_registers[reg]=value;
    if(reg==8) {
        ch=value&7;
        if(value&0x78)p->key_mask[ch]=(value>>3)&15;
        if(p->mute_mask&(1u<<ch)){p->suppressed_keys++;return;}
    }
    if(reg>=0x28 && reg<=0x37) {
        ch=reg&7;
        if(p->live_note[ch]>=0){live_pitch(p,ch);return;}
    }
    write_reg(p,reg,value);
}
static bool event(void *user,const retrofm_event *e) {
    fm1_mdx_player *p=user;
    if(e->opcode==RETROFM_OP_YM2151)fm1_mdx_song_write(p,e->reg,e->data);
    return true;
}
static bool pcm_event(void *user,const retrofm_mdx_pcm_command *c) {
    fm1_mdx_player *p=user;
    unsigned ch=c->channel;
    retrofm_pcm_result r=RETROFM_PCM_OK;
    if(ch>=8)return false;
    if((p->mute_mask&(1u<<(ch+8))) &&
       (c->opcode==RETROFM_MDX_PCM_PLAY || c->opcode==RETROFM_MDX_PCM_STOP))return true;
    switch(c->opcode) {
    case RETROFM_MDX_PCM_PLAY:r=retrofm_pcm_play(&p->pcm,ch,c->sample_data,c->sample_size,c->frequency,c->volume,c->pan);break;
    case RETROFM_MDX_PCM_STOP:r=retrofm_pcm_stop(&p->pcm,ch);break;
    case RETROFM_MDX_PCM_SET_FREQUENCY:r=retrofm_pcm_set_frequency(&p->pcm,ch,c->frequency);break;
    case RETROFM_MDX_PCM_SET_VOLUME:r=retrofm_pcm_set_volume(&p->pcm,ch,c->volume);break;
    case RETROFM_MDX_PCM_SET_PAN:r=retrofm_pcm_set_pan(&p->pcm,ch,c->pan);break;
    default:return false;
    }
    return r==RETROFM_PCM_OK;
}
void fm1_mdx_stop(fm1_mdx_player *p) {
    unsigned i;
    for(i=0;i<8;i++){write_reg(p,8,i);p->live_note[i]=-1;retrofm_pcm_stop(&p->pcm,i);}
    p->pcm_left=p->pcm_right=0;p->playing=0;
}
int fm1_mdx_load(fm1_mdx_player *p,const uint8_t *mdx,size_t n,const uint8_t *pdx,size_t pn) {
    unsigned i;
    memset(p,0,sizeof(*p));
    for(i=0;i<8;i++){p->live_note[i]=-1;p->key_mask[i]=15;}
    ym2151_init(&p->opm,4000000,FM1_MDX_RATE);
    ym2151_reset_chip(&p->opm);retrofm_pcm_init(&p->pcm);
    p->error=retrofm_mdx_open(&p->mdx,mdx,n);
    if(p->error)return (int)p->error;
    if(p->mdx.uses_pdx) {
        if(!pdx || retrofm_pdx_open(&p->pdx,pdx,pn)!=RETROFM_PDX_OK)
            return (int)(p->error=RETROFM_MDX_MISSING_PDX);
        p->error=retrofm_mdx_sequencer_init_with_pdx(&p->sequence,&p->mdx,&p->pdx,event,p,pcm_event,p);
    } else p->error=retrofm_mdx_sequencer_init(&p->sequence,&p->mdx,event,p);
    p->loaded=p->playing=p->error==RETROFM_MDX_OK;
    return (int)p->error;
}
int fm1_mdx_mute(fm1_mdx_player *p,unsigned track,int mute) {
    unsigned bit;
    if(!p->loaded || track>=p->mdx.track_count)return -1;
    bit=1u<<track;
    if(!!(p->mute_mask&bit)==!!mute)return 0;
    if(track<8) {
        /* One explicit release on each ownership transition; subsequent
           playback key-ons AND key-offs are suppressed while muted. */
        write_reg(p,8,track);p->live_note[track]=-1;
        if(!mute){write_reg(p,0x28+track,p->song_registers[0x28+track]);write_reg(p,0x30+track,p->song_registers[0x30+track]);}
    } else retrofm_pcm_stop(&p->pcm,track-8);
    if(mute)p->mute_mask|=(uint16_t)bit;else p->mute_mask&=(uint16_t)~bit;
    return 0;
}
int fm1_mdx_select(fm1_mdx_player *p,unsigned track) {
    if(!p->loaded || track>=8 || track>=p->mdx.track_count)return -1;
    if(p->selected!=track && p->live_note[p->selected]>=0) {
        write_reg(p,8,p->selected);p->live_note[p->selected]=-1;
    }
    p->selected=(uint8_t)track;return 0;
}
int fm1_mdx_note(fm1_mdx_player *p,unsigned midi,int down) {
    unsigned ch=p->selected,mask=p->key_mask[ch];
    int voice=p->sequence.tracks[ch].voice_number;
    if(!p->loaded || midi<13 || midi>108 || !(p->mute_mask&(1u<<ch)))return -1;
    if(down) {
        if(voice>=0 && voice<256 && p->mdx.voices[voice])mask=p->mdx.voices[voice][2]&15;
        write_reg(p,8,ch);p->live_note[ch]=(int)midi;live_pitch(p,ch);
        write_reg(p,8,(mask<<3)|ch);
    } else if(p->live_note[ch]==(int)midi) {write_reg(p,8,ch);p->live_note[ch]=-1;}
    return 0;
}
static int16_t clamp16(int v){return (int16_t)(v>32767?32767:v< -32768?-32768:v);}
int fm1_mdx_render(fm1_mdx_player *p,int16_t *stereo,size_t frames) {
    size_t i;
    if(!p || !stereo || frames>FM1_MDX_BLOCK)return -1;
    if(!p->loaded){memset(stereo,0,frames*2*sizeof(*stereo));return -1;}
    /* Advance against the DAC sample clock, never LCD/USB wall time. */
    for(i=0;i<frames;i++) {
        SAMP *buffers[2]={p->left,p->right};
        if(p->playing && p->cycles>=p->sequence.current_cycles+p->sequence.cycles_per_tick) {
            p->error=retrofm_mdx_sequencer_tick(&p->sequence);
            if(p->error || p->sequence.ended)fm1_mdx_stop(p);
        }
        ym2151_update_one(&p->opm,buffers,1);
        /* PDX mixer is natively 48kHz; bounded resampling to 44.1kHz. */
        p->pcm_phase+=RETROFM_PCM_OUTPUT_HZ;
        while(p->pcm_phase>=FM1_MDX_RATE) {
            retrofm_pcm_next_frame(&p->pcm,&p->pcm_left,&p->pcm_right);
            p->pcm_phase-=FM1_MDX_RATE;
        }
        /* MAME's YM2151 bit7 is left; MDX bit6 is left. Swap FM only. */
        stereo[2*i]=clamp16(p->right[0]/2+p->pcm_left/2);
        stereo[2*i+1]=clamp16(p->left[0]/2+p->pcm_right/2);
        p->remainder+=RETROFM_PL_CLOCK_HZ;
        p->cycles+=p->remainder/FM1_MDX_RATE;p->remainder%=FM1_MDX_RATE;
    }
    return (int)p->error;
}
