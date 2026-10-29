#include "view.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

static fm1_mod_document document;
static fm1_mod_editor editor;
static fm1_mod_player player;
static fm1_mod_library library;
static fm1_tracker_view view;
static uint8_t pixels[480];
static unsigned color(unsigned column){
    unsigned x=column*6+5;fm1_tracker_view_render(&view,164,1,pixels);
    return ((unsigned)pixels[2*x]<<8)|pixels[2*x+1];
}
static void update(unsigned running){fm1_tracker_view_update(&view,&document,&player,&editor,&library,0,running,1,0);}
static void compact_workflow_states(void){
    fm1_tracker_view_context context={0};fm1_mod_cell cell={428,3,10,0xa5};
    uint8_t before[480],after[480];unsigned i;
    fm1_mod_new(&document);fm1_mod_editor_init(&editor,&document);
    editor.row=18;editor.channel=2;editor.sample=7;
    fm1_mod_cell_set(&document,0,18,2,cell);
    editor.field=FM1_FIELD_SAMPLE;update(0);assert(strstr(view.ui[2],"SMP 03 IN07"));
    editor.field=FM1_FIELD_PARAM;update(0);assert(strstr(view.ui[2],"PARAM A5 K3 LO5 K4 HIA"));
    assert((view.beat_rows&(1u<<2)) && (view.beat_rows&(1u<<6)) && !(view.beat_rows&(1u<<4)));
    context.undo_count=12;context.redo_count=2;context.mute_mask=4;
    fm1_tracker_view_context_apply(&view,&document,&editor,&context);
    assert(strstr(view.ui[0],"U12R02") && view.grid[0][26]=='M');
    assert(view.grid[0][8]!='M' && view.grid[0][17]!='M' && view.grid[0][35]!='M');
    fm1_tracker_view_render(&view,164,1,before);
    context.help_open=1;context.help_topic=1;fm1_tracker_view_context_apply(&view,&document,&editor,&context);
    assert(view.overlay==1 && strstr(view.ui[0],"HELP 2/5") && strstr(view.ui[1],"UNDO"));
    fm1_tracker_view_render(&view,164,1,after);assert(!memcmp(before,after,sizeof(before)));
    context.confirm=FM1_VIEW_CONFIRM_NEW;strcpy(context.notice,"OLD SAMPLE NOTICE");
    fm1_tracker_view_context_apply(&view,&document,&editor,&context);
    assert(view.overlay==2 && !strcmp(view.ui[0],"NEW EMPTY SONG?") && strstr(view.ui[2],"REC CREATE"));
    context.confirm=FM1_VIEW_CONFIRM_BANK;strcpy(context.bank_name,"NEW BANK SOUND");
    fm1_tracker_view_context_apply(&view,&document,&editor,&context);
    assert(strstr(view.ui[0],"REPLACE SONG SAMPLE S07?") && strstr(view.ui[3],"FX REPLACE"));
    assert(!strcmp(view.ui[4],"RESETS UNDO/REDO HISTORY"));
    context.help_open=context.confirm=0;context.arranger=1;context.notice[0]=0;update(0);
    fm1_tracker_view_context_apply(&view,&document,&editor,&context);
    assert(strstr(view.ui[0],"ORDER O00") && !strncmp(view.ui[1],">00:00",6));
    memset(context.notice,'X',39);context.notice[39]=0;
    fm1_tracker_view_context_apply(&view,&document,&editor,&context);
    for(i=0;i<10;i++)assert(strlen(view.ui[i])<=FM1_VIEW_PATTERN_COLUMNS);
    assert(!strcmp(view.ui[2]+27,"..."));
    player.bpm=0;player.speed=0;update(0);assert(strstr(view.ui[0],"B125/06"));
    player.bpm=150;player.speed=3;update(1);
    assert(strstr(view.ui[0],"B150/03") && !strcmp(view.ui[2],"KEY AUDITION STOP TO EDIT"));
    editor.field=FM1_FIELD_NOTE;update(1);assert(!strstr(view.ui[2],"REC WRITE"));
    update(0);assert(strstr(view.ui[0],"B150/03")); /* stopped retains last known tempo */
    player.bpm=0;player.speed=0;
    library.count=1;snprintf(library.entries[0].name,sizeof(library.entries[0].name),"LONG SAMPLE FILE NAME");
    fm1_tracker_view_update(&view,&document,&player,&editor,&library,1,0,1,0);
    for(i=0;i<10;i++)assert(strlen(view.ui[i])<=FM1_VIEW_LIST_COLUMNS);
}
static void sample_waveform_is_exact_and_read_only(void){
    int8_t pcm[240]={0};fm1_tracker_view_context context={0};
    uint32_t crc;unsigned dirty,i;static uint8_t full[240*480],strips[240*480];
    fm1_mod_new(&document);fm1_mod_editor_init(&editor,&document);
    pcm[1]=127;pcm[238]=-128;
    assert(!fm1_mod_replace_sample(&document,1,"MINMAX",pcm,sizeof(pcm),56,0,120,120));
    crc=fm1_mod_crc32(document.bytes,document.length);dirty=document.dirty;
    fm1_tracker_view_update(&view,&document,&player,&editor,&library,2,0,1,0);
    assert(view.wave_valid && view.wave_min[0]==0 && view.wave_max[0]==127);
    assert(view.wave_min[119]==-128 && view.wave_max[119]==0);
    for(i=1;i<119;i++)assert(view.wave_min[i]==0 && view.wave_max[i]==0);
    assert(view.wave_loop && view.wave_start==120 && view.wave_end==239);
    context.sample_open=1;context.sample_field=FM1_SAMPLE_FIELD_LOOP_LENGTH;
    context.undo_count=3;fm1_tracker_view_context_apply(&view,&document,&editor,&context);
    assert(strstr(view.ui[5],"LOOP LENGTH 000120") && strstr(view.ui[6],"K3 2B K4 256B"));
    assert(strstr(view.ui[0],"U03R00"));
    fm1_tracker_view_render(&view,0,240,full);
    for(i=0;i<240;i+=3)fm1_tracker_view_render(&view,i,3,strips+i*480);
    assert(!memcmp(full,strips,sizeof(full)));
    assert(document.dirty==dirty && fm1_mod_crc32(document.bytes,document.length)==crc);
    assert(sizeof(fm1_tracker_view)<=1512); /* baseline1012 plus bounded500-byte growth */
}
int main(void){
    fm1_tracker_view_context context={0};unsigned field,base=13;
    uint8_t upper_name[480];
    static const unsigned start[]={0,3,5,6},finish[]={3,5,6,8};
    fm1_mod_new(&document);fm1_mod_editor_init(&editor,&document);editor.row=18;editor.channel=1;
    for(field=0;field<4;field++){
        unsigned col;editor.field=(uint8_t)field;update(0);
        for(col=0;col<8;col++){
            unsigned selected=col>=start[field] && col<finish[field];
            assert((color(base+col)==color(base+start[field]))==selected);
        }
    }
    editor.row=0;update(0);assert(view.grid[1][0]==0 && view.grid[4][0]==0);
    assert(!strncmp(view.grid[5],">00 ",4));
    editor.row=63;update(0);assert(!strncmp(view.grid[5],">63 ",4) && view.grid[6][0]==0);
    player.current_row=27;player.current_order=0;update(1);assert(!strncmp(view.grid[5],">27 ",4));
    editor.row=20;editor.channel=2;fm1_mod_editor_block_start(&editor);
    editor.row=18;editor.channel=1;fm1_mod_editor_block_update(&editor);update(0);
    assert(view.block_mask[4]==6 && view.block_mask[5]==6 && view.block_mask[6]==6);
    assert(!view.block_mask[3] && !view.block_mask[7]);
    context.arranger=1;fm1_tracker_view_context_apply(&view,&document,&editor,&context);
    assert(strstr(view.ui[0],"ORDER") && !view.page && !strncmp(view.grid[5],">18 ",4));
    context.arranger=0;context.bank_open=1;context.bank_count=32;context.bank_index=6;
    strcpy(context.bank_name,"EP MAJ7 STAB");fm1_tracker_view_context_apply(&view,&document,&editor,&context);
    assert(strstr(view.ui[0],"BANK 07/32") && strstr(view.ui[1],"EP MAJ7") && !view.page);
    fm1_tracker_view_render(&view,22,1,upper_name);
    strcpy(context.bank_name,"ep maj7 stab");fm1_tracker_view_context_apply(&view,&document,&editor,&context);
    fm1_tracker_view_render(&view,22,1,pixels);assert(!memcmp(pixels,upper_name,sizeof(pixels)));
    strcpy(context.notice,"Q1 BOUND S04 KICK");fm1_tracker_view_context_apply(&view,&document,&editor,&context);
    assert(!strcmp(view.ui[2],context.notice));
    compact_workflow_states();sample_waveform_is_exact_and_read_only();
    printf("tracker view passed (snapshot=%u bytes)\n",(unsigned)sizeof(fm1_tracker_view));return 0;
}
