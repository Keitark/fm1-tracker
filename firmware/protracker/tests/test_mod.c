#include "mod.h"
#include "usb.h"
#include "library.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static fm1_mod_document doc;
static fm1_mod_transfer transfer;
static char answer[160];
static unsigned queued;
static void reply(void *context,const char *s){(void)context;strncpy(answer,s,sizeof(answer)-1);answer[sizeof(answer)-1]=0;}
static int idle(void *context){(void)context;return 1;}
static int request(void *context,unsigned op){(void)context;queued=op;return 0;}
static void status(void *context,char *out,size_t n){(void)context;snprintf(out,n,"MOD status ready=%u dirty=%u\n",doc.loaded,doc.dirty);}
static fm1_mod_usb_io io={0,idle,request,status};
static void command(const char *s){answer[0]=0;assert(fm1_mod_usb_line(&doc,&transfer,&io,s,100,reply,0)==1);}

static void test_document(void){
    fm1_mod_cell cell,read;fm1_mod_editor edit;uint8_t baseline[2112];
    size_t size,loop,repeat;const int8_t *sample;
    fm1_mod_new(&doc);
    doc.bytes[20+22]=0;doc.bytes[20+23]=2;doc.bytes[20+25]=64;
    doc.bytes[2108]=0x40;doc.bytes[2109]=0x20;doc.bytes[2110]=0;doc.bytes[2111]=0;
    doc.length=2112;assert(fm1_mod_open(&doc,2112)==0);
    memcpy(baseline,doc.bytes,2112);assert(!memcmp(baseline,doc.bytes,2112));
    fm1_mod_editor_init(&edit,&doc);edit.octave=-1;
    assert(fm1_mod_editor_note(&edit,48)==0);assert(edit.row==1);
    assert(fm1_mod_cell_get(&doc,0,0,0,&read)==0);
    assert(read.period==428 && read.sample==1);
    assert(fm1_mod_editor_effect(&edit,0xc,0x20)==0);
    assert(fm1_mod_cell_get(&doc,0,1,0,&read)==0 && read.effect==0xc && read.param==0x20);
    assert(fm1_mod_editor_clear(&edit)==0);
    assert(fm1_mod_cell_get(&doc,0,1,0,&read)==0 && !read.effect && !read.param);
    assert(fm1_mod_open(&doc,doc.length)==0);
    assert(fm1_mod_cell_get(&doc,0,0,0,&cell)==0 && cell.period==428);
    assert(fm1_mod_add_pattern(&doc)==1 && doc.length==3136);
    sample=fm1_mod_sample(&doc,1,&size,&loop,&repeat);
    assert(size==4 && sample[0]==0x40 && sample[1]==0x20);
    assert(fm1_mod_set_order(&doc,1,1)==0 && doc.orders==2);
    assert(fm1_mod_cell_get(&doc,1,0,0,&read)==0 && !read.period);
    assert(fm1_mod_open(&doc,doc.length)==0 && doc.patterns==2 && doc.orders==2);
    assert(fm1_mod_set_title(&doc,"FM1 TRACKER")==0);
    assert(fm1_mod_set_sample_name(&doc,1,"WAVE")==0);
    assert(fm1_mod_set_sample_volume(&doc,1,32)==0);
    assert(fm1_mod_set_sample_finetune(&doc,1,15)==0);
    assert(fm1_mod_open(&doc,doc.length)==0);
    assert(!memcmp(doc.bytes,"FM1 TRACKER",11));
    assert(doc.bytes[20+25]==32);
    assert(doc.bytes[20+24]==15);
    assert(fm1_mod_period_for_midi(35)==0 && fm1_mod_period_for_midi(72)==0);
}

