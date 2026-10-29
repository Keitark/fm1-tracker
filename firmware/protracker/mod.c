#include "mod.h"
#include <string.h>

static unsigned be16(const uint8_t *p) { return ((unsigned)p[0]<<8)|p[1]; }
static void put16(uint8_t *p,unsigned v) { p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v; }
static unsigned sample_header(unsigned sample) { return 20u+(sample-1u)*30u; }
static size_t cell_offset(unsigned pattern,unsigned row,unsigned channel) {
    return 1084u+(size_t)pattern*1024u+(size_t)row*16u+(size_t)channel*4u;
}
static int document_layout_valid(const fm1_mod_document *d){
    return d && d->loaded && d->orders>=1 && d->orders<=128 &&
        d->patterns>=1 && d->patterns<=128 && d->length<=FM1_MOD_CAPACITY &&
        d->sample_offset==1084u+(size_t)d->patterns*1024u && d->sample_offset<=d->length;
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
    if(!document_layout_valid(d) || pattern>=d->patterns || row>=64 || channel>=4 ||
       cell.sample>31 || cell.period>4095 || cell.effect>15)return -1;
    p=d->bytes+cell_offset(pattern,row,channel);
    {
        uint8_t raw[4]={(uint8_t)((cell.sample&0xf0)|(cell.period>>8)),(uint8_t)cell.period,
            (uint8_t)(((cell.sample&15)<<4)|cell.effect),cell.param};
        if(memcmp(p,raw,4)){memcpy(p,raw,4);d->dirty=1;}
    }
    return 0;
}

int fm1_mod_add_pattern(fm1_mod_document *d) {
    if(!document_layout_valid(d) || d->patterns>=128 || d->orders>=128 ||
       d->length>FM1_MOD_CAPACITY-1024u)return -1;
    memmove(d->bytes+d->sample_offset+1024u,d->bytes+d->sample_offset,d->length-d->sample_offset);
    memset(d->bytes+d->sample_offset,0,1024u);
    d->sample_offset+=1024u;d->length+=1024u;d->patterns++;
    /* MOD has no pattern-count header. A spare order byte retains the highest
     * allocated pattern, including patterns not yet in the song sequence. */
    d->bytes[952+127]=(uint8_t)(d->patterns-1);
    if(d->patterns>64 && !memcmp(d->bytes+1080,"M.K.",4))memcpy(d->bytes+1080,"M!K!",4);
    d->dirty=1;return d->patterns-1;
}
int fm1_mod_duplicate_pattern(fm1_mod_document *d,unsigned pattern){
    int created;
    if(!document_layout_valid(d) || pattern>=d->patterns)return -1;
    created=fm1_mod_add_pattern(d);if(created<0)return -1;
    memcpy(d->bytes+cell_offset((unsigned)created,0,0),d->bytes+cell_offset(pattern,0,0),1024u);
    return created;
}
int fm1_mod_get_order(const fm1_mod_document *d,unsigned position){
    if(!document_layout_valid(d) || position>=d->orders || d->bytes[952+position]>=d->patterns)return -1;
    return d->bytes[952+position];
}
static int commit_orders(fm1_mod_document *d,uint8_t table[128],unsigned count,unsigned restart){
    unsigned i,highest=d->patterns-1,has_highest=0;
    for(i=0;i<128;i++){
        if(table[i]>=d->patterns)return -1;
        if(table[i]==highest)has_highest=1;
    }
    if(!has_highest){if(count>=128)return -1;table[127]=(uint8_t)highest;}
    if(d->orders!=count || d->bytes[951]!=restart || memcmp(d->bytes+952,table,128)){
        memcpy(d->bytes+952,table,128);d->orders=(uint16_t)count;
        d->bytes[950]=(uint8_t)count;d->bytes[951]=(uint8_t)restart;d->dirty=1;
    }
    return 0;
}

