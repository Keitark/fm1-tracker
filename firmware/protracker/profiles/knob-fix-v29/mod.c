#include "mod.h"
#include <string.h>

static unsigned be16(const uint8_t *p) { return ((unsigned)p[0]<<8)|p[1]; }
static void put16(uint8_t *p,unsigned v) { p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v; }
static unsigned sample_header(unsigned sample) { return 20u+(sample-1u)*30u; }
static size_t cell_offset(unsigned pattern,unsigned row,unsigned channel) {
    return 1084u+(size_t)pattern*1024u+(size_t)row*16u+(size_t)channel*4u;
}

int fm1_mod_open(fm1_mod_document *d,size_t length) {
    size_t samples=0,minimum;unsigned i,max=0;
    if(!d || length<2108u || length>FM1_MOD_CAPACITY)return -1;
    if(memcmp(d->bytes+1080,"M.K.",4) && memcmp(d->bytes+1080,"M!K!",4))return -2;
    if(d->bytes[950]<1 || d->bytes[950]>128)return -3;
    for(i=0;i<128;i++) {
        if(d->bytes[952+i]>127)return -4;
        if(d->bytes[952+i]>max)max=d->bytes[952+i];
    }
    if(max>=128)return -4;
    minimum=1084u+(size_t)(max+1u)*1024u;
    if(minimum>length)return -5;
    for(i=1;i<=31;i++) {
        unsigned h=sample_header(i),size=be16(d->bytes+h+22)*2u;
        unsigned loop=be16(d->bytes+h+26)*2u,repeat=be16(d->bytes+h+28)*2u;
        if(d->bytes[h+24]>15 || d->bytes[h+25]>64)return -6;
        if(size && loop>size)return -7;
        if(size && repeat>2 && ((size_t)loop+repeat>size))return -7;
        samples+=size;
    }
    if(samples>length-minimum)return -8;
    d->length=length;d->sample_offset=minimum;d->orders=d->bytes[950];
    d->patterns=(uint16_t)(max+1u);d->dirty=0;d->loaded=1;
    return 0;
}

void fm1_mod_new(fm1_mod_document *d) {
    if(!d)return;
    memset(d,0,sizeof(*d));d->length=2108;d->sample_offset=2108;
    d->bytes[950]=1;memcpy(d->bytes+1080,"M.K.",4);
    d->orders=d->patterns=d->loaded=1;
}

int fm1_mod_cell_get(const fm1_mod_document *d,unsigned pattern,unsigned row,
                     unsigned channel,fm1_mod_cell *cell) {
    const uint8_t *p;
    if(!d || !d->loaded || !cell || pattern>=d->patterns || row>=64 || channel>=4)return -1;
    p=d->bytes+cell_offset(pattern,row,channel);
    cell->sample=(uint8_t)((p[0]&0xf0)|(p[2]>>4));
    cell->period=(uint16_t)(((p[0]&15)<<8)|p[1]);
    cell->effect=p[2]&15;cell->param=p[3];return 0;
}

int fm1_mod_cell_set(fm1_mod_document *d,unsigned pattern,unsigned row,
                     unsigned channel,fm1_mod_cell cell) {
    uint8_t *p;
    if(!d || !d->loaded || pattern>=d->patterns || row>=64 || channel>=4 ||
       cell.sample>31 || cell.period>4095 || cell.effect>15)return -1;
    p=d->bytes+cell_offset(pattern,row,channel);
    p[0]=(uint8_t)((cell.sample&0xf0)|(cell.period>>8));p[1]=(uint8_t)cell.period;
    p[2]=(uint8_t)(((cell.sample&15)<<4)|cell.effect);p[3]=cell.param;d->dirty=1;return 0;
}