static void test_player(void){
    static int16_t pcm[FM1_MOD_BLOCK*2];fm1_mod_player player;unsigned i,nonzero=0;
    fm1_mod_new(&doc);doc.bytes[20+23]=2;doc.bytes[20+25]=64;
    doc.bytes[2108]=0x40;doc.bytes[2109]=0x40;doc.bytes[2110]=0x40;doc.bytes[2111]=0x40;
    doc.length=2112;assert(fm1_mod_open(&doc,2112)==0);
    assert(fm1_mod_cell_set(&doc,0,0,0,(fm1_mod_cell){428,1,0,0})==0);
    fm1_mod_player_start(&player,&doc);fm1_mod_render(&player,pcm,FM1_MOD_BLOCK);
    for(i=0;i<FM1_MOD_BLOCK;i++){if(pcm[2*i])nonzero++;assert(pcm[2*i+1]==pcm[2*i]);}
    assert(nonzero>0);
    assert(player.scope_head==2 && player.scope[0][0]>0 && player.scope[1][0]==0);
    fm1_mod_player_start(&player,&doc);player.stereo=1;fm1_mod_render(&player,pcm,1);
    assert(pcm[0]>0 && pcm[1]==0);
    fm1_mod_player_stop(&player);
    fm1_mod_render(&player,pcm,FM1_MOD_BLOCK);
    for(i=0;i<FM1_MOD_BLOCK*2;i++)assert(pcm[i]==0);
    assert(fm1_mod_cell_set(&doc,0,0,1,(fm1_mod_cell){0,0,0xf,0x20})==0);
    fm1_mod_player_start(&player,&doc);fm1_mod_render(&player,pcm,1);
    assert(player.speed==32 && player.bpm==125);
    assert(fm1_mod_cell_set(&doc,0,0,1,(fm1_mod_cell){0,0,0xf,0x21})==0);
    fm1_mod_player_start(&player,&doc);fm1_mod_render(&player,pcm,1);
    assert(player.speed==6 && player.bpm==33);
}
static void test_keyboard_preview(void){
    fm1_mod_player player={0};int16_t pcm[2];uint32_t low_step;
    fm1_mod_new(&doc);doc.bytes[20+23]=2;doc.bytes[20+25]=64;
    doc.bytes[2108]=0x40;doc.bytes[2109]=0x40;doc.bytes[2110]=0x40;doc.bytes[2111]=0x40;
    doc.length=2112;assert(fm1_mod_open(&doc,doc.length)==0 && !doc.dirty);
    player.stereo=1;
    assert(fm1_mod_preview_start(&player,&doc,1,48,2)==0);
    low_step=player.preview_step;
    fm1_mod_render(&player,pcm,1);
    assert(pcm[0]==0 && pcm[1]==4096);
    assert(player.scope[2][0]>0 && player.scope[0][0]==0);
    assert(!doc.dirty && !player.playing);
    assert(fm1_mod_preview_start(&player,&doc,1,60,0)==0);
    assert(player.preview_step>=low_step*2u-2u);
    fm1_mod_render(&player,pcm,1);
    assert(pcm[0]==4096 && pcm[1]==0);
    fm1_mod_preview_stop(&player);
    fm1_mod_render(&player,pcm,1);
    assert(pcm[0]==0 && pcm[1]==0 && !doc.dirty);
    assert(fm1_mod_preview_start(&player,&doc,1,72,0)==-1);
}
static void setup_preview_sample(void){
    fm1_mod_new(&doc);doc.bytes[20+23]=2;doc.bytes[20+25]=64;
    memset(doc.bytes+2108,0x40,4);
    doc.length=2112;assert(fm1_mod_open(&doc,doc.length)==0);
}
static void test_invalid_preview_sample_cancels_previous_sound(void){
    fm1_mod_player player={0};int16_t pcm[2];
    setup_preview_sample();
    assert(fm1_mod_preview_start(&player,&doc,1,48,0)==0);
    fm1_mod_render(&player,pcm,1);assert(pcm[0]>0 && player.preview_active);
    assert(fm1_mod_preview_start(&player,&doc,32,48,0)==-1);
    assert(!player.preview_active);
    fm1_mod_render(&player,pcm,1);assert(pcm[0]==0 && pcm[1]==0);
    assert(!doc.dirty);
}
static void test_empty_preview_sample_cancels_previous_sound(void){
    fm1_mod_player player={0};int16_t pcm[2];
    setup_preview_sample();
    assert(fm1_mod_preview_start(&player,&doc,1,48,0)==0);
    fm1_mod_render(&player,pcm,1);assert(pcm[0]>0 && player.preview_active);
    assert(fm1_mod_preview_start(&player,&doc,2,48,0)==-2);
    assert(!player.preview_active);
    fm1_mod_render(&player,pcm,1);assert(pcm[0]==0 && pcm[1]==0);
    assert(!doc.dirty);
}
static void render_frames(fm1_mod_player *player,unsigned frames){
    static int16_t pcm[FM1_MOD_BLOCK*2];
    while(frames){
        unsigned block=frames>FM1_MOD_BLOCK?FM1_MOD_BLOCK:frames;
        fm1_mod_render(player,pcm,block);frames-=block;
    }
}
static void test_player_starts_at_selected_order_and_row(void){
    fm1_mod_player player;
    fm1_mod_new(&doc);assert(fm1_mod_add_pattern(&doc)==1);
    assert(fm1_mod_set_order(&doc,1,1)==0);
    assert(fm1_mod_cell_set(&doc,0,17,2,(fm1_mod_cell){428,1,0xc,12})==0);
    assert(fm1_mod_cell_set(&doc,1,17,2,(fm1_mod_cell){214,2,0xc,37})==0);
    assert(fm1_mod_player_start_at(&player,&doc,1,17)==0);
    assert(player.playing && player.document==&doc);
    assert(player.order==1 && player.row==17);
    assert(player.current_order==1 && player.current_row==17);
    assert(player.speed==6 && player.bpm==125);
    render_frames(&player,1);
    assert(player.voices[2].base_period==214 && player.voices[2].sample==2);
    assert(player.voices[2].volume==37);
    assert(player.current_order==1 && player.current_row==17);
}
static void test_player_start_at_rejects_invalid_targets(void){
    fm1_mod_player player={0};
    fm1_mod_new(&doc);
    assert(fm1_mod_player_start_at(0,&doc,0,0)==-1);
    assert(fm1_mod_player_start_at(&player,0,0,0)==-1);
    doc.loaded=0;
    assert(fm1_mod_player_start_at(&player,&doc,0,0)==-1);
    doc.loaded=1;
    assert(fm1_mod_player_start_at(&player,&doc,doc.orders,0)==-1);
    assert(fm1_mod_player_start_at(&player,&doc,0,64)==-1);
    assert(!player.playing && !player.document);
}
static void test_current_row_waits_for_next_row_trigger_at_speed_six(void){
    fm1_mod_player player;
    fm1_mod_new(&doc);
    assert(fm1_mod_cell_set(&doc,0,0,0,(fm1_mod_cell){428,1,0,0})==0);
    assert(fm1_mod_cell_set(&doc,0,1,0,(fm1_mod_cell){214,1,0,0})==0);
    fm1_mod_player_start(&player,&doc);
    render_frames(&player,FM1_MOD_RATE/10u);
    assert(player.speed==6 && player.bpm==125);
    assert(player.row==1 && player.current_row==0 && player.current_order==0);
    assert(player.voices[0].base_period==428);
    render_frames(&player,FM1_MOD_RATE/50u-1u);
    assert(player.row==1 && player.current_row==0);
    assert(player.voices[0].base_period==428);
    render_frames(&player,1);
    assert(player.current_row==1 && player.current_order==0);
    assert(player.voices[0].base_period==214);
}
static void test_current_row_keeps_rendered_row_at_speed_one(void){
    fm1_mod_player player;
    fm1_mod_new(&doc);
    assert(fm1_mod_cell_set(&doc,0,0,0,(fm1_mod_cell){428,1,0xf,1})==0);
    assert(fm1_mod_cell_set(&doc,0,1,0,(fm1_mod_cell){214,1,0,0})==0);
    fm1_mod_player_start(&player,&doc);render_frames(&player,1);
    assert(player.speed==1 && player.row==1 && player.current_row==0);
    assert(player.voices[0].base_period==428);
    render_frames(&player,FM1_MOD_RATE/50u-2u);
    assert(player.row==1 && player.current_row==0);
    render_frames(&player,1);
    assert(player.row==2 && player.current_row==1);
    assert(player.voices[0].base_period==214);
}
static void test_current_position_crosses_order_only_when_next_row_triggers(void){
    fm1_mod_player player;
    fm1_mod_new(&doc);assert(fm1_mod_add_pattern(&doc)==1);
    assert(fm1_mod_set_order(&doc,1,1)==0);
    assert(fm1_mod_cell_set(&doc,0,63,0,(fm1_mod_cell){428,1,0xf,1})==0);
    assert(fm1_mod_cell_set(&doc,1,0,0,(fm1_mod_cell){214,1,0,0})==0);
    assert(fm1_mod_player_start_at(&player,&doc,0,63)==0);
    render_frames(&player,1);
    assert(player.order==1 && player.row==0);
    assert(player.current_order==0 && player.current_row==63);
    assert(player.voices[0].base_period==428);
    render_frames(&player,FM1_MOD_RATE/50u-1u);
    assert(player.order==1 && player.row==1);
    assert(player.current_order==1 && player.current_row==0);
    assert(player.voices[0].base_period==214);
}
static void setup_three_order_song(void){
    fm1_mod_new(&doc);assert(fm1_mod_add_pattern(&doc)==1);
    assert(fm1_mod_add_pattern(&doc)==2);
    assert(fm1_mod_set_order(&doc,1,1)==0);
    assert(fm1_mod_set_order(&doc,2,2)==0);
}
static void test_pattern_play_starts_selected_pattern_at_selected_row(void){
    fm1_mod_player player;
    setup_three_order_song();
    assert(fm1_mod_cell_set(&doc,0,17,2,(fm1_mod_cell){428,1,0xc,12})==0);
    assert(fm1_mod_cell_set(&doc,1,17,2,(fm1_mod_cell){214,2,0xc,37})==0);
    assert(fm1_mod_player_start_pattern_at(&player,&doc,1,17)==0);
    assert(player.playing==2 && player.order==1 && player.row==17);
    render_frames(&player,1);
    assert(player.current_order==1 && player.current_row==17);
    assert(player.voices[2].base_period==214 && player.voices[2].sample==2);
    assert(player.voices[2].volume==37);
}
static void test_pattern_play_rejects_invalid_start_targets(void){
    fm1_mod_player player={0};
    fm1_mod_new(&doc);
    assert(fm1_mod_player_start_pattern_at(0,&doc,0,0)==-1);
    assert(fm1_mod_player_start_pattern_at(&player,0,0,0)==-1);
    doc.loaded=0;
    assert(fm1_mod_player_start_pattern_at(&player,&doc,0,0)==-1);
    doc.loaded=1;
    assert(fm1_mod_player_start_pattern_at(&player,&doc,doc.orders,0)==-1);
    assert(fm1_mod_player_start_pattern_at(&player,&doc,0,64)==-1);
    assert(!player.playing && !player.document);
}
static void test_pattern_play_wraps_row_sixty_three_in_same_order(void){
    fm1_mod_player player;
    setup_three_order_song();
    assert(fm1_mod_cell_set(&doc,1,63,0,(fm1_mod_cell){428,1,0xf,1})==0);
    assert(fm1_mod_cell_set(&doc,1,0,0,(fm1_mod_cell){214,1,0,0})==0);
    assert(fm1_mod_cell_set(&doc,2,0,0,(fm1_mod_cell){320,1,0,0})==0);
    assert(fm1_mod_player_start_pattern_at(&player,&doc,1,63)==0);
    render_frames(&player,1);
    assert(player.order==1 && player.row==0);
    assert(player.current_order==1 && player.current_row==63);
    render_frames(&player,FM1_MOD_RATE/50u-1u);
    assert(player.current_order==1 && player.current_row==0 && player.playing==2);
    assert(player.voices[0].base_period==214);
}
static void test_pattern_break_uses_requested_row_in_same_order(void){
    fm1_mod_player player;
    setup_three_order_song();
    assert(fm1_mod_cell_set(&doc,1,5,0,(fm1_mod_cell){428,1,0xf,1})==0);
    assert(fm1_mod_cell_set(&doc,1,5,1,(fm1_mod_cell){0,0,0xd,0x12})==0);
    assert(fm1_mod_cell_set(&doc,1,12,0,(fm1_mod_cell){214,1,0,0})==0);
    assert(fm1_mod_cell_set(&doc,2,12,0,(fm1_mod_cell){320,1,0,0})==0);
    assert(fm1_mod_player_start_pattern_at(&player,&doc,1,5)==0);
    render_frames(&player,1);
    assert(player.order==1 && player.row==12);
    assert(player.current_order==1 && player.current_row==5);
    render_frames(&player,FM1_MOD_RATE/50u-1u);
    assert(player.current_order==1 && player.current_row==12);
    assert(player.voices[0].base_period==214);
}
static void test_pattern_jump_restarts_same_order(void){
    fm1_mod_player player;
    setup_three_order_song();
    assert(fm1_mod_cell_set(&doc,1,5,0,(fm1_mod_cell){428,1,0xf,1})==0);
    assert(fm1_mod_cell_set(&doc,1,5,1,(fm1_mod_cell){0,0,0xb,2})==0);
    assert(fm1_mod_cell_set(&doc,1,0,0,(fm1_mod_cell){214,1,0,0})==0);
    assert(fm1_mod_cell_set(&doc,2,0,0,(fm1_mod_cell){320,1,0,0})==0);
    assert(fm1_mod_player_start_pattern_at(&player,&doc,1,5)==0);
    render_frames(&player,1);assert(player.order==1 && player.row==0);
    render_frames(&player,FM1_MOD_RATE/50u-1u);
    assert(player.current_order==1 && player.current_row==0);
    assert(player.voices[0].base_period==214);
}
static void test_pattern_jump_and_break_keep_order_and_use_break_row(void){
    fm1_mod_player player;
    setup_three_order_song();
    assert(fm1_mod_cell_set(&doc,1,5,0,(fm1_mod_cell){428,1,0xf,1})==0);
    assert(fm1_mod_cell_set(&doc,1,5,1,(fm1_mod_cell){0,0,0xb,2})==0);
    assert(fm1_mod_cell_set(&doc,1,5,2,(fm1_mod_cell){0,0,0xd,0x12})==0);
    assert(fm1_mod_cell_set(&doc,1,12,0,(fm1_mod_cell){214,1,0,0})==0);
    assert(fm1_mod_cell_set(&doc,2,12,0,(fm1_mod_cell){320,1,0,0})==0);
    assert(fm1_mod_player_start_pattern_at(&player,&doc,1,5)==0);
    render_frames(&player,1);assert(player.order==1 && player.row==12);
    render_frames(&player,FM1_MOD_RATE/50u-1u);
    assert(player.current_order==1 && player.current_row==12);
    assert(player.voices[0].base_period==214);
}
static void test_song_jump_and_break_still_change_order(void){
    fm1_mod_player player;
    setup_three_order_song();
    assert(fm1_mod_cell_set(&doc,1,5,0,(fm1_mod_cell){428,1,0xf,1})==0);
    assert(fm1_mod_cell_set(&doc,1,5,1,(fm1_mod_cell){0,0,0xb,2})==0);
    assert(fm1_mod_cell_set(&doc,1,5,2,(fm1_mod_cell){0,0,0xd,0x12})==0);
    assert(fm1_mod_cell_set(&doc,1,12,0,(fm1_mod_cell){214,1,0,0})==0);
    assert(fm1_mod_cell_set(&doc,2,12,0,(fm1_mod_cell){320,1,0,0})==0);
    assert(fm1_mod_player_start_at(&player,&doc,1,5)==0);
    render_frames(&player,1);assert(player.playing==1 && player.order==2 && player.row==12);
    render_frames(&player,FM1_MOD_RATE/50u-1u);
    assert(player.current_order==2 && player.current_row==12);
    assert(player.voices[0].base_period==320);
}
static void test_zero_step_note_overwrites_selected_cell_without_moving(void){
    fm1_mod_editor edit;fm1_mod_cell cell;
    fm1_mod_new(&doc);
    assert(fm1_mod_cell_set(&doc,0,17,2,(fm1_mod_cell){428,3,0xe,0xd3})==0);
    assert(fm1_mod_cell_set(&doc,0,17,1,(fm1_mod_cell){320,2,4,0x47})==0);
    assert(fm1_mod_open(&doc,doc.length)==0 && !doc.dirty);
    fm1_mod_editor_init(&edit,&doc);
    edit.row=17;edit.channel=2;edit.sample=7;edit.step=0;
    assert(fm1_mod_editor_note(&edit,60)==0);
    assert(edit.order==0 && edit.row==17 && edit.channel==2 && doc.dirty);
    assert(fm1_mod_cell_get(&doc,0,17,2,&cell)==0);
    assert(cell.period==214 && cell.sample==7 && cell.effect==0xe && cell.param==0xd3);
    assert(fm1_mod_cell_get(&doc,0,17,1,&cell)==0);
    assert(cell.period==320 && cell.sample==2 && cell.effect==4 && cell.param==0x47);
}
static void test_note_entry_advances_by_selected_step(void){
    fm1_mod_editor edit;fm1_mod_cell cell;
    fm1_mod_new(&doc);fm1_mod_editor_init(&edit,&doc);
    edit.row=10;edit.channel=3;edit.sample=4;edit.step=2;
    assert(fm1_mod_editor_note(&edit,48)==0);
    assert(edit.order==0 && edit.row==12 && edit.channel==3);
    assert(fm1_mod_cell_get(&doc,0,10,3,&cell)==0 && cell.period==428 && cell.sample==4);
    assert(fm1_mod_cell_get(&doc,0,12,3,&cell)==0 && !cell.period && !cell.sample);
}
static void test_note_entry_step_wraps_within_selected_order(void){
    fm1_mod_editor edit;fm1_mod_cell cell;
    fm1_mod_new(&doc);assert(fm1_mod_set_order(&doc,1,0)==0);
    fm1_mod_editor_init(&edit,&doc);edit.order=1;edit.row=63;edit.channel=2;
    assert(fm1_mod_editor_note(&edit,48)==0);
    assert(edit.order==1 && edit.row==0 && edit.channel==2);
    assert(fm1_mod_cell_get(&doc,0,63,2,&cell)==0 && cell.period==428 && cell.sample==1);
}
static uint8_t clear_baseline[3132];
static void setup_clear_editor(fm1_mod_editor *edit){
    fm1_mod_new(&doc);assert(fm1_mod_add_pattern(&doc)==1);
    assert(fm1_mod_set_order(&doc,1,1)==0);
    assert(fm1_mod_cell_set(&doc,1,19,2,(fm1_mod_cell){428,3,0xa,0x37})==0);
    assert(fm1_mod_cell_set(&doc,1,19,1,(fm1_mod_cell){320,2,0xf,1})==0);
    assert(fm1_mod_cell_set(&doc,0,19,2,(fm1_mod_cell){214,7,0xc,0x42})==0);
    assert(fm1_mod_open(&doc,doc.length)==0 && !doc.dirty);
    assert(doc.length==sizeof(clear_baseline));
    memcpy(clear_baseline,doc.bytes,doc.length);
    fm1_mod_editor_init(edit,&doc);
    edit->order=1;edit->row=19;edit->channel=2;edit->step=3;
}
static void assert_clear_changed_only_selected_cell(const fm1_mod_editor *edit){
    size_t offset=1084u+1024u+19u*16u+2u*4u;
    assert(edit->order==1 && edit->row==19 && edit->channel==2);
    assert(!memcmp(doc.bytes,clear_baseline,offset));
    assert(!memcmp(doc.bytes+offset+4,clear_baseline+offset+4,doc.length-offset-4));
}
static void test_clear_note_preserves_selected_effect(void){
    fm1_mod_editor edit;fm1_mod_cell cell;
    setup_clear_editor(&edit);
    assert(fm1_mod_editor_clear_note(&edit)==0 && doc.dirty);
    assert(fm1_mod_cell_get(&doc,1,19,2,&cell)==0);
    assert(!cell.period && !cell.sample && cell.effect==0xa && cell.param==0x37);
    assert_clear_changed_only_selected_cell(&edit);
}
static void test_clear_effect_preserves_selected_note(void){
    fm1_mod_editor edit;fm1_mod_cell cell;
    setup_clear_editor(&edit);
    assert(fm1_mod_editor_clear_effect(&edit)==0 && doc.dirty);
    assert(fm1_mod_cell_get(&doc,1,19,2,&cell)==0);
    assert(cell.period==428 && cell.sample==3 && !cell.effect && !cell.param);
    assert_clear_changed_only_selected_cell(&edit);
}
static void test_clear_cell_erases_selected_note_and_effect(void){
    fm1_mod_editor edit;fm1_mod_cell cell;
    setup_clear_editor(&edit);
    assert(fm1_mod_editor_clear_cell(&edit)==0 && doc.dirty);
    assert(fm1_mod_cell_get(&doc,1,19,2,&cell)==0);
    assert(!cell.period && !cell.sample && !cell.effect && !cell.param);
    assert_clear_changed_only_selected_cell(&edit);
}
static void test_legacy_clear_still_erases_full_selected_cell(void){
    fm1_mod_editor edit;fm1_mod_cell cell;
    setup_clear_editor(&edit);
    assert(fm1_mod_editor_clear(&edit)==0 && doc.dirty);
    assert(fm1_mod_cell_get(&doc,1,19,2,&cell)==0);
    assert(!cell.period && !cell.sample && !cell.effect && !cell.param);
    assert_clear_changed_only_selected_cell(&edit);
}
static void test_clearing_unchanged_fields_does_not_mark_document_dirty(void){
    fm1_mod_editor edit;
    setup_clear_editor(&edit);
    assert(fm1_mod_cell_set(&doc,1,19,2,(fm1_mod_cell){0,0,0,0})==0);
    assert(fm1_mod_open(&doc,doc.length)==0 && !doc.dirty);
    assert(fm1_mod_editor_clear_note(&edit)==0 && !doc.dirty);
    assert(fm1_mod_editor_clear_effect(&edit)==0 && !doc.dirty);
    assert(fm1_mod_editor_clear_cell(&edit)==0 && !doc.dirty);
    assert(fm1_mod_editor_clear(&edit)==0 && !doc.dirty);
    assert(edit.order==1 && edit.row==19 && edit.channel==2);
}
static void test_clear_operations_reject_audition_mode(void){
    fm1_mod_editor edit;
    setup_clear_editor(&edit);edit.edit=0;
    assert(fm1_mod_editor_clear_note(&edit)==-1);
    assert(fm1_mod_editor_clear_effect(&edit)==-1);
    assert(fm1_mod_editor_clear_cell(&edit)==-1);
    assert(fm1_mod_editor_clear(&edit)==-1);
    assert(!doc.dirty && !memcmp(doc.bytes,clear_baseline,doc.length));
}
static void test_all_four_channels_on_mono_speaker(void){
    fm1_mod_player player;int16_t pcm[2];unsigned ch;
    fm1_mod_new(&doc);doc.bytes[20+23]=2;doc.bytes[20+25]=64;
    doc.bytes[2108]=0x40;doc.bytes[2109]=0x40;doc.bytes[2110]=0x40;doc.bytes[2111]=0x40;
    doc.length=2112;assert(fm1_mod_open(&doc,doc.length)==0);
    for(ch=0;ch<4;ch++){
        unsigned i;
        for(i=0;i<4;i++)assert(fm1_mod_cell_set(&doc,0,0,i,
            (fm1_mod_cell){i==ch?428:0,i==ch?1:0,0,0})==0);
        fm1_mod_player_start(&player,&doc);fm1_mod_render(&player,pcm,1);
        assert(pcm[0]==2048 && pcm[1]==2048);
    }
    for(ch=0;ch<4;ch++)assert(fm1_mod_cell_set(&doc,0,0,ch,
        (fm1_mod_cell){428,1,0,0})==0);
    fm1_mod_player_start(&player,&doc);fm1_mod_render(&player,pcm,1);
    assert(pcm[0]==8192 && pcm[1]==8192);
}

