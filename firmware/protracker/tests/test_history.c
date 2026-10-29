#include "history.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

static fm1_mod_document song;
static fm1_mod_editor editor;
static fm1_mod_history history;
static uint8_t before[FM1_MOD_CAPACITY],after[FM1_MOD_CAPACITY];
static const int8_t pcm[8]={64,32,16,0,-16,-32,-64,0};

static void clean(void){assert(!fm1_mod_open(&song,song.length) && !song.dirty);}
static void reset(unsigned sample){
    fm1_mod_new(&song);
    if(sample)assert(!fm1_mod_replace_sample(&song,1,"ORIGINAL",pcm,8,64,0,0,8));
    clean();fm1_mod_editor_init(&editor,&song);fm1_mod_history_init(&history,&song);
}
static fm1_mod_cell cell(unsigned pattern,unsigned row,unsigned ch){
    fm1_mod_cell result;assert(!fm1_mod_cell_get(&song,pattern,row,ch,&result));return result;
}
static void record_note(unsigned row,unsigned midi){
    editor.row=(uint8_t)row;
    assert(!fm1_mod_history_begin(&history,&editor,FM1_HISTORY_PATTERN));
    assert(!fm1_mod_editor_note(&editor,midi));assert(fm1_mod_history_commit(&history,&editor)==1);
}
static void history_pattern_saved_and_branch(void){
    size_t length;reset(1);length=song.length;memcpy(before,song.bytes,length);
    editor.channel=2;editor.field=FM1_FIELD_NOTE;record_note(5,60);memcpy(after,song.bytes,length);
    assert(history.cursor==1 && song.dirty && cell(0,5,2).period==214);
    editor.sample=7;editor.step=8;editor.clipboard_valid=1;editor.quick[0]=17;
    assert(!fm1_mod_history_undo(&history,&editor));
    assert(!memcmp(before,song.bytes,length) && !song.dirty && editor.row==5 && editor.channel==2);
    assert(editor.field==FM1_FIELD_NOTE && editor.sample==7 && editor.step==8 && editor.clipboard_valid && editor.quick[0]==17);
    assert(!fm1_mod_history_redo(&history,&editor));
    assert(!memcmp(after,song.bytes,length) && song.dirty && editor.row==6);
    fm1_mod_history_mark_saved(&history);assert(!song.dirty);
    assert(!fm1_mod_history_undo(&history,&editor) && song.dirty);
    assert(!fm1_mod_history_redo(&history,&editor) && !song.dirty);
    assert(!fm1_mod_history_undo(&history,&editor));
    assert(!fm1_mod_history_begin(&history,&editor,FM1_HISTORY_PATTERN));
    assert(!fm1_mod_editor_clear_all(&editor));assert(!fm1_mod_history_commit(&history,&editor));
    assert(fm1_mod_history_redo_count(&history)==1);
    assert(!fm1_mod_history_begin(&history,&editor,FM1_HISTORY_PATTERN));
    assert(fm1_mod_editor_set_field(&editor,72)==-1);fm1_mod_history_cancel(&history);
    assert(fm1_mod_history_redo_count(&history)==1);
    record_note(5,61);assert(!fm1_mod_history_redo_count(&history) && song.dirty);
    assert(cell(0,5,2).period==202); /* A branch must not reuse the exported state's ID. */
}
static void history_depth_and_header(void){
    unsigned i;size_t length;reset(1);
    for(i=0;i<12;i++)record_note(i,48+i%12);
    assert(fm1_mod_history_undo_count(&history)==8);
    for(i=0;i<8;i++)assert(!fm1_mod_history_undo(&history,&editor));
    for(i=0;i<12;i++)assert((cell(0,i,0).period!=0)==(i<4));
    assert(song.dirty && fm1_mod_history_undo(&history,&editor)==-1);
    for(i=0;i<8;i++)assert(!fm1_mod_history_redo(&history,&editor));
    reset(1);assert(fm1_mod_add_pattern(&song)==1);clean();fm1_mod_history_init(&history,&song);
    length=song.length;memcpy(before,song.bytes,length);
    assert(!fm1_mod_history_begin(&history,&editor,FM1_HISTORY_HEADER));
    assert(!fm1_mod_set_title(&song,"EDITABLE SONG"));assert(!fm1_mod_set_sample_volume(&song,1,47));
    assert(!fm1_mod_set_sample_finetune(&song,1,15));assert(!fm1_mod_set_sample_loop(&song,1,2,4));
    assert(!fm1_mod_insert_order(&song,0,1) && !fm1_mod_duplicate_order(&song,0));
    assert(fm1_mod_history_commit(&history,&editor)==1);memcpy(after,song.bytes,length);
    assert(song.orders==3 && song.bytes[45]==47 && song.bytes[44]==15);
    assert(!fm1_mod_history_undo(&history,&editor));assert(!memcmp(before,song.bytes,length) && song.orders==1 && !song.dirty);
    assert(!fm1_mod_history_redo(&history,&editor));assert(!memcmp(after,song.bytes,length) && song.orders==3);
    assert(!memcmp(fm1_mod_sample(&song,1,0,0,0),pcm,8));
}
static void history_structure_and_pcm(void){
    size_t old_length,new_length;unsigned i;int created;
    reset(1);assert(!fm1_mod_cell_set(&song,0,7,1,(fm1_mod_cell){428,1,0xa,0x12}));clean();
    fm1_mod_history_init(&history,&song);old_length=song.length;memcpy(before,song.bytes,old_length);
    assert(!fm1_mod_history_begin(&history,&editor,FM1_HISTORY_STRUCTURE));
    created=fm1_mod_duplicate_pattern(&song,0);assert(created==1);
    assert(!fm1_mod_set_order(&song,0,1) && !fm1_mod_append_order(&song,0));
    assert(fm1_mod_history_commit(&history,&editor)==1);new_length=song.length;memcpy(after,song.bytes,new_length);
    for(i=0;i<3;i++){
        assert(!fm1_mod_history_undo(&history,&editor));
        assert(song.length==old_length && song.patterns==1 && song.orders==1 && song.sample_offset==2108);
        assert(!memcmp(before,song.bytes,old_length) && !memcmp(fm1_mod_sample(&song,1,0,0,0),pcm,8));
        assert(!fm1_mod_history_redo(&history,&editor));
        assert(song.length==new_length && song.patterns==2 && song.orders==2);
        assert(!memcmp(after,song.bytes,new_length) && !memcmp(fm1_mod_sample(&song,1,0,0,0),pcm,8));
    }
    editor.row=7;editor.channel=1;editor.field=FM1_FIELD_NOTE;
    assert(!fm1_mod_history_begin(&history,&editor,FM1_HISTORY_PATTERN));
    assert(!fm1_mod_editor_adjust_field(&editor,1));assert(fm1_mod_history_commit(&history,&editor)==1);
    assert(cell(1,7,1).period==404 && cell(0,7,1).period==428);
    assert(!fm1_mod_history_undo(&history,&editor));assert(!fm1_mod_history_undo(&history,&editor));
    assert(!memcmp(before,song.bytes,old_length));
    assert(!fm1_mod_history_redo(&history,&editor) && !fm1_mod_history_redo(&history,&editor));
    assert(cell(1,7,1).period==404 && !memcmp(fm1_mod_sample(&song,1,0,0,0),pcm,8));
    reset(1);assert(!fm1_mod_history_begin(&history,&editor,FM1_HISTORY_STRUCTURE));
    assert(fm1_mod_add_pattern(&song)==1);assert(fm1_mod_history_commit(&history,&editor)==1);
    assert(!fm1_mod_history_undo(&history,&editor) && song.patterns==1);
    assert(!fm1_mod_history_redo(&history,&editor) && song.patterns==2);
    for(i=0;i<1024;i++)assert(!song.bytes[2108+i]);
}
static void history_barriers_and_stale_state(void){
    static const int8_t replacement[10]={1,2,3,4,5,6,7,8,9,10};size_t length;
    reset(1);record_note(1,48);assert(!fm1_mod_history_begin(&history,&editor,FM1_HISTORY_HEADER));
    assert(!fm1_mod_replace_sample(&song,1,"LARGER",replacement,10,32,0,0,0));
    assert(fm1_mod_history_commit(&history,&editor)==-1 && !fm1_mod_history_undo_count(&history));
    assert(!memcmp(fm1_mod_sample(&song,1,0,0,0),replacement,10));
    reset(1);record_note(1,48);assert(!fm1_mod_replace_sample(&song,1,"REPLACE",replacement,8,32,0,0,0));
    fm1_mod_history_init(&history,&song);assert(song.dirty && fm1_mod_history_undo(&history,&editor)==-1);
    assert(!memcmp(fm1_mod_sample(&song,1,0,0,0),replacement,8));
    reset(0);record_note(1,48);assert(!fm1_mod_cell_set(&song,0,9,3,(fm1_mod_cell){214,1,0,0}));
    length=song.length;memcpy(before,song.bytes,length);
    assert(fm1_mod_history_undo(&history,&editor)==-1 && !memcmp(before,song.bytes,length));
    assert(!fm1_mod_history_undo_count(&history));
    editor.order=128;assert(fm1_mod_history_begin(&history,&editor,FM1_HISTORY_PATTERN)==-1);
    editor.order=0;assert(!fm1_mod_history_begin(&history,&editor,FM1_HISTORY_PATTERN));
    assert(fm1_mod_history_begin(&history,&editor,FM1_HISTORY_HEADER)==-1);fm1_mod_history_cancel(&history);
}
static void compact_history_scope_and_recipe_failures(void){
    size_t length;int created;
    reset(1);record_note(1,48);
    assert(!fm1_mod_history_begin(&history,&editor,FM1_HISTORY_PATTERN));
    assert(!fm1_mod_set_sample_volume(&song,1,30));
    assert(fm1_mod_history_commit(&history,&editor)==-1);
    assert(!fm1_mod_history_undo_count(&history) && song.bytes[45]==30);
    reset(0);assert(!fm1_mod_history_begin(&history,&editor,FM1_HISTORY_HEADER));
    assert(!fm1_mod_cell_set(&song,0,0,0,(fm1_mod_cell){428,1,0,0}));
    assert(fm1_mod_history_commit(&history,&editor)==-1 && !fm1_mod_history_undo_count(&history));
    assert(cell(0,0,0).period==428);
    reset(1);assert(!fm1_mod_cell_set(&song,0,7,1,(fm1_mod_cell){428,1,0xa,0x12}));
    clean();fm1_mod_history_init(&history,&song);
    assert(!fm1_mod_history_begin(&history,&editor,FM1_HISTORY_STRUCTURE));
    created=fm1_mod_duplicate_pattern(&song,0);assert(created==1 && !fm1_mod_set_order(&song,0,1));
    assert(fm1_mod_history_commit(&history,&editor)==1);
    assert(!fm1_mod_history_undo(&history,&editor));
    /* An out-of-band source edit must reject clone reconstruction before PCM
     * moves or header changes. The user's current bytes remain untouched. */
    assert(!fm1_mod_cell_set(&song,0,7,1,(fm1_mod_cell){404,1,0xa,0x12}));
    length=song.length;memcpy(before,song.bytes,length);
    assert(fm1_mod_history_redo(&history,&editor)==-1);
    assert(song.length==length && song.patterns==1 && !memcmp(before,song.bytes,length));
    assert(!memcmp(fm1_mod_sample(&song,1,0,0,0),pcm,8));
    assert(!fm1_mod_history_undo_count(&history) && !fm1_mod_history_redo_count(&history));
    reset(1);assert(!fm1_mod_history_begin(&history,&editor,FM1_HISTORY_STRUCTURE));
    assert(fm1_mod_add_pattern(&song)==1 && !fm1_mod_set_order(&song,0,1));
    /* A pattern that is neither blank nor the captured source clone cannot
     * be reconstructed honestly within the compact structural record. */
    assert(!fm1_mod_cell_set(&song,1,0,0,(fm1_mod_cell){214,1,0,0}));
    assert(fm1_mod_history_commit(&history,&editor)==-1 && !fm1_mod_history_undo_count(&history));
    assert(song.patterns==2 && cell(1,0,0).period==214);
}
static void compact_history_blank_clone_chain(void){
    unsigned i;size_t original_length,final_length;int created;
    reset(1);assert(!fm1_mod_cell_set(&song,0,4,2,(fm1_mod_cell){428,1,0xe,0x93}));
    clean();fm1_mod_history_init(&history,&song);
    original_length=song.length;memcpy(before,song.bytes,original_length);
    for(i=1;i<=8;i++){
        if(i>1 && (i&1))editor.order=1; /* Clone the unchanged, nonempty pattern1. */
        assert(!fm1_mod_history_begin(&history,&editor,FM1_HISTORY_STRUCTURE));
        created=i&1?fm1_mod_duplicate_pattern(&song,(unsigned)fm1_mod_get_order(&song,editor.order)):
            fm1_mod_add_pattern(&song);
        assert(created==(int)i && !fm1_mod_append_order(&song,(unsigned)created));
        editor.order=(uint8_t)(song.orders-1);
        assert(fm1_mod_history_commit(&history,&editor)==1);
    }
    final_length=song.length;memcpy(after,song.bytes,final_length);
    assert(song.patterns==9 && song.orders==9 && fm1_mod_history_undo_count(&history)==8);
    for(i=0;i<8;i++)assert(!fm1_mod_history_undo(&history,&editor));
    assert(song.length==original_length && song.patterns==1 && song.orders==1 && !song.dirty);
    assert(!memcmp(before,song.bytes,original_length) && !memcmp(fm1_mod_sample(&song,1,0,0,0),pcm,8));
    for(i=0;i<8;i++)assert(!fm1_mod_history_redo(&history,&editor));
    assert(song.length==final_length && song.patterns==9 && song.orders==9);
    assert(!memcmp(after,song.bytes,final_length) && !memcmp(fm1_mod_sample(&song,1,0,0,0),pcm,8));
    for(i=1;i<=8;i++)assert(cell(i,4,2).period==(i&1?428:0));
}
static void select_block(unsigned top,unsigned bottom,unsigned left,unsigned right){
    editor.row=(uint8_t)top;editor.channel=(uint8_t)left;assert(!fm1_mod_editor_block_start(&editor));
    editor.row=(uint8_t)bottom;editor.channel=(uint8_t)right;assert(!fm1_mod_editor_block_update(&editor));
}
static void row_and_block_commands(void){
    unsigned row,ch;fm1_mod_cell c;reset(0);editor.edit=0;
    for(row=0;row<64;row++)for(ch=0;ch<4;ch++)assert(!fm1_mod_cell_set(&song,0,row,ch,
        (fm1_mod_cell){428,(uint8_t)(ch+1),(uint8_t)(ch+1),(uint8_t)row}));
    clean();fm1_mod_history_init(&history,&song);memcpy(before,song.bytes,song.length);
    editor.row=10;editor.channel=2;assert(!fm1_mod_history_begin(&history,&editor,FM1_HISTORY_PATTERN));
    assert(!fm1_mod_editor_insert_row(&editor));assert(fm1_mod_history_commit(&history,&editor)==1);
    assert(!cell(0,10,2).period && cell(0,11,2).param==10 && cell(0,63,2).param==62);
    assert(cell(0,10,1).param==10 && editor.row==10 && editor.channel==2);
    assert(!fm1_mod_history_undo(&history,&editor));assert(!memcmp(before,song.bytes,song.length));
    assert(!fm1_mod_editor_delete_row(&editor));assert(cell(0,10,2).param==11 && !cell(0,63,2).period);
    assert(cell(0,63,1).param==63 && editor.row==10);
    reset(0);editor.edit=0;select_block(4,8,1,2);editor.row=6;editor.channel=2;
    assert(!fm1_mod_cell_set(&song,0,4,1,(fm1_mod_cell){428,7,0xe,0xd3}));
    assert(!fm1_mod_cell_set(&song,0,5,1,(fm1_mod_cell){214,3,0xa,0x12}));
    clean();assert(!fm1_mod_editor_block_fill(&editor,2));
    for(row=4;row<=8;row+=2)for(ch=1;ch<=2;ch++){
        c=cell(0,row,ch);assert(c.period==428 && c.sample==7 && c.effect==0xe && c.param==0xd3);
    }
    assert(cell(0,5,1).period==214 && !cell(0,5,2).period);assert(!fm1_mod_editor_block_transpose(&editor,1));
    c=cell(0,4,1);assert(c.period==404 && c.sample==7 && c.effect==0xe && c.param==0xd3);
    assert(!cell(0,5,2).period);assert(!fm1_mod_editor_block_cut(&editor));
    assert(editor.clipboard_valid && editor.clipboard_width==2 && editor.clipboard_height==5);
    for(row=4;row<=8;row++)for(ch=1;ch<=2;ch++)assert(!cell(0,row,ch).period);
    editor.row=62;editor.channel=3;assert(!fm1_mod_editor_block_paste(&editor));
    assert(cell(0,62,3).period==404 && cell(0,63,3).period==202); /* clipped two rows/one channel */
    clean();assert(!fm1_mod_editor_block_clear_cells(&editor) && !song.dirty);
    memcpy(before,song.bytes,song.length);assert(fm1_mod_editor_block_fill(&editor,65)==-1);
    editor.block_order=1;assert(fm1_mod_editor_block_cut(&editor)==-1 && fm1_mod_editor_block_transpose(&editor,1)==-1);
    assert(!memcmp(before,song.bytes,song.length));
    reset(0);editor.edit=0;select_block(3,6,0,0);
    assert(!fm1_mod_cell_set(&song,0,3,0,(fm1_mod_cell){428,2,0xc,0x20}));
    assert(!fm1_mod_editor_block_fill(&editor,0));
    for(row=3;row<=6;row++)assert(cell(0,row,0).period==428 && cell(0,row,0).sample==2);
    reset(0);editor.row=63;editor.channel=3;assert(!fm1_mod_editor_insert_row(&editor) && !song.dirty);
    assert(!fm1_mod_editor_delete_row(&editor) && !song.dirty);
}
static void loop_metadata_and_mute(void){
    static const int8_t constant[8]={64,64,64,64,64,64,64,64};
    fm1_mod_player audible,muted;int16_t a[16],b[16];unsigned ch;size_t length,start,repeat;
    reset(1);assert(!fm1_mod_set_sample_loop(&song,1,2,4));
    assert(fm1_mod_sample(&song,1,&length,&start,&repeat) && length==8 && start==2 && repeat==4);
    clean();assert(!fm1_mod_set_sample_loop(&song,1,2,4) && !song.dirty);memcpy(before,song.bytes,song.length);
    assert(fm1_mod_set_sample_loop(&song,0,0,0)==-1 && fm1_mod_set_sample_loop(&song,32,0,0)==-1);
    assert(fm1_mod_set_sample_loop(&song,1,1,4)==-1 && fm1_mod_set_sample_loop(&song,1,2,3)==-1);
    assert(fm1_mod_set_sample_loop(&song,1,6,4)==-1 && fm1_mod_set_sample_loop(&song,1,SIZE_MAX,0)==-1);
    assert(!memcmp(before,song.bytes,song.length));assert(!fm1_mod_set_sample_loop(&song,1,0,0));
    assert(!fm1_mod_replace_sample(&song,1,"CONSTANT",constant,8,64,0,0,8));
    for(ch=0;ch<4;ch++)assert(!fm1_mod_cell_set(&song,0,0,ch,(fm1_mod_cell){428,1,0,0}));
    clean();fm1_mod_player_start(&audible,&song);audible.stereo=1;
    fm1_mod_player_start(&muted,&song);muted.stereo=1;muted.mute_mask=10;
    fm1_mod_render(&audible,a,8);fm1_mod_render(&muted,b,8);
    assert(a[0]==8192 && a[1]==8192 && b[0]==4096 && b[1]==4096);
    for(ch=0;ch<4;ch++)assert(audible.voices[ch].phase==muted.voices[ch].phase);
    muted.mute_mask=15;assert(!fm1_mod_preview_start(&muted,&song,1,48,1));fm1_mod_render(&muted,b,1);
    assert(!b[0] && b[1]==4096 && !song.dirty);
}
int main(void){
    history_pattern_saved_and_branch();history_depth_and_header();history_structure_and_pcm();
    history_barriers_and_stale_state();compact_history_scope_and_recipe_failures();compact_history_blank_clone_chain();
    row_and_block_commands();loop_metadata_and_mute();
    printf("tracker history/edit commands passed; history=%zu bytes\n",sizeof(history));return 0;
}