int fm1_mod_add_pattern(fm1_mod_document *d) {
    if(!d || !d->loaded || d->patterns>=128 || d->length+1024u>FM1_MOD_CAPACITY)return -1;
    memmove(d->bytes+d->sample_offset+1024u,d->bytes+d->sample_offset,d->length-d->sample_offset);
    memset(d->bytes+d->sample_offset,0,1024u);
    d->sample_offset+=1024u;d->length+=1024u;d->patterns++;
    if(d->patterns>64 && !memcmp(d->bytes+1080,"M.K.",4))memcpy(d->bytes+1080,"M!K!",4);
    d->dirty=1;return d->patterns-1;
}

int fm1_mod_set_order(fm1_mod_document *d,unsigned position,unsigned pattern) {
    if(!d || !d->loaded || position>=128 || pattern>=d->patterns)return -1;
    d->bytes[952+position]=(uint8_t)pattern;
    if(position>=d->orders){d->orders=(uint16_t)(position+1);d->bytes[950]=(uint8_t)d->orders;}
    d->dirty=1;return 0;
}

static int set_name(fm1_mod_document *d,unsigned offset,unsigned size,const char *name) {
    size_t n;
    if(!d || !d->loaded || !name)return -1;
    n=strlen(name);if(n>size)return -1;
    memset(d->bytes+offset,0,size);memcpy(d->bytes+offset,name,n);d->dirty=1;return 0;
}
int fm1_mod_set_title(fm1_mod_document *d,const char *title){return set_name(d,0,20,title);}
int fm1_mod_set_sample_name(fm1_mod_document *d,unsigned sample,const char *name){
    return sample<1 || sample>31?-1:set_name(d,sample_header(sample),22,name);
}
int fm1_mod_set_sample_volume(fm1_mod_document *d,unsigned sample,unsigned volume){
    if(!d || !d->loaded || sample<1 || sample>31 || volume>64)return -1;
    d->bytes[sample_header(sample)+25]=(uint8_t)volume;d->dirty=1;return 0;
}
int fm1_mod_set_sample_finetune(fm1_mod_document *d,unsigned sample,unsigned finetune){
    if(!d || !d->loaded || sample<1 || sample>31 || finetune>15)return -1;
    d->bytes[sample_header(sample)+24]=(uint8_t)finetune;d->dirty=1;return 0;
}
const int8_t *fm1_mod_sample(const fm1_mod_document *d,unsigned sample,size_t *length,
                            size_t *loop_start,size_t *loop_length) {
    size_t offset;unsigned i,h;
    if(!d || !d->loaded || sample<1 || sample>31)return 0;
    offset=d->sample_offset;
    for(i=1;i<sample;i++)offset+=be16(d->bytes+sample_header(i)+22)*2u;
    h=sample_header(sample);
    if(length)*length=be16(d->bytes+h+22)*2u;
    if(loop_start)*loop_start=be16(d->bytes+h+26)*2u;
    if(loop_length)*loop_length=be16(d->bytes+h+28)*2u;
    return (const int8_t *)(d->bytes+offset);
}

/* ProTracker's standard 36-note period table, C-1 through B-3. */
uint16_t fm1_mod_period_for_midi(unsigned midi) {
    static const uint16_t periods[36]={
        856,808,762,720,678,640,604,570,538,508,480,453,
        428,404,381,360,339,320,302,285,269,254,240,226,
        214,202,190,180,170,160,151,143,135,127,120,113};
    if(midi<36 || midi>71)return 0;
    return periods[midi-36];
}

uint32_t fm1_mod_crc32(const uint8_t *bytes,size_t length) {
    uint32_t crc=0xffffffffu;size_t i;unsigned bit;
    for(i=0;i<length;i++){crc^=bytes[i];for(bit=0;bit<8;bit++)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));}
    return crc^0xffffffffu;
}