static void test_explicit_stereo_preserves_local_mode_and_sample_preview(void){
    static int16_t capture[FM1_MOD_BLOCK*2],local[FM1_MOD_BLOCK*2],stereo[FM1_MOD_BLOCK*2];
    static const unsigned volumes[4]={64,16,32,48};
    fm1_mod_player raw,mono,phone;unsigned ch,i;
    setup_preview_sample();doc.bytes[20+29]=2; /* Keep the constant sample looping. */
    assert(fm1_mod_open(&doc,doc.length)==0);
    for(ch=0;ch<4;ch++)assert(fm1_mod_cell_set(&doc,0,0,ch,
        (fm1_mod_cell){428,1,0xc,(uint8_t)volumes[ch]})==0);
    for(ch=0;ch<4;ch++){
        fm1_mod_player_start(&raw,&doc);
        assert(fm1_mod_preview_start(&raw,&doc,1,48,ch)==0);
        mono=phone=raw;phone.stereo=1;
        fm1_mod_render_stereo(&raw,capture,FM1_MOD_BLOCK);
        fm1_mod_render(&mono,local,FM1_MOD_BLOCK);
        fm1_mod_render(&phone,stereo,FM1_MOD_BLOCK);
        assert(raw.stereo==0 && mono.stereo==0 && phone.stereo==1);
        assert(capture[0]==(ch==0 || ch==3?11264:7168));
        assert(capture[1]==(ch==0 || ch==3?3072:7168));
        for(i=0;i<FM1_MOD_BLOCK;i++){
            assert(capture[2*i]==stereo[2*i] && capture[2*i+1]==stereo[2*i+1]);
            assert(local[2*i]==(capture[2*i]+capture[2*i+1])/2);
            assert(local[2*i+1]==local[2*i]);
        }
        /* Rendering the USB mix advances playback and previews exactly once. */
        phone.stereo=0;
        assert(!memcmp(&raw,&phone,sizeof(raw)) && !memcmp(&raw,&mono,sizeof(raw)));
    }
}