int fm1_mod_set_order(fm1_mod_document *d,unsigned position,unsigned pattern) {
    uint8_t table[128];unsigned count;
    if(!document_layout_valid(d) || position>=128 || pattern>=d->patterns)return -1;
    memcpy(table,d->bytes+952,128);table[position]=(uint8_t)pattern;
    count=position>=d->orders?position+1:d->orders;
    return commit_orders(d,table,count,d->bytes[951]);
}
int fm1_mod_insert_order(fm1_mod_document *d,unsigned position,unsigned pattern){
    uint8_t table[128];unsigned restart;
    if(!document_layout_valid(d) || d->orders>=128 || position>d->orders || pattern>=d->patterns)return -1;
    memcpy(table,d->bytes+952,128);
    memmove(table+position+1,table+position,d->orders-position);table[position]=(uint8_t)pattern;
    restart=d->bytes[951];if(restart<d->orders && position<=restart)restart++;
    return commit_orders(d,table,d->orders+1,restart);
}
int fm1_mod_append_order(fm1_mod_document *d,unsigned pattern){
    if(!document_layout_valid(d))return -1;
    return fm1_mod_insert_order(d,d->orders,pattern);
}
int fm1_mod_delete_order(fm1_mod_document *d,unsigned position){
    uint8_t table[128];unsigned restart,count;
    if(!document_layout_valid(d) || d->orders<=1 || position>=d->orders)return -1;
    count=d->orders-1;memcpy(table,d->bytes+952,128);
    memmove(table+position,table+position+1,count-position);table[count]=0;
    restart=d->bytes[951];
    if(restart<d->orders){if(position<restart)restart--;else if(position==restart)restart=position<count?position:0;}
    return commit_orders(d,table,count,restart);
}
int fm1_mod_duplicate_order(fm1_mod_document *d,unsigned position){
    int pattern=fm1_mod_get_order(d,position);
    return pattern<0?-1:fm1_mod_insert_order(d,position+1,(unsigned)pattern);
}

