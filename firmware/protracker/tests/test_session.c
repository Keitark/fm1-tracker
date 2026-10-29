#include "session.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static fm1_mod_document song;
static fm1_mod_history history;
static fm1_mod_library library;
static fm1_bank bank;
static fm1_tracker_session session;
static uint32_t clock_ms;
static unsigned flushes;
static const int8_t pcm[16]={64,48,32,16,0,-16,-32,-48,-64,-48,-32,-16,0,16,32,48};
static void flush_cb(void *context){assert(context==&flushes);flushes++;}
static uint32_t crc(void){return fm1_mod_crc32(song.bytes,song.length);}
static void clean(void){song.dirty=0;fm1_mod_history_init(&history,&song);}
static void reset(void){
    fm1_mod_new(&song);
    assert(!fm1_mod_replace_sample(&song,1,"TEST LOOP",pcm,sizeof(pcm),64,0,0,sizeof(pcm)));
    song.dirty=0;memset(&library,0,sizeof(library));library.ready=1;library.count=1;
    strcpy(library.entries[0].name,"TEST FILE");library.entries[0].size=2108;
    clock_ms=100;flushes=0;
    fm1_tracker_session_init(&session,&song,&library,&bank,&history,flush_cb,&flushes);
}
static void edge(unsigned key,unsigned down){fm1_tracker_session_key(&session,key,down,++clock_ms);}
static void tap(unsigned key){edge(key,1);clock_ms+=11;edge(key,0);}
static void turn(unsigned knob,int steps){fm1_tracker_session_encoder(&session,knob,steps,++clock_ms);}
static void tick(unsigned elapsed){clock_ms+=elapsed;fm1_tracker_session_tick(&session,clock_ms);}
static void chord(unsigned modifier,unsigned key){edge(modifier,1);tap(key);edge(modifier,0);}
static fm1_mod_cell cell(unsigned row,unsigned channel){
    fm1_mod_cell c;assert(!fm1_mod_cell_get(&song,(unsigned)fm1_mod_get_order(&song,session.editor.order),row,channel,&c));return c;
}
static void new_hold(unsigned reversed){
    edge(reversed?13:11,1);edge(reversed?11:13,1);tick(600);
    edge(13,0);edge(11,0);
}
static void audition_write_playback(void){
    uint32_t before;int16_t audio[256];unsigned i;
    reset();turn(0,3);turn(2,7);before=crc();tap(21);
    assert(session.player.preview_active && !song.dirty && crc()==before);
    assert(session.editor.channel==3 && session.editor.row==7 && !session.editor.edit);
    tap(13);assert(session.editor.edit);tap(21);
    assert(cell(7,3).period==428 && cell(7,3).sample==1 && session.editor.row==8);
    assert(song.dirty && fm1_mod_history_undo_count(&history)==1);
    chord(9,0);assert(!cell(7,3).period && !song.dirty && !library.pending);
    chord(9,1);assert(cell(7,3).period==428 && song.dirty);
    tap(6);assert(!session.editor.edit && !session.player.preview_active);
    before=crc();tap(12);assert(session.running && session.player.playing==1);
    for(i=0;i<8;i++)fm1_mod_render(&session.player,audio,128);
    turn(2,1);assert(session.editor.row==8);tap(13);assert(!session.editor.edit);
    tap(22);assert(crc()==before);tap(6);
    assert(!session.running && !session.player.preview_active && session.editor.row==session.player.current_row);
    assert(!fm1_tracker_session_play(&session,1,1,++clock_ms));assert(session.player.playing==2);
    fm1_tracker_session_stop(&session);
}
static void new_confirmation_and_recovery(void){
    uint32_t before;unsigned reversed;
    for(reversed=0;reversed<2;reversed++){
        reset();tap(13);tap(21);before=crc();
        new_hold(reversed);assert(session.view_context.confirm==FM1_VIEW_CONFIRM_NEW);
        assert(crc()==before && song.dirty);tap(6);
        assert(!session.view_context.confirm && crc()==before);
        new_hold(reversed);tap(13);
        assert(song.loaded && song.dirty && song.orders==1 && song.patterns==1 && song.length==2108);
        assert(!session.editor.edit && !session.view_context.confirm && !fm1_mod_history_undo_count(&history));
    }
    reset();song.loaded=0;song.dirty=0;
    new_hold(1);assert(song.loaded && song.length==2108); /* Recovery after failed upload. */
}
static void modifiers_pages_and_audio_modes(void){
    uint32_t before;unsigned selected;
    reset();tap(13);session.editor.sample=1;before=crc();
    edge(6,1);edge(4,1);tick(600);edge(4,0);edge(6,0);
    assert(session.editor.quick[4]==1 && session.editor.edit && !session.view_context.bank_open && crc()==before);
    tap(10);assert(session.page==2 && session.view_context.sample_open);
    tap(21);assert(session.player.preview_active && crc()==before && session.editor.row==0);
    turn(3,1);turn(4,-1);assert(song.bytes[20+24]==15); /* Signed fine -1. */
    chord(9,0);assert(!song.bytes[20+24] && crc()==before);
    tap(10);assert(!session.page && !session.view_context.sample_open);
    chord(9,8);assert(session.view_context.help_open && !library.pending);
    selected=session.editor.sample;turn(6,1);
    assert(session.view_context.help_topic==1 && session.editor.sample==selected);
    tap(6);assert(!session.view_context.help_open);
    edge(9,1);turn(4,1);edge(9,0);assert(session.stereo && session.player.stereo && !library.pending);
    edge(9,1);turn(4,-1);edge(9,0);assert(!session.stereo && !library.pending);
    before=crc();chord(9,3);assert(session.player.mute_mask==1 && crc()==before);
    chord(9,5);assert(session.solo_mask==1 && session.player.mute_mask==14);
    assert(session.view_context.solo_mask==1); /* Solo overrides an existing channel mute. */
    chord(9,5);assert(!session.solo_mask && session.player.mute_mask==1);
    turn(0,2);chord(9,5);assert(session.solo_mask==4 && session.player.mute_mask==11 && crc()==before);
    tap(12);assert(session.player.mute_mask==11);tap(12);
    chord(9,5);assert(!session.solo_mask && session.player.mute_mask==1);
    chord(9,12);assert(session.page==1 && !session.running && !library.pending);
    tap(12);assert(library.pending && !library.saving);
}
static void sample_metadata_and_bank(void){
    size_t size,start,length;uint32_t before;unsigned count;
    reset();assert(bank.count);before=crc();tap(4);
    assert(session.view_context.bank_open && session.player.preview_active && crc()==before && !song.dirty);
    turn(6,1);tap(2); /* Empty slot materializes immediately. */
    fm1_mod_sample(&song,2,&size,&start,&length);assert(size && !session.view_context.bank_open && song.dirty);
    assert(!fm1_mod_history_undo_count(&history));
    before=crc();tap(4);turn(2,1);tap(2);
    assert(session.view_context.confirm==FM1_VIEW_CONFIRM_BANK && crc()==before);
    turn(6,1);assert(session.editor.sample==2);tap(6);
    assert(!session.view_context.confirm && crc()==before);
    tap(2);assert(session.view_context.confirm==FM1_VIEW_CONFIRM_BANK);tap(2);
    assert(!session.view_context.confirm && !session.view_context.bank_open && crc()!=before);
    reset();assert(!fm1_mod_set_sample_loop(&song,1,0,0));clean();tap(10);
    turn(3,3);turn(4,1);turn(4,1);fm1_mod_sample(&song,1,&size,&start,&length);
    assert(length==4 && !start);turn(3,-1);turn(4,1);
    fm1_mod_sample(&song,1,&size,&start,&length);assert(start==2 && length==4);
    count=fm1_mod_history_undo_count(&history);chord(9,0);
    fm1_mod_sample(&song,1,&size,&start,&length);assert(!start && fm1_mod_history_undo_count(&history)==count-1);
    turn(3,1);turn(5,1);fm1_mod_sample(&song,1,&size,&start,&length);assert(length==size);
    reset();tap(13);tap(21);count=fm1_mod_history_undo_count(&history);
    turn(6,1);tap(4);before=crc();tap(2);
    assert(session.view_context.confirm==FM1_VIEW_CONFIRM_BANK && crc()==before);
    assert(fm1_mod_history_undo_count(&history)==count);tap(6);
    assert(crc()==before && fm1_mod_history_undo_count(&history)==count);
}
static void blocks_history_and_arranger(void){
    unsigned row;uint32_t before;
    reset();
    for(row=4;row<7;row++)assert(!fm1_mod_cell_set(&song,0,row,0,(fm1_mod_cell){428,1,0xc,32}));
    clean();session.editor.row=4;before=crc();
    edge(11,1);turn(2,2);edge(11,0);
    assert(session.editor.block_active && session.editor.block_anchor_row==4 && session.editor.block_end_row==6);
    chord(11,3);assert(session.editor.clipboard_valid && !song.dirty);
    chord(11,2);assert(!cell(4,0).period && !cell(6,0).period && song.dirty);
    chord(9,0);assert(crc()==before && !song.dirty);
    chord(11,0);assert(cell(4,0).period==453 && !session.running);
    chord(9,0);assert(crc()==before);
    session.editor.row=4;chord(11,10);
    assert(!session.page && !cell(4,0).period && cell(5,0).period==428);
    chord(9,0);assert(crc()==before);
    chord(11,7);assert(!session.running && cell(6,0).period==0);
    chord(9,0);assert(crc()==before);
    tap(8);assert(session.view_context.arranger);tap(2);assert(song.orders==2 && session.editor.order==1);
    chord(9,0);assert(song.orders==1 && session.editor.order==0);
    tap(10);assert(song.patterns==2);chord(9,0);assert(song.patterns==1);
}
static char reply_text[192];
static void reply(void *unused,const char *text){(void)unused;snprintf(reply_text,sizeof(reply_text),"%s",text);}
static int idle(void *unused){(void)unused;return !session.running;}
static int request(void *unused,unsigned op){(void)unused;return fm1_tracker_session_action(&session,op,clock_ms)<0?-1:0;}
static void status(void *unused,char *out,size_t size){(void)unused;snprintf(out,size,"MOD status ready=%u running=%u dirty=%u",song.loaded,session.running,song.dirty);}
static void usb(fm1_mod_transfer *t,const char *text){
    fm1_mod_usb_io io={0,idle,request,status};
    assert(fm1_mod_usb_line(&song,t,&io,text,clock_ms,reply,0));
    session.busy=(uint8_t)(t->active || t->exporting);
}
static void save_open_protocol_and_timeout(void){
    fm1_mod_transfer transfer={0};char command[80];uint32_t expected;unsigned ticket;
    reset();tap(13);tap(21);expected=crc();tap(9);
    assert(library.pending && library.saving && session.editor.edit);
    assert(fm1_mod_library_line(&library,"LIB POLL",reply,0) && strstr(reply_text,"LIB SAVE"));ticket=library.ticket;
    usb(&transfer,"MOD INFO");assert(transfer.exporting && session.busy && song.dirty);
    usb(&transfer,"MOD READ 00000000 40");assert(strncmp(reply_text,"MOD DATA 00000000 ",18)==0);
    edge(6,1);edge(9,1);edge(11,1);edge(13,1);tick(700);
    edge(6,0);edge(9,0);edge(11,0);edge(13,0);
    assert(!session.view_context.confirm && crc()==expected);
    snprintf(command,sizeof(command),"MOD SAVED %08x",expected);usb(&transfer,command);
    assert(!song.dirty && !session.busy);
    snprintf(command,sizeof(command),"LIB DONE %04X",ticket);assert(fm1_mod_library_line(&library,command,reply,0));
    tick(1);assert(!strcmp(session.view_context.notice,"SAVED TO PC"));
    chord(9,0);assert(song.dirty);chord(9,1);assert(!song.dirty);
    tap(21);assert(song.dirty);tap(9);assert(library.pending);tick(10001);
    assert(library.failed && !library.pending && song.dirty && strstr(session.view_context.notice,"SAVE FAILED"));
    /* No host acknowledgment may be mistaken for success while edits remain. */
    tap(9);ticket=library.ticket;
    snprintf(command,sizeof(command),"LIB DONE %04X",ticket);assert(fm1_mod_library_line(&library,command,reply,0));
    tick(1);assert(strstr(session.view_context.notice,"EDITS REMAIN"));
    chord(9,12);tap(12);assert(library.pending && !library.saving);
    fm1_mod_new(&song);fm1_tracker_session_reset_after_load(&session,clock_ms);
    snprintf(command,sizeof(command),"LIB DONE %04X",library.ticket);assert(fm1_mod_library_line(&library,command,reply,0));
    tick(1);assert(!strcmp(session.view_context.notice,"MOD LOADED"));
}
static unsigned hex_digit(char c){return c<='9'?(unsigned)(c-'0'):(unsigned)(c-'a'+10);}
static void compose_arrange_export_reopen(void){
    static fm1_mod_document reopened;
    fm1_mod_transfer transfer={0};uint8_t *output;
    unsigned track,pattern,ticket;size_t length,offset;uint32_t checksum;char command[80];
    reset();
    /* Copy four distinct immutable bank sounds into four MOD sample slots. */
    for(track=0;track<4;track++){
        if(track)turn(6,1);tap(4);if(track)turn(2,1);tap(2);
        if(session.view_context.confirm==FM1_VIEW_CONFIRM_BANK)tap(2);
        assert(!session.view_context.bank_open && !session.view_context.confirm);
    }
    turn(6,-3);turn(5,-1);assert(session.editor.sample==1 && !session.editor.step);
    tap(13);
    for(track=0;track<4;track++){
        if(track){turn(0,1);turn(6,1);}tap(21+track);
        assert(cell(0,track).sample==track+1 && cell(0,track).period==fm1_mod_period_for_midi(48+track));
        assert(!session.editor.row);
    }
    turn(2,-1);assert(session.editor.row==63);tap(24);
    turn(5,1);tap(24);assert(!session.editor.row && cell(63,3).period);
    tap(8);tap(2);assert(song.orders==2 && session.editor.order==1);
    tap(11);assert(song.patterns==2 && fm1_mod_get_order(&song,0)==0 && fm1_mod_get_order(&song,1)==1);
    tap(8);turn(4,1);
    {fm1_mod_cell original,variation;
     assert(!fm1_mod_cell_get(&song,0,0,3,&original) && !fm1_mod_cell_get(&song,1,0,3,&variation));
     assert(original.period==fm1_mod_period_for_midi(51) && variation.period==fm1_mod_period_for_midi(52));}
    turn(2,30);chord(9,7);
    assert(session.running && session.player.playing==2 && session.player.current_row==30 && !library.pending);
    tap(6);checksum=crc();length=song.length;output=(uint8_t *)malloc(length);assert(output);
    tap(9);ticket=library.ticket;assert(library.pending && library.saving);
    usb(&transfer,"MOD INFO");
    for(offset=0;offset<length;offset+=64){
        size_t count=length-offset<64?length-offset:64,i;
        snprintf(command,sizeof(command),"MOD READ %08x %02x",(unsigned)offset,(unsigned)count);usb(&transfer,command);
        assert(strncmp(reply_text,"MOD DATA ",9)==0);
        for(i=0;i<count;i++)output[offset+i]=(uint8_t)((hex_digit(reply_text[18+2*i])<<4)|hex_digit(reply_text[19+2*i]));
    }
    assert(fm1_mod_crc32(output,length)==checksum && !memcmp(output,song.bytes,length));
    snprintf(command,sizeof(command),"MOD SAVED %08x",checksum);usb(&transfer,command);
    snprintf(command,sizeof(command),"LIB DONE %04X",ticket);assert(fm1_mod_library_line(&library,command,reply,0));tick(1);
    assert(!song.dirty && !strcmp(session.view_context.notice,"SAVED TO PC"));
    chord(9,0);assert(song.dirty);chord(9,1);assert(!song.dirty && crc()==checksum);
    memset(&reopened,0,sizeof(reopened));memcpy(reopened.bytes,output,length);free(output);
    assert(!fm1_mod_open(&reopened,length));song=reopened;fm1_tracker_session_reset_after_load(&session,clock_ms);
    assert(song.orders==2 && song.patterns==2 && !song.dirty && !session.running && !session.editor.edit);
    for(pattern=0;pattern<2;pattern++)for(track=0;track<4;track++){
        fm1_mod_cell c;assert(!fm1_mod_cell_get(&song,pattern,0,track,&c));assert(c.sample==track+1 && c.period);
    }
    turn(1,3);assert(session.editor.order<song.orders);turn(2,-1);assert(session.editor.row==63);
    chord(9,8);assert(session.view_context.help_open);
    turn(3,FM1_VIEW_HELP_TOPICS);assert(session.view_context.help_topic==0);tap(6);
}
int main(int argc,char **argv){
    FILE *input;long bytes;uint8_t *data;
    assert(argc==2 && (input=fopen(argv[1],"rb"))!=0);
    assert(!fseek(input,0,SEEK_END) && (bytes=ftell(input))>0 && !fseek(input,0,SEEK_SET));
    data=(uint8_t *)malloc((size_t)bytes);assert(data && fread(data,1,(size_t)bytes,input)==(size_t)bytes);fclose(input);
    assert(!fm1_bank_load(&bank,data,(uint32_t)bytes));
    audition_write_playback();new_confirmation_and_recovery();modifiers_pages_and_audio_modes();
    sample_metadata_and_bank();blocks_history_and_arranger();save_open_protocol_and_timeout();compose_arrange_export_reopen();
    free(data);puts("tracker session scenarios passed");return 0;
}