static void test_explicit_stereo_bank_preview_panning(void){
    static const int8_t sample[4]={-64,-64,-64,-64};
    fm1_mod_player raw,mono,phone;int16_t capture[2],local[2],stereo[2];unsigned ch;
    for(ch=0;ch<4;ch++){
        memset(&raw,0,sizeof(raw));
        assert(fm1_mod_preview_pcm(&raw,sample,sizeof(sample),32,0,0,4,48,ch)==0);
        mono=phone=raw;phone.stereo=1;
        fm1_mod_render_stereo(&raw,capture,1);
        fm1_mod_render(&mono,local,1);fm1_mod_render(&phone,stereo,1);
        assert(raw.stereo==0 && !raw.playing);
        assert(capture[0]==(ch==0 || ch==3?-2048:0));
        assert(capture[1]==(ch==0 || ch==3?0:-2048));
        assert(!memcmp(capture,stereo,sizeof(capture)));
        assert(local[0]==-1024 && local[1]==-1024);
        phone.stereo=0;
        assert(!memcmp(&raw,&phone,sizeof(raw)) && !memcmp(&raw,&mono,sizeof(raw)));
    }
}

static void test_explicit_stereo_four_voices_and_preview_headroom(void){
    fm1_mod_player player;int16_t pcm[2];unsigned ch;
    setup_preview_sample();memset(doc.bytes+2108,127,4);
    for(ch=0;ch<4;ch++)assert(fm1_mod_cell_set(&doc,0,0,ch,
        (fm1_mod_cell){428,1,0,0})==0);
    fm1_mod_player_start(&player,&doc);
    assert(fm1_mod_preview_start(&player,&doc,1,48,0)==0);
    fm1_mod_render_stereo(&player,pcm,1);
    assert(player.stereo==0 && pcm[0]==24384 && pcm[1]==16256);
}