static uint16_t period_step(uint16_t period,unsigned semitones) {
    unsigned i,best=0,diff=65536;
    for(i=0;i<36;i++){unsigned p=fm1_mod_period_for_midi(i+36),delta=p>period?p-period:period-p;
        if(delta<diff){diff=delta;best=i;}}
    return fm1_mod_period_for_midi(36+(best+semitones>35?35:best+semitones));
}
static void update_step(fm1_mod_voice *v,unsigned period) {
    static const uint32_t tune_q16[16]={65536u,66011u,66489u,66971u,67456u,67945u,68438u,68933u,
                                        61858u,62306u,62757u,63212u,63670u,64132u,64596u,65065u};
    uint64_t step;
    if(period<113)period=113;if(period>856)period=856;
    v->period=(uint16_t)period;
    step=(3546895ull<<16)/((uint64_t)period*FM1_MOD_RATE);
    v->step=(uint32_t)((step*tune_q16[v->finetune&15])>>16);
}
void fm1_mod_player_stop(fm1_mod_player *p){if(p){p->playing=0;p->preview_active=0;memset(p->voices,0,sizeof(p->voices));}}
void fm1_mod_player_start(fm1_mod_player *p,fm1_mod_document *d){
    unsigned ch;
    if(!p)return;memset(p,0,sizeof(*p));p->document=d;p->speed=6;p->bpm=125;
    for(ch=0;ch<4;ch++)p->voices[ch].pan=(ch==0 || ch==3)?0:255;
    p->playing=d && d->loaded;p->tick=0;p->clock=FM1_MOD_RATE*5u;
}
int fm1_mod_preview_start(fm1_mod_player *p,fm1_mod_document *d,
                          unsigned sample,unsigned midi,unsigned channel){
    fm1_mod_voice voice={0};size_t length=0;
    if(!p || !d || !d->loaded || sample<1 || sample>31 || channel>3 ||
       !fm1_mod_period_for_midi(midi))return -1;
    if(!fm1_mod_sample(d,sample,&length,0,0) || !length)return -2;
    voice.finetune=d->bytes[sample_header(sample)+24];
    update_step(&voice,fm1_mod_period_for_midi(midi));
    p->document=d;p->preview_phase=0;p->preview_step=voice.step;
    p->preview_sample=(uint8_t)sample;
    p->preview_volume=d->bytes[sample_header(sample)+25];
    p->preview_channel=(uint8_t)channel;p->preview_active=1;
    return 0;
}
void fm1_mod_preview_stop(fm1_mod_player *p){if(p)p->preview_active=0;}
static void fine_volume(fm1_mod_voice *v,int amount){
    int value=(int)v->volume+amount;
    if(value<0)value=0;if(value>64)value=64;v->volume=(uint8_t)value;
}
static void row_start(fm1_mod_player *p) {
    unsigned ch;fm1_mod_document *d=p->document;
    for(ch=0;ch<4;ch++){
        fm1_mod_cell c;fm1_mod_voice *v=p->voices+ch;
        if(fm1_mod_cell_get(d,d->bytes[952+p->order],p->row,ch,&c))continue;
        v->effect=c.effect;v->param=c.param;v->delayed_period=0;
        if(c.sample){v->sample=c.sample;v->volume=d->bytes[sample_header(c.sample)+25];
            v->finetune=d->bytes[sample_header(c.sample)+24];}
        if(c.period && c.effect==0xe && (c.param>>4)==0xd && (c.param&15)){
            v->delayed_period=c.period;
        } else if(c.period){
            if(c.effect==3 || c.effect==5)v->target_period=c.period;
            else {v->base_period=c.period;v->target_period=0;v->phase=0;v->vibrato_pos=0;
                  v->active=v->sample!=0;update_step(v,c.period);}
        }
        if(v->base_period)update_step(v,v->base_period);
        if(c.effect==9){if(c.param)v->sample_offset=c.param;v->phase=(uint32_t)v->sample_offset*256u<<16;}
        if(c.effect==8)v->pan=c.param;
        if(c.effect==4){
            if(c.param>>4)v->vibrato_speed=c.param>>4;
            if(c.param&15)v->vibrato_depth=c.param&15;
        }
        if(c.effect==0xc)v->volume=c.param>64?64:c.param;
        if(c.effect==0xe){
            unsigned sub=c.param>>4,x=c.param&15;
            if(sub==1 || sub==2){
                int period=(int)v->base_period+(sub==1?-(int)x:(int)x);
                if(period<113)period=113;if(period>856)period=856;
                if(v->base_period){v->base_period=(uint16_t)period;update_step(v,period);}
            }
            if(sub==0xa)fine_volume(v,(int)x);
            if(sub==0xb)fine_volume(v,-(int)x);
            if(sub==0xc && !x)v->volume=0;
            if(sub==0xe && x>p->row_delay)p->row_delay=(uint8_t)x;
        }
        if(c.effect==0xf){if(c.param<=32)p->speed=c.param?c.param:1;else p->bpm=c.param;}
        if(c.effect==0xb){p->jump_order=c.param;p->jump_pending=1;}
        if(c.effect==0xd){p->break_row=(uint8_t)((c.param>>4)*10+(c.param&15));
            if(p->break_row>63)p->break_row=0;p->break_pending=1;}
    }
}
static void volume_slide(fm1_mod_voice *v,unsigned param){
    int value=(int)v->volume+((param>>4)?(int)(param>>4):-(int)(param&15));
    if(value<0)value=0;if(value>64)value=64;v->volume=(uint8_t)value;
}
static void tick_effects(fm1_mod_player *p) {
    static const uint8_t sine[32]={0,24,49,74,97,120,141,161,180,197,212,224,235,244,250,253,
                                   255,253,250,244,235,224,212,197,180,161,141,120,97,74,49,24};
    unsigned ch;
    for(ch=0;ch<4;ch++){
        fm1_mod_voice *v=p->voices+ch;unsigned effect=v->effect,param=v->param;
        if(effect==0 && param && v->base_period){
            unsigned shift=p->tick%3==1?param>>4:p->tick%3==2?param&15:0;
            update_step(v,period_step(v->base_period,shift));
        } else if(effect==1 || effect==2){
            int period=(int)v->base_period+(effect==1?-(int)param:(int)param);
            if(period<113)period=113;if(period>856)period=856;
            v->base_period=(uint16_t)period;update_step(v,period);
        } else if(effect==3 || effect==5){
            unsigned speed=effect==3?param:0;
            if(speed)v->porta_speed=(uint8_t)speed;
            speed=v->porta_speed;
            if(v->target_period && speed){
                int period=v->base_period,target=v->target_period;
                if(period<target){period+=speed;if(period>target)period=target;}
                else if(period>target){period-=speed;if(period<target)period=target;}
                v->base_period=(uint16_t)period;update_step(v,period);
            }
            if(effect==5)volume_slide(v,param);
        } else if(effect==4 || effect==6){
            int wave=sine[v->vibrato_pos&31];int period;
            if(v->vibrato_pos&32)wave=-wave;
            period=(int)v->base_period+((wave*(int)v->vibrato_depth)>>7);
            if(v->base_period)update_step(v,(unsigned)(period<113?113:period));
            v->vibrato_pos=(uint8_t)((v->vibrato_pos+v->vibrato_speed)&63);
            if(effect==6)volume_slide(v,param);
        } else if(effect==0xa)volume_slide(v,param);
        else if(effect==0xe){
            unsigned sub=param>>4,x=param&15;
            if(sub==0x9 && x && p->tick%x==0)v->phase=0;
            if(sub==0xc && p->tick==x)v->volume=0;
            if(sub==0xd && p->tick==x && v->delayed_period){
                v->base_period=v->delayed_period;v->delayed_period=0;
                v->phase=0;v->active=v->sample!=0;update_step(v,v->base_period);
            }
        }
    }
}
static void advance_row(fm1_mod_player *p) {
    unsigned next=p->order;
    if(p->jump_pending || p->break_pending){
        if(p->jump_pending)next=p->jump_order;else next++;
        p->row=p->break_pending?p->break_row:0;
    } else if(++p->row==64){p->row=0;next++;}
    if(next>=p->document->orders)next=p->document->bytes[951]<p->document->orders?p->document->bytes[951]:0;
    p->order=(uint8_t)next;p->jump_pending=p->break_pending=0;
}
static void tick(fm1_mod_player *p){
    if(!p->tick){if(p->delayed_cycle)p->delayed_cycle=0;else row_start(p);}else tick_effects(p);
    if(++p->tick>=p->speed){
        p->tick=0;
        if(p->row_delay){p->row_delay--;p->delayed_cycle=1;}
        else advance_row(p);
    }
}
static int16_t clamp16(int sample){if(sample>32767)return 32767;if(sample<-32768)return -32768;return (int16_t)sample;}
void fm1_mod_render(fm1_mod_player *p,int16_t *stereo,size_t frames){
    size_t i;unsigned ch;uint8_t peaks[4]={0,0,0,0};
    if(!stereo)return;
    for(i=0;i<frames;i++){
        int left=0,right=0;
        unsigned capture=p && !(i&63u);
        unsigned scope_slot=0;
        if(capture){scope_slot=p->scope_head;
            for(ch=0;ch<4;ch++)p->scope[ch][scope_slot]=0;}
        if(p && p->playing && p->document && p->document->loaded){
            p->clock+=p->bpm*2u;
            if(p->clock>=FM1_MOD_RATE*5u){p->clock-=FM1_MOD_RATE*5u;tick(p);}
            for(ch=0;ch<4;ch++){
                fm1_mod_voice *v=p->voices+ch;size_t size=0,loop=0,repeat=0,index;
                const int8_t *sample;
                if(!v->active || !v->step || !v->sample)continue;
                sample=fm1_mod_sample(p->document,v->sample,&size,&loop,&repeat);
                index=(size_t)(v->phase>>16);
                if(index>=size){
                    if(repeat>2 && loop<size){index=loop+(index-loop)%repeat;v->phase=(uint64_t)index<<16;}
                    else {v->active=0;continue;}
                }
                {int value=(int)sample[index]*v->volume;
                    unsigned level=(unsigned)(value<0?-value:value)>>7;
                    if(level>64)level=64;
                    if(level>peaks[ch])peaks[ch]=(uint8_t)level;
                    if(capture)p->scope[ch][scope_slot]=(int8_t)(value/128);
                    left+=value*(255-v->pan)/255;right+=value*v->pan/255;}
                v->phase+=v->step;
            }
        }
        if(p && p->preview_active && p->document && p->document->loaded){
            size_t size=0,loop=0,repeat=0,index;
            const int8_t *sample=fm1_mod_sample(p->document,p->preview_sample,&size,&loop,&repeat);
            index=(size_t)(p->preview_phase>>16);
            if(!sample || !size)p->preview_active=0;
            else {
                if(index>=size){
                    if(repeat>2 && loop<size){index=loop+(index-loop)%repeat;p->preview_phase=(uint64_t)index<<16;}
                    else p->preview_active=0;
                }
                if(p->preview_active){
                    unsigned channel=p->preview_channel;
                    int value=(int)sample[index]*p->preview_volume;
                    unsigned level=(unsigned)(value<0?-value:value)>>7;
                    if(level>64)level=64;
                    if(level>peaks[channel])peaks[channel]=(uint8_t)level;
                    if(capture){int mixed=(int)p->scope[channel][scope_slot]+value/128;
                        if(mixed>127)mixed=127;if(mixed< -128)mixed=-128;
                        p->scope[channel][scope_slot]=(int8_t)mixed;}
                    if(channel==0 || channel==3)left+=value;else right+=value;
                    p->preview_phase+=p->preview_step;
                }
            }
        }
        if(p && p->stereo){stereo[2*i]=clamp16(left);stereo[2*i+1]=clamp16(right);}
        else { /* Mono speaker: every tracker voice reaches both DAC outputs. */
            stereo[2*i]=stereo[2*i+1]=clamp16((left+right)/2);}
        if(capture && ++p->scope_head==48)p->scope_head=0;
    }
    if(p)for(ch=0;ch<4;ch++){
        uint8_t decayed=p->meter[ch]?p->meter[ch]-1:0;
        p->meter[ch]=peaks[ch]>decayed?peaks[ch]:decayed;
    }
}