static int set_name(fm1_mod_document *d,unsigned offset,unsigned size,const char *name) {
    size_t n;
    if(!document_layout_valid(d) || !name)return -1;
    n=strlen(name);if(n>size)return -1;
    {
        uint8_t raw[22]={0};memcpy(raw,name,n);
        if(memcmp(d->bytes+offset,raw,size)){memcpy(d->bytes+offset,raw,size);d->dirty=1;}
    }
    return 0;
}
int fm1_mod_set_title(fm1_mod_document *d,const char *title){return set_name(d,0,20,title);}
int fm1_mod_set_sample_name(fm1_mod_document *d,unsigned sample,const char *name){
    return sample<1 || sample>31?-1:set_name(d,sample_header(sample),22,name);
}
int fm1_mod_set_sample_volume(fm1_mod_document *d,unsigned sample,unsigned volume){
    unsigned h;
    if(!document_layout_valid(d) || sample<1 || sample>31 || volume>64)return -1;
    h=sample_header(sample)+25;
    if(d->bytes[h]!=volume){d->bytes[h]=(uint8_t)volume;d->dirty=1;}return 0;
}
int fm1_mod_set_sample_finetune(fm1_mod_document *d,unsigned sample,unsigned finetune){
    unsigned h;
    if(!document_layout_valid(d) || sample<1 || sample>31 || finetune>15)return -1;
    h=sample_header(sample)+24;
    if(d->bytes[h]!=finetune){d->bytes[h]=(uint8_t)finetune;d->dirty=1;}return 0;
}
int fm1_mod_set_sample_loop(fm1_mod_document *d,unsigned sample,size_t start,size_t length){
    unsigned h;size_t size;uint8_t raw[4];
    if(!document_layout_valid(d) || sample<1 || sample>31 || (start&1u) || (length&1u))return -1;
    h=sample_header(sample);size=be16(d->bytes+h+22)*2u;
    if(start>size || length>size-start)return -1;
    put16(raw,(unsigned)(start/2));put16(raw+2,(unsigned)(length/2));
    if(memcmp(d->bytes+h+26,raw,4)){memcpy(d->bytes+h+26,raw,4);d->dirty=1;}
    return 0;
}
int fm1_mod_replace_sample(fm1_mod_document *d,unsigned slot,const char *name,
    const int8_t *pcm,size_t length,unsigned volume,unsigned finetune,
    size_t loop_start,size_t loop_length){
    size_t offset,total,old_length=0,tail,name_length=0;unsigned i,h;
    uint8_t header[30]={0};uintptr_t source,begin,end;
    if(!document_layout_valid(d) || slot<1 || slot>31 || !name ||
       (length && !pcm) || length>131070u || volume>64 || finetune>15 ||
       (length&1u) || (loop_start&1u) || (loop_length&1u) ||
       loop_start>length || loop_length>length-loop_start)return -1;
    while(name_length<23 && name[name_length])name_length++;
    if(name_length>22)return -1;
    offset=total=d->sample_offset;
    for(i=1;i<=31;i++){
        size_t size=be16(d->bytes+sample_header(i)+22)*2u;
        if(size>d->length-total)return -1;
        if(i<slot)offset+=size;if(i==slot)old_length=size;total+=size;
    }
    if(length>FM1_MOD_CAPACITY-(d->length-old_length))return -1;
    /* A resize can move later samples before the final copy. Reject ambiguous
     * aliases transactionally instead of adding another sample-size buffer. */
    source=(uintptr_t)pcm;begin=(uintptr_t)d->bytes;end=begin+sizeof(*d);
    if(length && (source>UINTPTR_MAX-length ||
       (source<end && source+length>begin &&
        (source!=begin+offset || length>old_length))))return -1;
    memcpy(header,name,name_length);put16(header+22,(unsigned)(length/2));
    header[24]=(uint8_t)finetune;header[25]=(uint8_t)volume;
    put16(header+26,(unsigned)(loop_start/2));put16(header+28,(unsigned)(loop_length/2));
    h=sample_header(slot);
    if(length==old_length && !memcmp(d->bytes+h,header,30) &&
       (!length || !memcmp(d->bytes+offset,pcm,length)))return 0;
    tail=d->length-offset-old_length;
    memmove(d->bytes+offset+length,d->bytes+offset+old_length,tail);
    if(length)memmove(d->bytes+offset,pcm,length);
    memcpy(d->bytes+h,header,30);d->length=d->length-old_length+length;d->dirty=1;
    return 0;
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
void fm1_mod_player_stop(fm1_mod_player *p){if(p){p->playing=0;fm1_mod_preview_stop(p);memset(p->voices,0,sizeof(p->voices));}}
void fm1_mod_player_start(fm1_mod_player *p,fm1_mod_document *d){
    unsigned ch;
    if(!p)return;memset(p,0,sizeof(*p));p->document=d;p->speed=6;p->bpm=125;
    for(ch=0;ch<4;ch++)p->voices[ch].pan=(ch==0 || ch==3)?0:255;
    p->playing=d && d->loaded;p->tick=0;p->clock=FM1_MOD_RATE*5u;
}
int fm1_mod_player_start_at(fm1_mod_player *p,fm1_mod_document *d,
                            unsigned order,unsigned row){
    if(!p || !d || !d->loaded || order>=d->orders || row>=64)return -1;
    fm1_mod_player_start(p,d);
    p->order=p->current_order=(uint8_t)order;
    p->row=p->current_row=(uint8_t)row;
    return 0;
}
int fm1_mod_player_start_pattern_at(fm1_mod_player *p,fm1_mod_document *d,
                                    unsigned order,unsigned row){
    if(fm1_mod_player_start_at(p,d,order,row))return -1;
    p->playing=2;return 0;
}
int fm1_mod_preview_start(fm1_mod_player *p,fm1_mod_document *d,
                          unsigned sample,unsigned midi,unsigned channel){
    fm1_mod_voice voice={0};size_t length=0;
    fm1_mod_preview_stop(p);
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
int fm1_mod_preview_pcm(fm1_mod_player *p,const int8_t *pcm,size_t length,
    unsigned volume,unsigned finetune,size_t loop_start,size_t loop_length,
    unsigned midi,unsigned channel){
    fm1_mod_voice voice={0};uint16_t period=fm1_mod_period_for_midi(midi);
    fm1_mod_preview_stop(p);
    if(!p || !pcm || !length || length>131070u || volume>64 || finetune>15 ||
       (length&1u) || (loop_start&1u) || (loop_length&1u) ||
       loop_start>length || loop_length>length-loop_start || !period || channel>3)return -1;
    voice.finetune=(uint8_t)finetune;update_step(&voice,period);
    p->preview_pcm=pcm;p->preview_length=length;
    p->preview_loop_start=loop_start;p->preview_loop_length=loop_length;
    p->preview_phase=0;p->preview_step=voice.step;p->preview_sample=0;
    p->preview_volume=(uint8_t)volume;p->preview_channel=(uint8_t)channel;
    p->preview_active=1;return 0;
}
void fm1_mod_preview_stop(fm1_mod_player *p){if(p){
    p->preview_active=0;p->preview_pcm=0;p->preview_length=0;
    p->preview_loop_start=p->preview_loop_length=0;
}}
static void fine_volume(fm1_mod_voice *v,int amount){
    int value=(int)v->volume+amount;
    if(value<0)value=0;if(value>64)value=64;v->volume=(uint8_t)value;
}
static void row_start(fm1_mod_player *p) {
    unsigned ch;fm1_mod_document *d=p->document;
    p->current_order=p->order;p->current_row=p->row;
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
            if(sub==0x9 && x && p->tick%x==0){v->phase=0;if(v->active)v->active=1;}
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
        if(p->playing!=2){if(p->jump_pending)next=p->jump_order;else next++;}
        p->row=p->break_pending?p->break_row:0;
    } else if(++p->row==64){p->row=0;if(p->playing!=2)next++;}
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
                {size_t end=size;unsigned looping=repeat>2 && loop<size && repeat<=size-loop;
                    /* PT315.S 26473: nonzero loopstart shortens the first DMA
                     * pass to loop end; zero loopstart plays the full first
                     * pass. active=2 marks subsequent loop passes, no new state. */
                    if(looping && (loop || v->active==2))end=loop+repeat;
                    if(index>=end){
                        if(looping){index=loop+(index-loop)%repeat;v->phase=(uint64_t)index<<16;v->active=2;}
                        else {v->active=0;continue;}
                    }
                }
                if(!(p->mute_mask&(1u<<ch))){int value=(int)sample[index]*v->volume;
                    unsigned level=(unsigned)(value<0?-value:value)>>7;
                    if(level>64)level=64;
                    if(level>peaks[ch])peaks[ch]=(uint8_t)level;
                    if(capture)p->scope[ch][scope_slot]=(int8_t)(value/128);
                    left+=value*(255-v->pan)/255;right+=value*v->pan/255;}
                v->phase+=v->step;
            }
        }
        if(p && p->preview_active && (p->preview_pcm || (p->document && p->document->loaded))){
            size_t size=0,loop=0,repeat=0,index;
            const int8_t *sample=p->preview_pcm;
            if(sample){size=p->preview_length;loop=p->preview_loop_start;repeat=p->preview_loop_length;}
            else sample=fm1_mod_sample(p->document,p->preview_sample,&size,&loop,&repeat);
            index=(size_t)(p->preview_phase>>16);
            if(!sample || !size)p->preview_active=0;
            else {
                {size_t end=size;unsigned looping=repeat>2 && loop<size && repeat<=size-loop;
                    if(looping && (loop || p->preview_active==2))end=loop+repeat;
                    if(index>=end){
                        if(looping){index=loop+(index-loop)%repeat;p->preview_phase=(uint64_t)index<<16;p->preview_active=2;}
                        else p->preview_active=0;
                    }
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

const uint8_t fm1_mod_entry_steps[FM1_MOD_ENTRY_STEPS]={0,1,2,3,4,6,8,16};
static unsigned wrapped(unsigned value,int delta,unsigned count){
    int moved=(int)(value%count)+delta%(int)count;
    if(moved<0)moved+=(int)count;else if(moved>=(int)count)moved-=(int)count;
    return (unsigned)moved;
}
static unsigned clamped(unsigned value,int delta,unsigned maximum){
    if(delta<0){unsigned amount=0u-(unsigned)delta;return amount>value?0:value-amount;}
    return (unsigned)delta>maximum-value?maximum:value+(unsigned)delta;
}
static int editor_cell(const fm1_mod_editor *e,unsigned *pattern,fm1_mod_cell *cell,int write){
    int assigned;
    if(!e || (write && !e->edit) || !document_layout_valid(e->document) ||
       e->row>=64 || e->channel>=4)return -1;
    assigned=fm1_mod_get_order(e->document,e->order);if(assigned<0)return -1;
    *pattern=(unsigned)assigned;
    return fm1_mod_cell_get(e->document,*pattern,e->row,e->channel,cell);
}
void fm1_mod_editor_init(fm1_mod_editor *e,fm1_mod_document *d){
    unsigned i;
    if(!e)return;memset(e,0,sizeof(*e));e->document=d;e->sample=1;e->step=1;e->edit=1;
    for(i=0;i<FM1_MOD_QUICK_SLOTS;i++)e->quick[i]=(uint8_t)(i+1);
}
int fm1_mod_editor_note(fm1_mod_editor *e,unsigned midi){
    fm1_mod_cell cell;uint16_t period;unsigned pattern;
    if(editor_cell(e,&pattern,&cell,1) || e->sample<1 || e->sample>31)return -1;
    period=fm1_mod_period_for_midi(midi);if(!period)return -1;
    cell.period=period;cell.sample=e->sample;
    if(fm1_mod_cell_set(e->document,pattern,e->row,e->channel,cell))return -1;
    e->row=(uint8_t)((e->row+e->step)&63);return 0;
}
static int editor_clear_fields(fm1_mod_editor *e,unsigned fields,int write){
    fm1_mod_cell cell;unsigned pattern;int changed=0;
    if(editor_cell(e,&pattern,&cell,write))return -1;
    if(fields&1u){changed=cell.period || cell.sample;cell.period=0;cell.sample=0;}
    if(fields&2u){changed|=cell.effect || cell.param;cell.effect=0;cell.param=0;}
    if(!changed)return 0;
    return fm1_mod_cell_set(e->document,pattern,e->row,e->channel,cell);
}
int fm1_mod_editor_clear_note(fm1_mod_editor *e){return editor_clear_fields(e,1,1);}
int fm1_mod_editor_clear_effect(fm1_mod_editor *e){return editor_clear_fields(e,2,1);}
int fm1_mod_editor_clear_cell(fm1_mod_editor *e){return editor_clear_fields(e,3,1);}
int fm1_mod_editor_clear_all(fm1_mod_editor *e){return editor_clear_fields(e,3,0);}
int fm1_mod_editor_clear(fm1_mod_editor *e){return fm1_mod_editor_clear_cell(e);}
int fm1_mod_editor_effect(fm1_mod_editor *e,unsigned effect,unsigned param){
    fm1_mod_cell cell;unsigned pattern;
    if(effect>15 || param>255 || editor_cell(e,&pattern,&cell,1))return -1;
    cell.effect=(uint8_t)effect;cell.param=(uint8_t)param;
    return fm1_mod_cell_set(e->document,pattern,e->row,e->channel,cell);
}
void fm1_mod_editor_move(fm1_mod_editor *e,int rows,int channels,int orders){
    if(!e || !document_layout_valid(e->document))return;
    e->row=(uint8_t)wrapped(e->row,rows,64);
    e->channel=(uint8_t)wrapped(e->channel,channels,4);
    e->order=(uint8_t)wrapped(e->order,orders,e->document->orders);
}
int fm1_mod_editor_field_move(fm1_mod_editor *e,int delta){
    if(!e || e->field>=FM1_FIELD_COUNT)return -1;
    e->field=(uint8_t)wrapped(e->field,delta,FM1_FIELD_COUNT);return 0;
}
int fm1_mod_editor_set_field(fm1_mod_editor *e,unsigned value){
    fm1_mod_cell cell;unsigned pattern;uint16_t period;
    if(editor_cell(e,&pattern,&cell,0) || e->field>=FM1_FIELD_COUNT)return -1;
    if(e->field==FM1_FIELD_NOTE){period=fm1_mod_period_for_midi(value);if(!period)return -1;cell.period=period;}
    else if(e->field==FM1_FIELD_SAMPLE){if(value>31)return -1;cell.sample=(uint8_t)value;}
    else if(e->field==FM1_FIELD_EFFECT){if(value>15)return -1;cell.effect=(uint8_t)value;}
    else {if(value>255)return -1;cell.param=(uint8_t)value;}
    return fm1_mod_cell_set(e->document,pattern,e->row,e->channel,cell);
}
int fm1_mod_editor_transpose(fm1_mod_editor *e,int semitones){
    fm1_mod_cell cell;unsigned pattern,i,best=0,diff=65536;
    if(editor_cell(e,&pattern,&cell,0))return -1;
    if(!cell.period || !semitones)return 0;
    for(i=0;i<36;i++){
        unsigned period=fm1_mod_period_for_midi(i+36);
        unsigned distance=period>cell.period?period-cell.period:cell.period-period;
        if(distance<diff){diff=distance;best=i;}
    }
    cell.period=fm1_mod_period_for_midi(36+clamped(best,semitones,35));
    return fm1_mod_cell_set(e->document,pattern,e->row,e->channel,cell);
}
int fm1_mod_editor_adjust_field(fm1_mod_editor *e,int delta){
    fm1_mod_cell cell;unsigned pattern,value,maximum;
    if(editor_cell(e,&pattern,&cell,0) || e->field>=FM1_FIELD_COUNT)return -1;
    if(e->field==FM1_FIELD_NOTE)return fm1_mod_editor_transpose(e,delta);
    if(e->field==FM1_FIELD_SAMPLE){value=cell.sample;maximum=31;}
    else if(e->field==FM1_FIELD_EFFECT){value=cell.effect;maximum=15;}
    else {value=cell.param;maximum=255;}
    if(value>maximum)return -1;
    return fm1_mod_editor_set_field(e,clamped(value,delta,maximum));
}
int fm1_mod_editor_param_nibble(fm1_mod_editor *e,unsigned high,int delta){
    fm1_mod_cell cell;unsigned pattern,value;
    if(high>1 || editor_cell(e,&pattern,&cell,0))return -1;
    value=clamped(high?cell.param>>4:cell.param&15,delta,15);
    cell.param=(uint8_t)(high?((cell.param&15)|(value<<4)):((cell.param&0xf0)|value));
    return fm1_mod_cell_set(e->document,pattern,e->row,e->channel,cell);
}
int fm1_mod_editor_clear_field(fm1_mod_editor *e){
    fm1_mod_cell cell;unsigned pattern;
    if(editor_cell(e,&pattern,&cell,0) || e->field>=FM1_FIELD_COUNT)return -1;
    if(e->field==FM1_FIELD_NOTE)cell.period=0;
    else if(e->field==FM1_FIELD_SAMPLE)cell.sample=0;
    else if(e->field==FM1_FIELD_EFFECT)cell.effect=0;
    else cell.param=0;
    return fm1_mod_cell_set(e->document,pattern,e->row,e->channel,cell);
}
static int entry_step_index(unsigned step){
    unsigned i;for(i=0;i<FM1_MOD_ENTRY_STEPS;i++)if(fm1_mod_entry_steps[i]==step)return (int)i;
    return -1;
}
int fm1_mod_editor_set_step(fm1_mod_editor *e,unsigned step){
    if(!e || entry_step_index(step)<0)return -1;
    e->step=(uint8_t)step;return 0;
}
int fm1_mod_editor_step_move(fm1_mod_editor *e,int delta){
    int index;if(!e || (index=entry_step_index(e->step))<0)return -1;
    e->step=fm1_mod_entry_steps[wrapped((unsigned)index,delta,FM1_MOD_ENTRY_STEPS)];return 0;
}
int fm1_mod_editor_quick_bind(fm1_mod_editor *e,unsigned index){
    if(!e || !document_layout_valid(e->document) || index>=FM1_MOD_QUICK_SLOTS ||
       e->sample<1 || e->sample>31)return -1;
    e->quick[index]=e->sample;return 0;
}
int fm1_mod_editor_quick_select(fm1_mod_editor *e,unsigned index){
    if(!e || !document_layout_valid(e->document) || index>=FM1_MOD_QUICK_SLOTS ||
       e->quick[index]<1 || e->quick[index]>31)return -1;
    e->sample=e->quick[index];return 0;
}
int fm1_mod_editor_block_start(fm1_mod_editor *e){
    fm1_mod_cell cell;unsigned pattern;if(editor_cell(e,&pattern,&cell,0))return -1;
    e->block_order=e->order;e->block_anchor_row=e->block_end_row=e->row;
    e->block_anchor_channel=e->block_end_channel=e->channel;e->block_active=1;return 0;
}
int fm1_mod_editor_block_update(fm1_mod_editor *e){
    fm1_mod_cell cell;unsigned pattern;
    if(editor_cell(e,&pattern,&cell,0) || !e->block_active || e->order!=e->block_order)return -1;
    e->block_end_row=e->row;e->block_end_channel=e->channel;return 0;
}
void fm1_mod_editor_block_clear(fm1_mod_editor *e){if(e)e->block_active=0;}
int fm1_mod_editor_block_copy(fm1_mod_editor *e){
    unsigned first_row,last_row,first_channel,last_channel,row,channel,pattern,index=0;int assigned;
    if(!e || !document_layout_valid(e->document) || !e->block_active ||
       e->block_anchor_row>=64 || e->block_end_row>=64 ||
       e->block_anchor_channel>=4 || e->block_end_channel>=4)return -1;
    assigned=fm1_mod_get_order(e->document,e->block_order);if(assigned<0)return -1;
    pattern=(unsigned)assigned;
    first_row=e->block_anchor_row<e->block_end_row?e->block_anchor_row:e->block_end_row;
    last_row=e->block_anchor_row>e->block_end_row?e->block_anchor_row:e->block_end_row;
    first_channel=e->block_anchor_channel<e->block_end_channel?e->block_anchor_channel:e->block_end_channel;
    last_channel=e->block_anchor_channel>e->block_end_channel?e->block_anchor_channel:e->block_end_channel;
    for(row=first_row;row<=last_row;row++)for(channel=first_channel;channel<=last_channel;channel++){
        memcpy(e->clipboard+index,e->document->bytes+cell_offset(pattern,row,channel),4);index+=4;
    }
    e->clipboard_width=(uint8_t)(last_channel-first_channel+1);
    e->clipboard_height=(uint8_t)(last_row-first_row+1);e->clipboard_valid=1;return 0;
}
int fm1_mod_editor_block_paste(fm1_mod_editor *e){
    fm1_mod_cell cell;unsigned pattern,rows,channels,row,ch;
    if(editor_cell(e,&pattern,&cell,0) || !e->clipboard_valid || !e->clipboard_width ||
       e->clipboard_width>4 || !e->clipboard_height || e->clipboard_height>64)return -1;
    rows=e->clipboard_height;if(rows>64u-e->row)rows=64u-e->row;
    channels=e->clipboard_width;if(channels>4u-e->channel)channels=4u-e->channel;
    for(row=0;row<rows;row++)for(ch=0;ch<channels;ch++){
        const uint8_t *src=e->clipboard+(row*e->clipboard_width+ch)*4u;
        uint8_t *dst=e->document->bytes+cell_offset(pattern,e->row+row,e->channel+ch);
        if(memcmp(dst,src,4)){memcpy(dst,src,4);e->document->dirty=1;}
    }
    return 0;
}
static int block_bounds(const fm1_mod_editor *e,unsigned *pattern,unsigned *top,
    unsigned *bottom,unsigned *left,unsigned *right){
    fm1_mod_cell cell;
    if(editor_cell(e,pattern,&cell,0) || !e->block_active || e->block_order!=e->order ||
       e->block_anchor_row>=64 || e->block_end_row>=64 ||
       e->block_anchor_channel>=4 || e->block_end_channel>=4)return -1;
    *top=e->block_anchor_row<e->block_end_row?e->block_anchor_row:e->block_end_row;
    *bottom=e->block_anchor_row>e->block_end_row?e->block_anchor_row:e->block_end_row;
    *left=e->block_anchor_channel<e->block_end_channel?e->block_anchor_channel:e->block_end_channel;
    *right=e->block_anchor_channel>e->block_end_channel?e->block_anchor_channel:e->block_end_channel;
    return 0;
}
static void replace_cell_bytes(fm1_mod_document *d,unsigned pattern,unsigned row,
    unsigned channel,const uint8_t raw[4]){
    uint8_t *dst=d->bytes+cell_offset(pattern,row,channel);
    if(memcmp(dst,raw,4)){memmove(dst,raw,4);d->dirty=1;}
}
int fm1_mod_editor_block_clear_cells(fm1_mod_editor *e){
    static const uint8_t empty[4]={0};unsigned pattern,top,bottom,left,right,row,ch;
    if(block_bounds(e,&pattern,&top,&bottom,&left,&right))return -1;
    for(row=top;row<=bottom;row++)for(ch=left;ch<=right;ch++)replace_cell_bytes(e->document,pattern,row,ch,empty);
    return 0;
}
int fm1_mod_editor_block_cut(fm1_mod_editor *e){
    unsigned pattern,top,bottom,left,right;
    if(block_bounds(e,&pattern,&top,&bottom,&left,&right))return -1;
    if(fm1_mod_editor_block_copy(e))return -1;
    return fm1_mod_editor_block_clear_cells(e);
}
int fm1_mod_editor_block_transpose(fm1_mod_editor *e,int semitones){
    unsigned pattern,top,bottom,left,right,row,ch,i;
    if(block_bounds(e,&pattern,&top,&bottom,&left,&right))return -1;
    if(!semitones)return 0;
    for(row=top;row<=bottom;row++)for(ch=left;ch<=right;ch++){
        uint8_t raw[4];unsigned period,best=0,diff=65536;
        memcpy(raw,e->document->bytes+cell_offset(pattern,row,ch),4);
        period=((unsigned)(raw[0]&15)<<8)|raw[1];if(!period)continue;
        for(i=0;i<36;i++){
            unsigned note=fm1_mod_period_for_midi(i+36),distance=note>period?note-period:period-note;
            if(distance<diff){diff=distance;best=i;}
        }
        period=fm1_mod_period_for_midi(36+clamped(best,semitones,35));
        raw[0]=(uint8_t)((raw[0]&0xf0)|(period>>8));raw[1]=(uint8_t)period;
        replace_cell_bytes(e->document,pattern,row,ch,raw);
    }
    return 0;
}
int fm1_mod_editor_block_fill(fm1_mod_editor *e,unsigned step){
    unsigned pattern,top,bottom,left,right,row,ch;uint8_t source[4];
    if(step>64 || block_bounds(e,&pattern,&top,&bottom,&left,&right))return -1;
    if(!step)step=1;
    memcpy(source,e->document->bytes+cell_offset(pattern,e->block_anchor_row,e->block_anchor_channel),4);
    for(row=top;row<=bottom;row+=step)for(ch=left;ch<=right;ch++)replace_cell_bytes(e->document,pattern,row,ch,source);
    return 0;
}
int fm1_mod_editor_insert_row(fm1_mod_editor *e){
    static const uint8_t empty[4]={0};fm1_mod_cell cell;unsigned pattern,row;
    if(editor_cell(e,&pattern,&cell,0))return -1;
    for(row=63;row>e->row;row--)replace_cell_bytes(e->document,pattern,row,e->channel,
        e->document->bytes+cell_offset(pattern,row-1,e->channel));
    replace_cell_bytes(e->document,pattern,e->row,e->channel,empty);return 0;
}
int fm1_mod_editor_delete_row(fm1_mod_editor *e){
    static const uint8_t empty[4]={0};fm1_mod_cell cell;unsigned pattern,row;
    if(editor_cell(e,&pattern,&cell,0))return -1;
    for(row=e->row;row<63;row++)replace_cell_bytes(e->document,pattern,row,e->channel,
        e->document->bytes+cell_offset(pattern,row+1,e->channel));
    replace_cell_bytes(e->document,pattern,63,e->channel,empty);return 0;
}
int fm1_mod_editor_sync_player(fm1_mod_editor *e,const fm1_mod_player *p){
    if(!e || !p || e->document!=p->document || p->current_row>=64 ||
       fm1_mod_get_order(e->document,p->current_order)<0)return -1;
    e->order=p->current_order;e->row=p->current_row;return 0;
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