static void test_arpeggio_resets_on_next_row(void){
    static int16_t pcm[5500*2];fm1_mod_player player;
    fm1_mod_new(&doc);doc.bytes[20+23]=2;doc.bytes[20+25]=64;
    doc.length=2112;assert(fm1_mod_open(&doc,2112)==0);
    assert(fm1_mod_cell_set(&doc,0,0,0,(fm1_mod_cell){428,1,0,0x47})==0);
    fm1_mod_player_start(&player,&doc);fm1_mod_render(&player,pcm,5500);
    assert(player.row==1 && player.voices[0].base_period==428);
    assert(player.voices[0].period==428);
}
static void test_classic_song_effects(void){
    static int16_t pcm[FM1_MOD_BLOCK*2];fm1_mod_player player;unsigned i,nonzero=0;
    fm1_mod_new(&doc);doc.bytes[20+23]=2;doc.bytes[20+25]=40;
    doc.bytes[2108]=0x40;doc.bytes[2109]=0x40;doc.bytes[2110]=0x40;doc.bytes[2111]=0x40;
    doc.length=2112;assert(fm1_mod_open(&doc,2112)==0);
    assert(fm1_mod_cell_set(&doc,0,0,0,(fm1_mod_cell){428,1,8,255})==0);
    fm1_mod_player_start(&player,&doc);fm1_mod_render(&player,pcm,FM1_MOD_BLOCK);
    for(i=0;i<FM1_MOD_BLOCK;i++){assert(pcm[2*i]==pcm[2*i+1]);if(pcm[2*i+1])nonzero++;}
    assert(nonzero>0);
    fm1_mod_player_start(&player,&doc);player.stereo=1;fm1_mod_render(&player,pcm,1);
    assert(pcm[0]==0 && pcm[1]>0);
    assert(fm1_mod_cell_set(&doc,0,0,0,(fm1_mod_cell){428,1,14,0xa4})==0);
    fm1_mod_player_start(&player,&doc);fm1_mod_render(&player,pcm,1);
    assert(player.voices[0].volume==44);
    assert(fm1_mod_cell_set(&doc,0,0,0,(fm1_mod_cell){428,1,14,0xb4})==0);
    fm1_mod_player_start(&player,&doc);fm1_mod_render(&player,pcm,1);
    assert(player.voices[0].volume==36);
    assert(fm1_mod_cell_set(&doc,0,0,0,(fm1_mod_cell){428,1,14,0x13})==0);
    fm1_mod_player_start(&player,&doc);fm1_mod_render(&player,pcm,1);
    assert(player.voices[0].base_period==425);
}