void fm1_mod_editor_init(fm1_mod_editor *e,fm1_mod_document *d){
    if(!e)return;memset(e,0,sizeof(*e));e->document=d;e->sample=1;e->step=1;e->edit=1;
}
int fm1_mod_editor_note(fm1_mod_editor *e,unsigned midi){
    fm1_mod_cell cell;uint16_t period;unsigned pattern;
    if(!e || !e->edit || !e->document || !e->document->loaded)return -1;
    period=fm1_mod_period_for_midi(midi);if(!period)return -1;
    pattern=e->document->bytes[952+e->order];
    if(fm1_mod_cell_get(e->document,pattern,e->row,e->channel,&cell))return -1;
    cell.period=period;cell.sample=e->sample;
    if(fm1_mod_cell_set(e->document,pattern,e->row,e->channel,cell))return -1;
    e->row=(uint8_t)((e->row+e->step)&63);return 0;
}
int fm1_mod_editor_clear(fm1_mod_editor *e){
    fm1_mod_cell empty={0,0,0,0};
    if(!e || !e->edit || !e->document || !e->document->loaded)return -1;
    return fm1_mod_cell_set(e->document,e->document->bytes[952+e->order],e->row,e->channel,empty);
}
int fm1_mod_editor_effect(fm1_mod_editor *e,unsigned effect,unsigned param){
    fm1_mod_cell cell;unsigned pattern;
    if(!e || !e->edit || !e->document || !e->document->loaded || effect>15 || param>255)return -1;
    pattern=e->document->bytes[952+e->order];
    if(fm1_mod_cell_get(e->document,pattern,e->row,e->channel,&cell))return -1;
    cell.effect=(uint8_t)effect;cell.param=(uint8_t)param;
    return fm1_mod_cell_set(e->document,pattern,e->row,e->channel,cell);
}
void fm1_mod_editor_move(fm1_mod_editor *e,int rows,int channels,int orders){
    if(!e || !e->document || !e->document->loaded)return;
    e->row=(uint8_t)(((int)e->row+rows+6400)%64);
    e->channel=(uint8_t)(((int)e->channel+channels+400)%4);
    e->order=(uint8_t)(((int)e->order+orders+12800)%e->document->orders);
}
static char digit(unsigned x){return "0123456789ABCDEF"[x&15];}
void fm1_mod_format_cell(fm1_mod_cell c,char out[12]){
    static const char *names[12]={"C-","C#","D-","D#","E-","F-","F#","G-","G#","A-","A#","B-"};
    unsigned i,best=0,diff=65536;
    for(i=0;i<36;i++){unsigned p=fm1_mod_period_for_midi(i+36),delta=p>c.period?p-c.period:c.period-p;
        if(delta<diff){diff=delta;best=i;}}
    out[0]=c.period?names[best%12][0]:'-';out[1]=c.period?names[best%12][1]:'-';
    out[2]=c.period?(char)('1'+best/12):'-';out[3]=' ';
    out[4]=digit(c.sample>>4);out[5]=digit(c.sample);out[6]=' ';
    out[7]=digit(c.effect);out[8]=digit(c.param>>4);out[9]=digit(c.param);out[10]=0;
}
