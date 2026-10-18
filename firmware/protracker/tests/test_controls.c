#include "controls.h"
#include "audio_queue.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <limits.h>

static fm1_mod_document document;
static fm1_mod_editor editor;
static fm1_tracker_controls controls;
static void reset(void){
    fm1_mod_new(&document);fm1_mod_editor_init(&editor,&document);
    fm1_tracker_controls_init(&controls);
}
static fm1_tracker_control_event key(unsigned slot,unsigned down,uint32_t now){
    return fm1_tracker_controls_key(&controls,slot,down,now,editor.sample);
}
static void quick_gestures(void){
    static const unsigned buttons[]={0,1,2,3,4,5,10,11};unsigned i;
    for(i=0;i<8;i++){
        fm1_tracker_control_event event;
        reset();editor.sample=17;editor.edit=1;
        assert(!key(6,1,100).kind);assert(!key(buttons[i],1,101).kind);
        assert(editor.sample==17 && editor.edit==1); /* no premature selection */
        event=key(buttons[i],0,150);
        assert(event.kind==FM1_CONTROL_QUICK_SELECT && event.index==i);
        assert(!key(6,0,151).kind); /* chord never triggers EDIT */
        reset();editor.sample=17;
        key(6,1,100);key(buttons[i],1,101);editor.sample=23;
        assert(!fm1_tracker_controls_tick(&controls,700).kind);
        event=fm1_tracker_controls_tick(&controls,701);
        assert(event.kind==FM1_CONTROL_QUICK_BIND && event.index==i && event.sample==17);
        assert(!fm1_tracker_controls_tick(&controls,1000).kind);
        assert(!key(buttons[i],0,1001).kind && !key(6,0,1002).kind);
    }
    reset();key(6,1,1);key(3,1,2);
    assert(!key(6,0,3).kind);
    assert(key(3,0,4).kind==FM1_CONTROL_QUICK_SELECT); /* modifier released first */
    reset();key(6,1,UINT32_MAX-100u);key(0,1,UINT32_MAX-99u);
    assert(fm1_tracker_controls_tick(&controls,501).kind==FM1_CONTROL_QUICK_BIND);
    reset();key(6,1,0);key(0,1,1);
    assert(key(0,0,601).kind==FM1_CONTROL_QUICK_BIND); /* release between timer polls */
    assert(!key(6,0,602).kind);
}
static void modifiers_and_new_song(void){
    fm1_tracker_control_event event;
    reset();assert(!key(6,1,0).kind);event=key(6,0,20);
    assert(event.kind==FM1_CONTROL_KEY && event.index==6);
    key(9,1,0);fm1_tracker_controls_use_save(&controls);assert(!key(9,0,10).kind);
    key(9,1,20);event=key(9,0,30);assert(event.kind==FM1_CONTROL_KEY && event.index==9);
    key(11,1,0);assert(!key(13,1,1).kind);assert(!key(13,0,300).kind);
    assert(!key(11,0,301).kind && !fm1_tracker_controls_tick(&controls,900).kind);
    key(11,1,1000);key(13,1,1001);
    assert(!fm1_tracker_controls_tick(&controls,1600).kind);
    assert(fm1_tracker_controls_tick(&controls,1601).kind==FM1_CONTROL_NEW_SONG);
    assert(!fm1_tracker_controls_tick(&controls,2000).kind);
    assert(!key(11,0,2001).kind && !key(13,0,2002).kind);
    key(11,1,2100);event=key(3,1,2101);
    assert(event.kind==FM1_CONTROL_KEY && event.index==3);
    assert(!key(11,0,2102).kind); /* copy chord must not also run SEQ tap */
}
static void encoder_mapping_and_block(void){
    unsigned i;fm1_mod_cell cell={0,1,0,0};
    reset();editor.row=10;
    fm1_tracker_controls_encoder(&controls,&editor,0,6,INT_MAX);assert(editor.sample==31);
    fm1_tracker_controls_encoder(&controls,&editor,0,6,INT_MIN);assert(editor.sample==1);
    for(i=0;i<4;i++){assert(!fm1_tracker_controls_encoder(&controls,&editor,0,2,1));assert(editor.row==11+i);}
    for(i=0;i<3;i++){assert(!fm1_tracker_controls_encoder(&controls,&editor,0,0,1));assert(editor.channel==1+i);}
    fm1_tracker_controls_encoder(&controls,&editor,1,2,1);assert(editor.row==14);
    fm1_tracker_controls_encoder(&controls,&editor,0,3,1);assert(editor.field==FM1_FIELD_SAMPLE);
    fm1_tracker_controls_encoder(&controls,&editor,0,3,2);assert(editor.field==FM1_FIELD_PARAM);
    fm1_mod_cell_set(&document,0,14,3,cell);
    fm1_tracker_controls_encoder(&controls,&editor,0,4,1);
    fm1_tracker_controls_encoder(&controls,&editor,0,5,2);
    fm1_mod_cell_get(&document,0,14,3,&cell);assert(cell.param==0x21);
    fm1_tracker_controls_encoder(&controls,&editor,1,4,1);
    fm1_mod_cell_get(&document,0,14,3,&cell);assert(cell.param==0x21); /* stopped-only edits */
    editor.field=FM1_FIELD_NOTE;editor.step=0;
    for(i=1;i<8;i++){fm1_tracker_controls_encoder(&controls,&editor,0,5,1);assert(editor.step==fm1_mod_entry_steps[i]);}
    fm1_tracker_controls_encoder(&controls,&editor,0,5,1);assert(editor.step==0);
    editor.row=20;editor.channel=1;key(11,1,0);
    fm1_tracker_controls_encoder(&controls,&editor,0,2,3);
    fm1_tracker_controls_encoder(&controls,&editor,0,0,1);
    assert(editor.block_active && editor.block_anchor_row==20 && editor.block_end_row==23);
    assert(editor.block_anchor_channel==1 && editor.block_end_channel==2);
    assert(!key(11,0,10).kind);
    fm1_tracker_controls_encoder(&controls,&editor,0,2,1);
    assert(editor.row==24 && editor.block_end_row==23); /* retained selection */
}
static void audio_slots_survive_counter_wrap(void){
    uint32_t samples[FM1_PCM_RING_FRAMES],read=UINT32_MAX-511u,write=read;
    unsigned read_slot=0,write_slot=0,cycle,i;
    /* Same 128-frame production, 64-frame DAC service and 1470-frame prime
     * watermark as target.c. Check chronology across counter and slot wraps. */
    for(cycle=0;cycle<12;cycle++)for(i=0;i<128;i++){
        samples[write_slot]=write++;write_slot=fm1_pcm_next_slot(write_slot);
    }
    assert(write-read==1536u && write-read>=1470u);
    for(cycle=0;cycle<4096;cycle++){
        for(i=0;i<64;i++){
            assert(read!=write && samples[read_slot]==read);
            read++;read_slot=fm1_pcm_next_slot(read_slot);
        }
        if(write-read<=FM1_PCM_RING_FRAMES-128u)for(i=0;i<128;i++){
            samples[write_slot]=write++;write_slot=fm1_pcm_next_slot(write_slot);
        }
        assert(write-read<=FM1_PCM_RING_FRAMES && read_slot<FM1_PCM_RING_FRAMES && write_slot<FM1_PCM_RING_FRAMES);
    }
}
int main(void){quick_gestures();modifiers_and_new_song();encoder_mapping_and_block();audio_slots_survive_counter_wrap();puts("tracker controls passed");return 0;}