static void test_extended_patterns(void){
    unsigned i;fm1_mod_new(&doc);
    for(i=1;i<=64;i++){
        assert(fm1_mod_add_pattern(&doc)==(int)i);
        assert(fm1_mod_set_order(&doc,i,i)==0);
    }
    assert(!memcmp(doc.bytes+1080,"M!K!",4));
    assert(fm1_mod_open(&doc,doc.length)==0 && doc.patterns==65 && doc.orders==65);
}

static void test_usb(void){
    uint32_t crc;unsigned i;char line[160],hex[129];static uint8_t saved[2112];
    fm1_mod_new(&doc);memcpy(saved,doc.bytes,2112);
    crc=fm1_mod_crc32(saved,2112);memset(&transfer,0,sizeof(transfer));
    snprintf(line,sizeof(line),"MOD BEGIN %08x %08x",2112,crc);command(line);
    assert(!strcmp(answer,"OK MOD BEGIN RAM\n") && transfer.active && !doc.loaded);
    for(i=0;i<2112;i+=64){unsigned j;
        for(j=0;j<64;j++)sprintf(hex+j*2,"%02x",saved[i+j]);
        snprintf(line,sizeof(line),"MOD DATA %08x %s",i,hex);command(line);
        assert(strncmp(answer,"OK MOD DATA ",12)==0);
    }
    command("MOD END");assert(!strcmp(answer,"OK MOD READY RAM VOLATILE\n") && doc.loaded);
    command("MOD INFO");assert(transfer.exporting && strstr(answer,"MOD INFO "));
    command("MOD READ 00000000 40");assert(strncmp(answer,"MOD DATA 00000000 ",18)==0);
    command("MOD PLAY");assert(!strcmp(answer,"ERR MOD BUSY_OR_NO_SONG\n"));
    snprintf(line,sizeof(line),"MOD SAVED %08x",crc);command(line);
    assert(!strcmp(answer,"OK MOD SAVED\n") && !transfer.exporting);
    command("MOD PLAY");assert(queued==FM1_MOD_REQUEST_PLAY);
    command("MOD BEGIN 00000001 00000000");assert(!transfer.active);
    command("MOD READ 00000000 20");assert(strncmp(answer,"ERR MOD",7)==0);
}

