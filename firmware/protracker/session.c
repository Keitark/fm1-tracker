#include "session.h"
#include <stdio.h>
#include <string.h>

static void flush(fm1_tracker_session *s){if(s->flush)s->flush(s->flush_context);}
static void quiet(fm1_tracker_session *s){fm1_mod_preview_stop(&s->player);if(!s->running)flush(s);}
static int editable(const fm1_tracker_session *s){return s->song && s->song->loaded && !s->running && !s->busy;}
void fm1_tracker_session_notice(fm1_tracker_session *s,const char *text,uint32_t now){
    snprintf(s->view_context.notice,sizeof(s->view_context.notice),"%.39s",text);
    s->notice_until=now+1200u;
}
static void counts(fm1_tracker_session *s){
    s->view_context.undo_count=(uint8_t)(s->history?fm1_mod_history_undo_count(s->history):0);
    s->view_context.redo_count=(uint8_t)(s->history?fm1_mod_history_redo_count(s->history):0);
}
static void mix(fm1_tracker_session *s){
    s->player.stereo=s->stereo;
    s->player.mute_mask=(uint8_t)((s->solo_mask?~s->solo_mask:s->mute_mask)&15u);
    s->view_context.mute_mask=s->player.mute_mask;
    s->view_context.solo_mask=s->solo_mask;
}
void fm1_tracker_session_stop(fm1_tracker_session *s){
    if(s->player.playing)fm1_mod_editor_sync_player(&s->editor,&s->player);
    fm1_mod_player_stop(&s->player);s->running=0;s->editor.edit=0;flush(s);
}
void fm1_tracker_session_reset_after_load(fm1_tracker_session *s,uint32_t now){
    fm1_mod_player_stop(&s->player);flush(s);memset(&s->player,0,sizeof(s->player));
    s->player.document=s->song;
    fm1_mod_editor_init(&s->editor,s->song);s->editor.octave=-1;s->editor.edit=0;
    if(s->song->loaded && !memcmp(s->song->bytes,"FM1 ORIGINAL BREAK",18)){
        static const uint8_t quick[8]={1,3,6,7,17,20,13,27};
        memcpy(s->editor.quick,quick,sizeof(quick));
    }
    fm1_tracker_controls_init(&s->gestures);memset(&s->view_context,0,sizeof(s->view_context));
    s->page=s->running=s->mute_mask=s->solo_mask=0;mix(s);
    if(s->history)fm1_mod_history_init(s->history,s->song);
    counts(s);fm1_tracker_session_notice(s,"MOD LOADED",now);
}
void fm1_tracker_session_init(fm1_tracker_session *s,fm1_mod_document *song,
    fm1_mod_library *library,const fm1_bank *bank,fm1_mod_history *history,
    void (*callback)(void *),void *context){
    memset(s,0,sizeof(*s));s->song=song;s->library=library;s->bank=bank;
    s->history=history;s->flush=callback;s->flush_context=context;
    fm1_tracker_session_reset_after_load(s,0);s->view_context.notice[0]=0;
}
int fm1_tracker_session_play(fm1_tracker_session *s,unsigned pattern,unsigned from_cursor,uint32_t now){
    int rc;
    if(s->busy || !s->song || !s->song->loaded){fm1_tracker_session_notice(s,"LOAD OR CREATE A SONG",now);return -1;}
    quiet(s);flush(s);
    rc=pattern?fm1_mod_player_start_pattern_at(&s->player,s->song,s->editor.order,from_cursor?s->editor.row:0):
        fm1_mod_player_start_at(&s->player,s->song,s->editor.order,from_cursor?s->editor.row:0);
    if(rc){fm1_tracker_session_notice(s,"INVALID PLAY POSITION",now);return rc;}
    s->running=1;s->editor.edit=0;s->page=0;
    s->view_context.arranger=s->view_context.bank_open=s->view_context.sample_open=0;
    s->view_context.help_open=s->view_context.confirm=0;mix(s);return 0;
}
static void new_song(fm1_tracker_session *s,uint32_t now){
    fm1_tracker_session_stop(s);fm1_mod_new(s->song);s->song->dirty=1;
    fm1_tracker_session_reset_after_load(s,now);fm1_tracker_session_notice(s,"NEW EMPTY SONG",now);
}
int fm1_tracker_session_action(fm1_tracker_session *s,unsigned op,uint32_t now){
    if(op==FM1_MOD_REQUEST_STOP){fm1_tracker_session_stop(s);return 0;}
    if(s->busy)return -1;
    if(op==FM1_MOD_REQUEST_NEW){
        if(s->running){fm1_tracker_session_notice(s,"STOP FIRST FOR NEW SONG",now);return -1;}
        if(s->song->loaded && s->song->dirty){
            quiet(s);s->view_context.confirm=FM1_VIEW_CONFIRM_NEW;
            fm1_tracker_session_notice(s,"NEW: REC YES / EDIT CANCEL",now);return 1;
        }
        new_song(s,now);return 0;
    }
    if(op==FM1_MOD_REQUEST_PLAY || op==FM1_MOD_REQUEST_PATTERN)
        return fm1_tracker_session_play(s,op==FM1_MOD_REQUEST_PATTERN,0,now);
    return -1;
}
static int begin(fm1_tracker_session *s,unsigned scope,uint32_t now){
    if(!editable(s)){fm1_tracker_session_notice(s,"STOP FIRST TO EDIT",now);return -1;}
    quiet(s);
    if(s->history && fm1_mod_history_begin(s->history,&s->editor,scope)){
        fm1_tracker_session_notice(s,"EDIT HISTORY UNAVAILABLE",now);return -1;
    }
    return 0;
}
static int finish(fm1_tracker_session *s,int rc,const char *label,uint32_t now){
    int recorded=0;
    if(s->history){if(rc)fm1_mod_history_cancel(s->history);else recorded=fm1_mod_history_commit(s->history,&s->editor);}
    counts(s);fm1_tracker_session_notice(s,rc?"EDIT UNAVAILABLE / FULL":recorded<0?"EDIT DONE / HISTORY RESET":label,now);return rc;
}
static void sample_notice(fm1_tracker_session *s,unsigned quick,const char *verb,uint32_t now){
    char text[40];unsigned h=20u+(s->editor.sample-1u)*30u;
    if(quick<8)snprintf(text,sizeof(text),"Q%u %s S%02X %.12s",quick+1,verb,s->editor.sample,(const char *)s->song->bytes+h);
    else snprintf(text,sizeof(text),"S%02X %.22s",s->editor.sample,(const char *)s->song->bytes+h);
    fm1_tracker_session_notice(s,text,now);
}
static void audition(fm1_tracker_session *s,unsigned midi){
    if(!s->running)flush(s);
    fm1_mod_preview_start(&s->player,s->song,s->editor.sample,midi,s->editor.channel);
}
static int bank_audition(fm1_tracker_session *s,unsigned midi){
    fm1_bank_entry entry;
    if(!s->bank || fm1_bank_entry_get(s->bank,s->view_context.bank_index,&entry))return -1;
    snprintf(s->view_context.bank_name,sizeof(s->view_context.bank_name),"%.19s",entry.name);
    if(!s->running)flush(s);
    return fm1_mod_preview_pcm(&s->player,entry.pcm,entry.length,entry.volume,
        (unsigned)entry.finetune&15u,entry.loop_start,entry.loop_length,midi,s->editor.channel);
}
static void import_bank(fm1_tracker_session *s,unsigned index,unsigned sample,uint32_t now){
    int rc;
    if(!editable(s)){fm1_tracker_session_notice(s,"STOP FIRST TO COPY BANK",now);return;}
    quiet(s);rc=fm1_bank_import(s->bank,index,s->song,sample);
    if(!rc){
        if(s->history)fm1_mod_history_init(s->history,s->song);
        s->editor.sample=(uint8_t)sample;s->view_context.bank_open=0;
        fm1_tracker_session_notice(s,"BANK COPIED / HISTORY RESET",now);
    }else fm1_tracker_session_notice(s,"BANK COPY FULL / ERROR",now);
    s->view_context.confirm=0;counts(s);
}
static void bank_copy(fm1_tracker_session *s,uint32_t now){
    size_t size;
    if(!editable(s)){fm1_tracker_session_notice(s,"STOP FIRST TO COPY BANK",now);return;}
    fm1_mod_sample(s->song,s->editor.sample,&size,0,0);
    if(size || (s->history && (fm1_mod_history_undo_count(s->history) || fm1_mod_history_redo_count(s->history)))){
        quiet(s);s->confirm_bank_index=s->view_context.bank_index;s->confirm_sample=s->editor.sample;
        s->view_context.confirm=FM1_VIEW_CONFIRM_BANK;
        fm1_tracker_session_notice(s,size?"REPLACE: FX YES / EDIT CANCEL":"IMPORT CLEARS HISTORY: FX YES",now);
    }else import_bank(s,s->view_context.bank_index,s->editor.sample,now);
}
static void save(fm1_tracker_session *s,uint32_t now){
    unsigned write=s->editor.edit,was_running=s->running;
    if(!s->song->loaded){fm1_tracker_session_notice(s,"NO SONG TO SAVE",now);return;}
    fm1_tracker_session_stop(s);if(!was_running)s->editor.edit=(uint8_t)write;
    s->page=0;s->view_context.arranger=s->view_context.bank_open=s->view_context.sample_open=0;
    if(!s->library || fm1_mod_library_save(s->library))fm1_tracker_session_notice(s,"CONNECT PC LIBRARY TO SAVE",now);
    else {
        s->library->last_ms=now;s->library_ticket=s->library->ticket;
        s->library_pending=s->library_saving=1;
        fm1_tracker_session_notice(s,"SAVE REQUESTED FROM PC",now);
    }
}
static void history_move(fm1_tracker_session *s,unsigned redo,uint32_t now){
    int rc;
    if(!editable(s)){fm1_tracker_session_notice(s,"STOP FIRST FOR UNDO / REDO",now);return;}
    quiet(s);rc=s->history?(redo?fm1_mod_history_redo(s->history,&s->editor):fm1_mod_history_undo(s->history,&s->editor)):-1;
    counts(s);fm1_tracker_session_notice(s,rc?(redo?"NOTHING TO REDO":"NOTHING TO UNDO"):(redo?"REDO":"UNDO"),now);
}
static void mute(fm1_tracker_session *s,unsigned solo,uint32_t now){
    uint8_t bit=(uint8_t)(1u<<s->editor.channel);char text[40];
    if(solo)s->solo_mask=s->solo_mask==bit?0:bit;else s->mute_mask^=bit;
    mix(s);snprintf(text,sizeof(text),"CH%u %s",s->editor.channel+1,
        solo?(s->solo_mask?"SOLO":"SOLO OFF"):(s->mute_mask&bit?"MUTED":"UNMUTED"));
    fm1_tracker_session_notice(s,text,now);
}
static void block_action(fm1_tracker_session *s,unsigned slot,uint32_t now){
    int rc;const char *label;
    fm1_tracker_controls_use_seq(&s->gestures);
    if(!editable(s)){fm1_tracker_session_notice(s,"STOP FIRST FOR BLOCK EDIT",now);return;}
    if(slot==3){rc=fm1_mod_editor_block_copy(&s->editor);fm1_tracker_session_notice(s,rc?"SELECT A BLOCK FIRST":"BLOCK COPIED",now);return;}
    if(begin(s,FM1_HISTORY_PATTERN,now))return;
    if(slot==5){rc=fm1_mod_editor_block_paste(&s->editor);label="BLOCK PASTED";}
    else if(slot==2){rc=fm1_mod_editor_block_cut(&s->editor);label="BLOCK CUT";}
    else if(slot==4){rc=fm1_mod_editor_block_fill(&s->editor,s->editor.step?s->editor.step:1);label="BLOCK FILLED";}
    else if(slot==0 || slot==1){rc=fm1_mod_editor_block_transpose(&s->editor,slot==0?-1:1);label="BLOCK TRANSPOSED";}
    else if(slot==10){rc=fm1_mod_editor_insert_row(&s->editor);label="ROW INSERTED";}
    else {rc=fm1_mod_editor_delete_row(&s->editor);label="ROW DELETED";}
    finish(s,rc,label,now);
}
static void order_action(fm1_tracker_session *s,unsigned slot,uint32_t now){
    int rc=-1,pattern,added;
    if(slot!=2 && slot!=3 && slot!=4 && slot!=5 && slot!=10 && slot!=11)return;
    if(begin(s,slot==10 || slot==11?FM1_HISTORY_STRUCTURE:FM1_HISTORY_HEADER,now))return;
    pattern=fm1_mod_get_order(s->song,s->editor.order);
    if(slot==2){rc=fm1_mod_append_order(s->song,(unsigned)pattern);if(!rc)s->editor.order=(uint8_t)(s->song->orders-1);}
    else if(slot==4)rc=fm1_mod_insert_order(s->song,s->editor.order,(unsigned)pattern);
    else if(slot==3){rc=fm1_mod_delete_order(s->song,s->editor.order);if(!rc && s->editor.order>=s->song->orders)s->editor.order=(uint8_t)(s->song->orders-1);}
    else if(slot==5){rc=fm1_mod_duplicate_order(s->song,s->editor.order);if(!rc)s->editor.order++;}
    else {
        added=slot==10?fm1_mod_add_pattern(s->song):fm1_mod_duplicate_pattern(s->song,(unsigned)pattern);
        rc=added<0?-1:fm1_mod_set_order(s->song,s->editor.order,(unsigned)added);
    }
    if(!rc)fm1_mod_editor_block_clear(&s->editor);
    finish(s,rc,slot==10?"NEW BLANK PATTERN":slot==11?"PATTERN DUPLICATED":"ORDER UPDATED",now);
}
static void key_action(fm1_tracker_session *s,unsigned slot,uint32_t now){
    unsigned save_held=(unsigned)fm1_tracker_controls_held(&s->gestures,9);
    if(slot==6){
        if(s->view_context.confirm){s->view_context.confirm=0;fm1_tracker_session_notice(s,"CANCELLED",now);return;}
        if(s->view_context.help_open){s->view_context.help_open=0;return;}
        fm1_tracker_session_stop(s);s->page=0;
        s->view_context.arranger=s->view_context.bank_open=s->view_context.sample_open=0;
        fm1_tracker_session_notice(s,"EDIT / AUDITION",now);return;
    }
    if(s->busy)return;
    if(s->view_context.confirm){
        if(s->view_context.confirm==FM1_VIEW_CONFIRM_NEW && slot==13)new_song(s,now);
        else if(s->view_context.confirm==FM1_VIEW_CONFIRM_BANK && slot==2)
            import_bank(s,s->confirm_bank_index,s->confirm_sample,now);
        return;
    }
    if(save_held && slot!=9){
        fm1_tracker_controls_use_save(&s->gestures);
        if(slot==0 || slot==1)history_move(s,slot==1,now);
        else if(slot==8){
            if(!s->view_context.help_open)s->view_context.help_topic=(uint8_t)
                (s->page==2 || s->view_context.bank_open?2:s->view_context.arranger?3:0);
            s->view_context.help_open^=1;s->view_context.notice[0]=0;
        }
        else if(slot==12){fm1_tracker_session_stop(s);s->page=1;s->view_context.arranger=s->view_context.bank_open=s->view_context.sample_open=s->view_context.help_open=0;}
        else if(slot==7)fm1_tracker_session_play(s,1,1,now);
        else if(slot==3 || slot==5)mute(s,slot==5,now);
        return;
    }
    if(fm1_tracker_controls_held(&s->gestures,11) &&
       (slot==0 || slot==1 || slot==2 || slot==3 || slot==4 || slot==5 || slot==7 || slot==10)){
        block_action(s,slot,now);return;
    }
    if(slot==9){save(s,now);return;}
    if(s->view_context.help_open)return;
    if(s->page==1){
        if(slot==8)s->page=0;
        else if(slot==12){
            if(!s->library || fm1_mod_library_pick(s->library))fm1_tracker_session_notice(s,"CONNECT PC / FILE NOT READY",now);
            else {
                fm1_tracker_session_stop(s);s->library->last_ms=now;
                s->library_ticket=s->library->ticket;s->library_pending=1;s->library_saving=0;
                fm1_tracker_session_notice(s,"OPEN REQUESTED FROM PC",now);
            }
        }
        return;
    }
    if(slot==12 || slot==7){
        if(s->running)fm1_tracker_session_stop(s);
        else fm1_tracker_session_play(s,slot==7,0,now);
        return;
    }
    if(slot==10 && !s->view_context.arranger && !s->view_context.bank_open){
        s->page=s->page==2?0:2;s->view_context.sample_open=s->page==2;
        s->view_context.notice[0]=0;return;
    }
    if(s->page==2){
        if(slot==8){s->page=0;s->view_context.sample_open=0;}
        else if(slot==4){
            if(!s->bank || !s->bank->count)fm1_tracker_session_notice(s,"ACTIVE BANK UNAVAILABLE",now);
            else {s->page=0;s->view_context.sample_open=0;s->view_context.bank_open=1;
                s->view_context.bank_count=s->bank->count;s->view_context.notice[0]=0;bank_audition(s,48);}
        }else if(slot==0 && s->editor.octave>-2)s->editor.octave--;
        else if(slot==1 && s->editor.octave<1)s->editor.octave++;
        else if(slot>=14 && slot<41 && s->song->loaded){
            int midi=53+(int)slot-14+(int)s->editor.octave*12;
            if(midi>=36 && midi<=71)audition(s,(unsigned)midi);
        }
        return;
    }
    if(slot==13){
        if(s->running)fm1_tracker_session_notice(s,"STOP FIRST TO WRITE",now);
        else {s->editor.edit^=1;fm1_tracker_session_notice(s,s->editor.edit?"WRITE NOTE ENTRY":"EDIT / AUDITION",now);}
    }else if(slot==8){
        if(s->running)fm1_tracker_session_notice(s,"STOP FIRST FOR ARRANGER",now);
        else {s->view_context.bank_open=0;s->view_context.arranger^=1;s->view_context.notice[0]=0;}
    }else if(slot==0 || slot==1){
        if(slot==0 && s->editor.octave>-2)s->editor.octave--;
        if(slot==1 && s->editor.octave<1)s->editor.octave++;
    }else if(s->view_context.bank_open){
        if(slot==4){s->view_context.bank_open=0;quiet(s);}
        else if(slot==2)bank_copy(s,now);
    }else if(s->view_context.arranger)order_action(s,slot,now);
    else if(slot==3 || slot==5){
        if(!begin(s,FM1_HISTORY_PATTERN,now))finish(s,slot==3?fm1_mod_editor_clear_field(&s->editor):
            fm1_mod_editor_clear_all(&s->editor),slot==3?"FIELD CLEARED":"CELL CLEARED",now);
    }else if(slot==4){
        if(!s->bank || !s->bank->count)fm1_tracker_session_notice(s,"ACTIVE BANK UNAVAILABLE",now);
        else {s->view_context.bank_open=1;s->view_context.bank_count=s->bank->count;s->view_context.notice[0]=0;bank_audition(s,48);}
    }else if(slot==2)fm1_mod_editor_field_move(&s->editor,1);
    else if(slot==11){fm1_mod_editor_block_clear(&s->editor);fm1_tracker_session_notice(s,"BLOCK CLEARED",now);}
    if(slot>=14 && slot<41 && s->song->loaded){
        int midi=53+(int)slot-14+(int)s->editor.octave*12;
        if(midi>=36 && midi<=71){
            if(s->view_context.bank_open)bank_audition(s,(unsigned)midi);
            else {
                if(s->editor.edit && !s->running && !begin(s,FM1_HISTORY_PATTERN,now))
                    finish(s,fm1_mod_editor_note(&s->editor,(unsigned)midi),"NOTE WRITTEN",now);
                audition(s,(unsigned)midi);
            }
        }
    }
}
static void control_event(fm1_tracker_session *s,fm1_tracker_control_event e,uint32_t now){
    if(!e.kind)return;
    if(e.kind==FM1_CONTROL_KEY){key_action(s,e.index,now);return;}
    if(s->busy || s->view_context.confirm)return;
    if(e.kind==FM1_CONTROL_NEW_SONG){fm1_tracker_session_action(s,FM1_MOD_REQUEST_NEW,now);return;}
    if(!s->song->loaded)return;
    if(e.kind==FM1_CONTROL_QUICK_BIND){
        unsigned sample=s->editor.sample;s->editor.sample=e.sample;
        fm1_mod_editor_quick_bind(&s->editor,e.index);sample_notice(s,e.index,"BOUND",now);s->editor.sample=(uint8_t)sample;
    }else if(e.kind==FM1_CONTROL_QUICK_SELECT){
        fm1_mod_editor_quick_select(&s->editor,e.index);s->page=0;
        s->view_context.bank_open=s->view_context.arranger=s->view_context.sample_open=0;
        sample_notice(s,e.index,"SELECT",now);
    }
}
void fm1_tracker_session_key(fm1_tracker_session *s,unsigned slot,unsigned down,uint32_t now){
    fm1_tracker_control_event e;
    if(slot>=41)return;
    if(down && (slot==6 || slot==9 || slot==11))s->view_context.notice[0]=0;
    e=fm1_tracker_controls_key(&s->gestures,slot,down,now,s->editor.sample);
    if(s->busy){
        /* Consume both physical edges during USB work, suppress deferred taps
         * and holds, and leave no destructive action waiting for the thaw. */
        s->gestures.edit_used=s->gestures.seq_used=s->gestures.save_used=s->gestures.rec_used=1;
        s->gestures.new_pending=0;memset(s->gestures.quick_pending,0,sizeof(s->gestures.quick_pending));
    }else control_event(s,e,now);
    s->view_context.quick_modifier=(uint8_t)fm1_tracker_controls_held(&s->gestures,6);
    s->view_context.block_modifier=(uint8_t)fm1_tracker_controls_held(&s->gestures,11);
    s->view_context.save_modifier=(uint8_t)fm1_tracker_controls_held(&s->gestures,9);
}
static unsigned adjusted(unsigned value,int delta,unsigned maximum){
    if(delta<0){unsigned amount=0u-(unsigned)delta;return amount>value?0:value-amount;}
    return (unsigned)delta>maximum-value?maximum:value+(unsigned)delta;
}
static void sample_encoder(fm1_tracker_session *s,unsigned id,int step,uint32_t now){
    unsigned field=s->view_context.sample_field,h=20u+(s->editor.sample-1u)*30u;int rc=-1;
    size_t size,start,length;
    if(id==0 || id==2 || id==6){fm1_tracker_controls_encoder(&s->gestures,&s->editor,0,6,step);return;}
    if(id==3){s->view_context.sample_field=(uint8_t)((field+4+step%4)%4);return;}
    if(id!=4 && id!=5)return;
    if(begin(s,FM1_HISTORY_HEADER,now))return;
    if(field==FM1_SAMPLE_FIELD_VOLUME)rc=fm1_mod_set_sample_volume(s->song,s->editor.sample,adjusted(s->song->bytes[h+25],step,64));
    else if(field==FM1_SAMPLE_FIELD_FINETUNE){
        int fine=s->song->bytes[h+24];if(fine>=8)fine-=16;
        fine=(int)adjusted((unsigned)(fine+8),step,15)-8;
        rc=fm1_mod_set_sample_finetune(s->song,s->editor.sample,(unsigned)fine&15u);
    }else {
        unsigned amount=id==5?256u:2u;int delta=step>0?(int)amount:-(int)amount;
        fm1_mod_sample(s->song,s->editor.sample,&size,&start,&length);
        if(start>size)start=size;
        if(length>size-start)length=0;
        if(field==FM1_SAMPLE_FIELD_LOOP_START){
            unsigned maximum=(unsigned)(length>2?size-length:size);
            start=adjusted((unsigned)start,delta,maximum)&~1u;
        }else length=adjusted((unsigned)length,delta,(unsigned)(size-start))&~1u;
        rc=fm1_mod_set_sample_loop(s->song,s->editor.sample,start,length);
    }
    finish(s,rc,"SAMPLE METADATA UPDATED",now);
}
void fm1_tracker_session_encoder(fm1_tracker_session *s,unsigned id,int detents,uint32_t now){
    int step,rc;unsigned save_held;
    if(id>6 || !detents || s->busy || s->view_context.confirm)return;
    step=detents>0?1:-1;
    /* Public deltas are detents. Apply each one so clamps, history and gesture
     * behavior are identical to individual hardware clicks. */
    while(detents){
        detents-=step;save_held=(unsigned)fm1_tracker_controls_held(&s->gestures,9);
        if(save_held && id==4){
            fm1_tracker_controls_use_save(&s->gestures);s->stereo=(uint8_t)(step>0);mix(s);
            fm1_tracker_session_notice(s,s->stereo?"STEREO AUDIO OUT":"SPK ALL FOUR TRACKS",now);continue;
        }
        if(s->view_context.help_open){
            if(id==0 || id==2 || id==3 || id==6)s->view_context.help_topic=(uint8_t)
                ((s->view_context.help_topic+FM1_VIEW_HELP_TOPICS+step)%FM1_VIEW_HELP_TOPICS);
            continue;
        }
        if(!s->song->loaded)continue;
        if(s->page==1){if(s->library && (id==0 || id==2))fm1_mod_library_move(s->library,step);continue;}
        if(s->page==2){sample_encoder(s,id,step,now);continue;}
        if(s->view_context.bank_open && id==2){
            if(s->bank && s->bank->count){s->view_context.bank_index=(uint16_t)adjusted(s->view_context.bank_index,step,s->bank->count-1);bank_audition(s,48);}
        }else if(s->view_context.arranger && id==4){
            if(begin(s,FM1_HISTORY_HEADER,now))continue;
            rc=fm1_mod_set_order(s->song,s->editor.order,
                adjusted((unsigned)fm1_mod_get_order(s->song,s->editor.order),step,s->song->patterns-1));
            if(!rc)fm1_mod_editor_block_clear(&s->editor);
            finish(s,rc,"ORDER PATTERN ASSIGNED",now);
        }else if((id==4 || (id==5 && s->editor.field==FM1_FIELD_PARAM)) && !s->running){
            if(begin(s,FM1_HISTORY_PATTERN,now))continue;
            rc=fm1_tracker_controls_encoder(&s->gestures,&s->editor,0,id,step);
            finish(s,rc,"FIELD UPDATED",now);
        }else {
            rc=fm1_tracker_controls_encoder(&s->gestures,&s->editor,s->running,id,step);
            if(s->running && (id==1 || id==2 || id==4 || id==5))fm1_tracker_session_notice(s,"STOP FIRST TO EDIT",now);
            else if(id==6)sample_notice(s,8,"",now);
            else if(id==5 && s->editor.field!=FM1_FIELD_PARAM){char text[40];snprintf(text,sizeof(text),"ENTRY STEP %02u",s->editor.step);fm1_tracker_session_notice(s,text,now);}
            else if(rc)fm1_tracker_session_notice(s,"FIELD VALUE UNAVAILABLE",now);
        }
    }
    counts(s);
}
void fm1_tracker_session_tick(fm1_tracker_session *s,uint32_t now){
    unsigned count;
    if(s->busy){
        s->gestures.edit_used=s->gestures.seq_used=s->gestures.save_used=s->gestures.rec_used=1;
        s->gestures.new_pending=0;memset(s->gestures.quick_pending,0,sizeof(s->gestures.quick_pending));
    }
    for(count=0;count<10;count++){
        fm1_tracker_control_event e=fm1_tracker_controls_tick(&s->gestures,now);
        if(!e.kind)break;control_event(s,e,now);
    }
    if(s->view_context.notice[0] && (int32_t)(now-s->notice_until)>=0)s->view_context.notice[0]=0;
    s->view_context.quick_modifier=(uint8_t)fm1_tracker_controls_held(&s->gestures,6);
    s->view_context.block_modifier=(uint8_t)fm1_tracker_controls_held(&s->gestures,11);
    s->view_context.save_modifier=(uint8_t)fm1_tracker_controls_held(&s->gestures,9);
    s->view_context.new_hold=s->gestures.new_pending && !s->gestures.new_fired;
    if(s->library){
        fm1_mod_library_tick(s->library,now);
        if(s->library_pending && s->library_ticket==s->library->ticket && !s->library->pending){
            if(s->library->failed)fm1_tracker_session_notice(s,s->library_saving?"SAVE FAILED / EDITS KEPT":"OPEN FAILED / CHECK PC",now);
            else if(s->library_saving){
                if(!s->song->dirty){
                    if(s->history)fm1_mod_history_mark_saved(s->history);
                    fm1_tracker_session_notice(s,"SAVED TO PC",now);
                }else fm1_tracker_session_notice(s,"SAVE ACK / EDITS REMAIN",now);
            }else fm1_tracker_session_notice(s,s->song->loaded?"MOD LOADED":"OPEN FAILED / CREATE NEW",now);
            s->library_pending=0;
        }
    }
    counts(s);mix(s);
}
