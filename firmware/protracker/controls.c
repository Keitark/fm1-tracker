#include "controls.h"
#include <string.h>

static const uint8_t quick_keys[8]={0,1,2,3,4,5,10,11};
static fm1_tracker_control_event event(unsigned kind,unsigned index,unsigned sample){
    fm1_tracker_control_event result={(uint8_t)kind,(uint8_t)index,(uint8_t)sample};
    return result;
}
void fm1_tracker_controls_init(fm1_tracker_controls *c){memset(c,0,sizeof(*c));}
int fm1_tracker_controls_held(const fm1_tracker_controls *c,unsigned slot){
    return slot<14 && (c->held&(1u<<slot));
}
void fm1_tracker_controls_use_seq(fm1_tracker_controls *c){c->seq_used=1;}
void fm1_tracker_controls_use_save(fm1_tracker_controls *c){c->save_used=1;}
fm1_tracker_control_event fm1_tracker_controls_key(fm1_tracker_controls *c,
    unsigned slot,unsigned down,uint32_t now,unsigned current_sample){
    unsigned i;
    if(slot>=14)return down?event(FM1_CONTROL_KEY,slot,0):event(0,0,0);
    if(down){
        if(c->held&(1u<<slot))return event(0,0,0);
        c->held|=(uint16_t)(1u<<slot);
        if(slot==6){c->edit_used=0;return event(0,0,0);}
        if(slot==9){c->save_used=0;return event(0,0,0);}
        for(i=0;i<8;i++)if(slot==quick_keys[i] && fm1_tracker_controls_held(c,6)){
            c->edit_used=1;c->quick_pending[i]=1;c->quick_bound[i]=0;
            c->quick_sample[i]=(uint8_t)current_sample;c->quick_since[i]=now;
            return event(0,0,0);
        }
        if(slot==11){c->seq_used=0;return event(0,0,0);}
        if(slot==13 && fm1_tracker_controls_held(c,11)){
            c->seq_used=1;c->new_pending=1;c->new_fired=0;c->new_since=now;
            return event(0,0,0);
        }
        if(fm1_tracker_controls_held(c,11))c->seq_used=1;
        return event(FM1_CONTROL_KEY,slot,0);
    }
    if(!(c->held&(1u<<slot)))return event(0,0,0);
    c->held&=(uint16_t)~(1u<<slot);
    if(slot==11 || slot==13){
        if(c->new_pending && !c->new_fired && (uint32_t)(now-c->new_since)>=600u){
            c->new_pending=0;return event(FM1_CONTROL_NEW_SONG,0,0);
        }
        c->new_pending=0;
    }
    for(i=0;i<8;i++)if(slot==quick_keys[i] && c->quick_pending[i]){
        c->quick_pending[i]=0;
        if(c->quick_bound[i])return event(0,0,0);
        /* A long hold can end between polling iterations. */
        if((uint32_t)(now-c->quick_since[i])>=600u)
            return event(FM1_CONTROL_QUICK_BIND,i,c->quick_sample[i]);
        return event(FM1_CONTROL_QUICK_SELECT,i,0);
    }
    if(slot==6)return c->edit_used?event(0,0,0):event(FM1_CONTROL_KEY,6,0);
    if(slot==9)return c->save_used?event(0,0,0):event(FM1_CONTROL_KEY,9,0);
    if(slot==11)return c->seq_used?event(0,0,0):event(FM1_CONTROL_KEY,11,0);
    return event(0,0,0);
}
fm1_tracker_control_event fm1_tracker_controls_tick(fm1_tracker_controls *c,uint32_t now){
    unsigned i;
    if(c->new_pending && !c->new_fired && (uint32_t)(now-c->new_since)>=600u){
        c->new_fired=1;return event(FM1_CONTROL_NEW_SONG,0,0);
    }
    for(i=0;i<8;i++)if(c->quick_pending[i] && !c->quick_bound[i] &&
        (uint32_t)(now-c->quick_since[i])>=600u){
        c->quick_bound[i]=1;
        return event(FM1_CONTROL_QUICK_BIND,i,c->quick_sample[i]);
    }
    return event(0,0,0);
}
int fm1_tracker_controls_encoder(fm1_tracker_controls *c,fm1_mod_editor *e,
    unsigned running,unsigned id,int detents){
    if(!detents || id>6)return -1;
    if(id==0 || id==2){
        if(id==2 && running)return 0;
        if(!running && fm1_tracker_controls_held(c,11) && !fm1_tracker_controls_held(c,6)){
            if(!c->seq_used){fm1_mod_editor_block_start(e);c->seq_used=1;}
            fm1_mod_editor_move(e,id==2?detents:0,id==0?detents:0,0);
            fm1_mod_editor_block_update(e);
        }else fm1_mod_editor_move(e,id==2?detents:0,id==0?detents:0,0);
    }else if(id==1){if(!running)fm1_mod_editor_move(e,0,0,detents);}
    else if(id==3)fm1_mod_editor_field_move(e,detents);
    else if(id==6){unsigned value=e->sample;
        if(detents<0){unsigned amount=0u-(unsigned)detents;
            value=amount>=value?1u:value-amount;
        }else value=(unsigned)detents>31u-value?31u:value+(unsigned)detents;
        e->sample=(uint8_t)value;
    }else if(!running && id==4){
        return e->field==FM1_FIELD_PARAM?fm1_mod_editor_param_nibble(e,0,detents):
            fm1_mod_editor_adjust_field(e,detents);
    }else if(!running && id==5){
        return e->field==FM1_FIELD_PARAM?fm1_mod_editor_param_nibble(e,1,detents):
            fm1_mod_editor_step_move(e,detents);
    }
    return 0;
}