static void test_invalid(void){
    static fm1_mod_document other;fm1_mod_new(&other);
    memcpy(other.bytes+1080,"6CHN",4);assert(fm1_mod_open(&other,other.length)==-2);
    fm1_mod_new(&other);other.bytes[20+25]=65;assert(fm1_mod_open(&other,other.length)==-6);
    fm1_mod_new(&other);other.bytes[952]=127;assert(fm1_mod_open(&other,other.length)==-5);
    fm1_mod_new(&other);assert(fm1_mod_cell_set(&other,0,64,0,(fm1_mod_cell){0,0,0,0})==-1);
}
static void test_library(void){
    fm1_mod_library lib={0};char line[80];char name[21]="STATE OF THE ART";unsigned i;
    memset(name+16,' ',4);name[20]=0;
    assert(fm1_mod_library_line(&lib,"LIB RESET",reply,0)==1);
    assert(!strcmp(answer,"OK LIB RESET\n"));
    memcpy(line,"LIB ADD 000384FC ",17);
    for(i=0;i<20;i++)sprintf(line+17+i*2,"%02X",(unsigned char)name[i]);
    assert(fm1_mod_library_line(&lib,line,reply,0)==1);
    assert(lib.count==1 && lib.entries[0].size==230652);
    assert(fm1_mod_library_line(&lib,"LIB READY",reply,0)==1 && lib.ready);
    assert(fm1_mod_library_pick(&lib)==0);
    assert(fm1_mod_library_line(&lib,"LIB POLL",reply,0)==1);
    assert(!strcmp(answer,"LIB PICK 0001 00\n"));
    assert(fm1_mod_library_line(&lib,"LIB DONE 0000",reply,0)==1 && lib.pending);
    assert(fm1_mod_library_line(&lib,"LIB DONE 0001",reply,0)==1 && !lib.pending && !lib.failed);
    assert(fm1_mod_library_line(&lib,"LIB POLL",reply,0)==1 && !strcmp(answer,"LIB IDLE\n"));
}
static void test_starter_bank(const char *path){
    static int16_t pcm[FM1_MOD_BLOCK*2];fm1_mod_player player;fm1_mod_cell cell;
    FILE *f=fopen(path,"rb");size_t length;unsigned i,nonzero=0;
    assert(f);length=fread(doc.bytes,1,FM1_MOD_CAPACITY,f);
    assert(!ferror(f));assert(fgetc(f)==EOF);assert(!ferror(f));fclose(f);
    assert(length==32828 && fm1_mod_open(&doc,length)==0);
    assert(doc.orders==1 && doc.patterns==1 && doc.sample_offset==2108);
    assert(fm1_mod_cell_get(&doc,0,0,0,&cell)==0 && !cell.period && !cell.sample);
    for(i=1;i<=17;i++){size_t size;const int8_t *sample=fm1_mod_sample(&doc,i,&size,0,0);
        assert(sample && size>0);
    }
    assert(fm1_mod_cell_set(&doc,0,0,0,(fm1_mod_cell){428,1,0,0})==0);
    fm1_mod_player_start(&player,&doc);fm1_mod_render(&player,pcm,FM1_MOD_BLOCK);
    for(i=0;i<FM1_MOD_BLOCK;i++)if(pcm[i*2])nonzero++;
    assert(nonzero>0);
}
static void probe_mod(const char *path){
    static int16_t pcm[FM1_MOD_BLOCK*2];fm1_mod_player player;unsigned block,nonzero=0;
    FILE *f=fopen(path,"rb");size_t length;fm1_mod_cell cell;
    assert(f);length=fread(doc.bytes,1,FM1_MOD_CAPACITY,f);
    assert(!ferror(f));assert(fgetc(f)==EOF);assert(!ferror(f));fclose(f);
    assert(fm1_mod_open(&doc,length)==0);
    assert(doc.orders>=1 && doc.patterns>=1 && doc.sample_offset<=length);
    assert(fm1_mod_cell_get(&doc,doc.bytes[952],0,0,&cell)==0);
    fm1_mod_player_start(&player,&doc);
    for(block=0;block<FM1_MOD_RATE*5u/FM1_MOD_BLOCK;block++){
        unsigned i;fm1_mod_render(&player,pcm,FM1_MOD_BLOCK);
        for(i=0;i<FM1_MOD_BLOCK*2;i++)if(pcm[i])nonzero++;
    }
    assert(nonzero>0);
    printf("parsed and rendered %s: %zu bytes, %u orders, %u patterns\n",path,length,doc.orders,doc.patterns);
}
#include "test_editor_extensions.h"
int main(int argc,char **argv){
    if(argc==3 && !strcmp(argv[1],"--probe")){probe_mod(argv[2]);return 0;}
    if(argc==2){test_starter_bank(argv[1]);puts("starter bank parsed and rendered");return 0;}
    test_document();test_player();test_keyboard_preview();test_all_four_channels_on_mono_speaker();
    test_explicit_stereo_preserves_local_mode_and_sample_preview();
    test_explicit_stereo_bank_preview_panning();
    test_explicit_stereo_four_voices_and_preview_headroom();
    test_invalid_preview_sample_cancels_previous_sound();
    test_empty_preview_sample_cancels_previous_sound();
    test_player_starts_at_selected_order_and_row();test_player_start_at_rejects_invalid_targets();
    test_current_row_waits_for_next_row_trigger_at_speed_six();
    test_current_row_keeps_rendered_row_at_speed_one();
    test_current_position_crosses_order_only_when_next_row_triggers();
    test_pattern_play_starts_selected_pattern_at_selected_row();
    test_pattern_play_rejects_invalid_start_targets();
    test_pattern_play_wraps_row_sixty_three_in_same_order();
    test_pattern_break_uses_requested_row_in_same_order();test_pattern_jump_restarts_same_order();
    test_pattern_jump_and_break_keep_order_and_use_break_row();
    test_song_jump_and_break_still_change_order();
    test_zero_step_note_overwrites_selected_cell_without_moving();
    test_note_entry_advances_by_selected_step();test_note_entry_step_wraps_within_selected_order();
    test_clear_note_preserves_selected_effect();test_clear_effect_preserves_selected_note();
    test_clear_cell_erases_selected_note_and_effect();test_legacy_clear_still_erases_full_selected_cell();
    test_clearing_unchanged_fields_does_not_mark_document_dirty();test_clear_operations_reject_audition_mode();
    test_arpeggio_resets_on_next_row();test_classic_song_effects();
    test_extended_patterns();test_usb();test_invalid();test_library();test_editor_extensions();
    puts("mod tests passed");return 0;
}
