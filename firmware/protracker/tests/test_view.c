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
    puts("tracker view passed");return 0;
}
