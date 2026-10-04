/* Milestone #2 core regressions. Included by test_mod.c after legacy tests. */
#include <limits.h>

static fm1_mod_document extension_before;
static int8_t extension_pcm[131070];
static void clean_document(void){assert(fm1_mod_open(&doc,doc.length)==0 && !doc.dirty);}
static void remember_document(void){memcpy(&extension_before,&doc,sizeof(doc));}
static void unchanged_document(void){assert(!memcmp(&extension_before,&doc,sizeof(doc)));}
static fm1_mod_cell selected_cell(const fm1_mod_editor *e){
    fm1_mod_cell cell;int pattern=fm1_mod_get_order(e->document,e->order);
    assert(pattern>=0 && !fm1_mod_cell_get(e->document,(unsigned)pattern,e->row,e->channel,&cell));return cell;
}
static void field_fixture(fm1_mod_editor *e){
    fm1_mod_new(&doc);fm1_mod_editor_init(e,&doc);e->row=10;e->channel=2;
    assert(!fm1_mod_cell_set(&doc,0,10,2,(fm1_mod_cell){428,3,0xa,0x37}));clean_document();
}
static void test_editor_fields_and_nibbles(void){
    fm1_mod_editor e;fm1_mod_cell c;unsigned i;
    field_fixture(&e);remember_document();
    for(i=0;i<FM1_FIELD_COUNT;i++){assert(e.field==i);assert(!fm1_mod_editor_field_move(&e,1));}
    assert(e.field==FM1_FIELD_NOTE);assert(!fm1_mod_editor_field_move(&e,-1));assert(e.field==FM1_FIELD_PARAM);
    assert(!fm1_mod_editor_field_move(&e,INT_MAX));assert(e.field==FM1_FIELD_EFFECT);
    assert(!fm1_mod_editor_field_move(&e,INT_MIN));assert(e.field==FM1_FIELD_EFFECT);unchanged_document();
    e.field=FM1_FIELD_NOTE;assert(!fm1_mod_editor_adjust_field(&e,1));c=selected_cell(&e);
    assert(c.period==404 && c.sample==3 && c.effect==0xa && c.param==0x37);
    assert(!fm1_mod_editor_transpose(&e,-2));assert(selected_cell(&e).period==453);
    assert(!fm1_mod_editor_set_field(&e,36));assert(!fm1_mod_editor_adjust_field(&e,INT_MIN));
    assert(selected_cell(&e).period==856);clean_document();
    assert(!fm1_mod_editor_adjust_field(&e,-1) && !doc.dirty);
    assert(!fm1_mod_editor_adjust_field(&e,INT_MAX));assert(selected_cell(&e).period==113);
    e.field=FM1_FIELD_SAMPLE;assert(!fm1_mod_editor_set_field(&e,12));c=selected_cell(&e);
    assert(c.period==113 && c.sample==12 && c.effect==0xa && c.param==0x37);
    assert(!fm1_mod_editor_adjust_field(&e,INT_MAX));assert(selected_cell(&e).sample==31);
    assert(!fm1_mod_editor_adjust_field(&e,INT_MIN));assert(!selected_cell(&e).sample);
    e.field=FM1_FIELD_EFFECT;assert(!fm1_mod_editor_set_field(&e,3));assert(selected_cell(&e).param==0x37);
    e.field=FM1_FIELD_PARAM;assert(!fm1_mod_editor_param_nibble(&e,0,3));assert(selected_cell(&e).param==0x3a);
    assert(!fm1_mod_editor_param_nibble(&e,1,-2));assert(selected_cell(&e).param==0x1a);
    assert(!fm1_mod_editor_param_nibble(&e,0,INT_MAX));assert(selected_cell(&e).param==0x1f);
    assert(!fm1_mod_editor_param_nibble(&e,1,INT_MIN));assert(selected_cell(&e).param==0x0f);
    assert(!fm1_mod_editor_adjust_field(&e,INT_MAX));assert(selected_cell(&e).param==255);
    assert(!fm1_mod_editor_adjust_field(&e,INT_MIN));assert(!selected_cell(&e).param);
    assert(e.row==10 && e.channel==2);
    for(i=0;i<FM1_FIELD_COUNT;i++){
        field_fixture(&e);e.field=(uint8_t)i;assert(!fm1_mod_editor_clear_field(&e));c=selected_cell(&e);
        assert(c.period==(i==FM1_FIELD_NOTE?0:428) && c.sample==(i==FM1_FIELD_SAMPLE?0:3));
        assert(c.effect==(i==FM1_FIELD_EFFECT?0:0xa) && c.param==(i==FM1_FIELD_PARAM?0:0x37));
        assert(e.row==10 && e.channel==2);clean_document();
        assert(!fm1_mod_editor_clear_field(&e) && !doc.dirty);
    }
    e.field=FM1_FIELD_NOTE;assert(!fm1_mod_editor_clear_field(&e));clean_document();
    assert(!fm1_mod_editor_transpose(&e,12) && !doc.dirty && !selected_cell(&e).period);
}
static void test_editor_step_quick_and_validation(void){
    static const unsigned steps[]={0,1,2,3,4,6,8,16};
    fm1_mod_editor e;unsigned i;fm1_mod_cell c;
    field_fixture(&e);remember_document();assert(!fm1_mod_editor_set_step(&e,0));
    for(i=0;i<8;i++){
        assert(e.step==steps[i]);assert(fm1_mod_entry_steps[i]==steps[i]);
        assert(!fm1_mod_editor_step_move(&e,1));
    }
    assert(!e.step);assert(!fm1_mod_editor_step_move(&e,-1) && e.step==16);
    assert(!fm1_mod_editor_step_move(&e,INT_MIN) && e.step==16);
    assert(!fm1_mod_editor_step_move(&e,INT_MAX) && e.step==8);
    for(i=0;i<8;i++){assert(e.quick[i]==i+1);assert(!fm1_mod_editor_quick_select(&e,i) && e.sample==i+1);}
    e.sample=31;assert(!fm1_mod_editor_quick_bind(&e,3));e.sample=1;
    assert(!fm1_mod_editor_quick_select(&e,3) && e.sample==31);unchanged_document();
    assert(fm1_mod_editor_quick_bind(&e,8)==-1 && fm1_mod_editor_quick_select(&e,8)==-1);
    e.sample=0;assert(fm1_mod_editor_quick_bind(&e,0)==-1);e.sample=32;
    assert(fm1_mod_editor_quick_bind(&e,0)==-1);e.quick[0]=0;
    assert(fm1_mod_editor_quick_select(&e,0)==-1);e.quick[0]=32;
    assert(fm1_mod_editor_quick_select(&e,0)==-1);assert(fm1_mod_editor_set_step(&e,5)==-1);
    for(i=0;i<8;i++){
        field_fixture(&e);e.row=63;e.sample=7;assert(!fm1_mod_editor_set_step(&e,steps[i]));
        assert(!fm1_mod_cell_set(&doc,0,63,2,(fm1_mod_cell){0,0,0xe,0xd3}));
        assert(!fm1_mod_editor_note(&e,60));assert(e.row==((63+steps[i])&63));
        assert(!fm1_mod_cell_get(&doc,0,63,2,&c));assert(c.period==214 && c.sample==7 && c.effect==0xe && c.param==0xd3);
    }
    field_fixture(&e);remember_document();e.row=64;
    assert(fm1_mod_editor_note(&e,48)==-1 && fm1_mod_editor_effect(&e,1,2)==-1);
    assert(fm1_mod_editor_clear_field(&e)==-1);e.row=10;e.channel=4;
    assert(fm1_mod_editor_adjust_field(&e,1)==-1);e.channel=2;e.order=255;
    assert(fm1_mod_editor_note(&e,48)==-1 && fm1_mod_editor_clear_cell(&e)==-1);
    e.order=0;e.sample=32;assert(fm1_mod_editor_note(&e,48)==-1);e.sample=1;e.field=FM1_FIELD_COUNT;
    assert(fm1_mod_editor_set_field(&e,0)==-1 && fm1_mod_editor_field_move(&e,1)==-1);
    e.field=FM1_FIELD_NOTE;assert(fm1_mod_editor_set_field(&e,72)==-1);
    e.field=FM1_FIELD_SAMPLE;assert(fm1_mod_editor_set_field(&e,32)==-1);
    e.field=FM1_FIELD_EFFECT;assert(fm1_mod_editor_set_field(&e,16)==-1);
    e.field=FM1_FIELD_PARAM;assert(fm1_mod_editor_set_field(&e,256)==-1);
    assert(fm1_mod_editor_param_nibble(&e,2,1)==-1);e.edit=0;
    assert(!fm1_mod_editor_set_field(&e,0x37) && !doc.dirty);
    assert(!fm1_mod_editor_clear_field(&e) && !selected_cell(&e).param);
    assert(!fm1_mod_editor_set_field(&e,0x37));clean_document();unchanged_document();
    assert(!fm1_mod_editor_clear_all(&e));c=selected_cell(&e);
    assert(!c.period && !c.sample && !c.effect && !c.param && e.row==10);
    e.row=0;e.channel=0;fm1_mod_editor_move(&e,INT_MIN,INT_MIN,INT_MIN);
    assert(e.row==0 && e.channel==0 && e.order==0);
    fm1_mod_editor_move(&e,INT_MAX,INT_MAX,INT_MAX);assert(e.row==63 && e.channel==3 && e.order==0);
}
static void test_explicit_edit_actions_and_noops(void){
    fm1_mod_editor e;fm1_mod_cell c;
    field_fixture(&e);e.edit=0;e.field=FM1_FIELD_NOTE;remember_document();
    assert(fm1_mod_editor_note(&e,60)==-1);unchanged_document();
    assert(!fm1_mod_editor_adjust_field(&e,1));assert(selected_cell(&e).period==404);
    e.field=FM1_FIELD_SAMPLE;assert(!fm1_mod_editor_set_field(&e,8));
    e.field=FM1_FIELD_EFFECT;assert(!fm1_mod_editor_adjust_field(&e,1));
    e.field=FM1_FIELD_PARAM;assert(!fm1_mod_editor_param_nibble(&e,1,1));c=selected_cell(&e);
    assert(c.period==404 && c.sample==8 && c.effect==0xb && c.param==0x47);
    assert(!fm1_mod_editor_clear_field(&e));c=selected_cell(&e);
    assert(c.period==404 && c.sample==8 && c.effect==0xb && !c.param && e.row==10);
    assert(!fm1_mod_editor_clear_all(&e));clean_document();remember_document();
    assert(!fm1_mod_editor_clear_all(&e));assert(!fm1_mod_editor_adjust_field(&e,0));
    assert(!fm1_mod_set_title(&doc,""));assert(!fm1_mod_set_sample_name(&doc,1,""));
    assert(!fm1_mod_set_sample_volume(&doc,1,0));assert(!fm1_mod_set_sample_finetune(&doc,1,0));
    unchanged_document();
}
static void test_arranger_extent_and_capacity(void){
    static const int8_t pcm[]={64,32,-64,-32};size_t length,offset;unsigned i;fm1_mod_cell c;
    fm1_mod_new(&doc);assert(!fm1_mod_replace_sample(&doc,1,"KEEP",pcm,4,64,0,0,4));
    assert(fm1_mod_add_pattern(&doc)==1 && fm1_mod_add_pattern(&doc)==2);
    assert(!fm1_mod_cell_set(&doc,2,7,3,(fm1_mod_cell){214,1,9,0x12}));clean_document();
    length=doc.length;offset=doc.sample_offset;
    assert(!fm1_mod_append_order(&doc,1) && !fm1_mod_append_order(&doc,2));
    assert(!fm1_mod_duplicate_order(&doc,1));assert(doc.orders==4 && doc.length==length && doc.sample_offset==offset);
    assert(fm1_mod_get_order(&doc,0)==0 && fm1_mod_get_order(&doc,1)==1 &&
           fm1_mod_get_order(&doc,2)==1 && fm1_mod_get_order(&doc,3)==2);
    doc.bytes[951]=2;assert(!fm1_mod_insert_order(&doc,1,2));assert(doc.orders==5 && doc.bytes[951]==3);
    assert(fm1_mod_get_order(&doc,1)==2 && fm1_mod_get_order(&doc,2)==1);
    assert(!fm1_mod_delete_order(&doc,1) && doc.bytes[951]==2);
    assert(!fm1_mod_delete_order(&doc,3));assert(doc.orders==3 && doc.bytes[1079]==2);
    clean_document();assert(doc.patterns==3 && doc.sample_offset==offset && !memcmp(fm1_mod_sample(&doc,1,0,0,0),pcm,4));
    assert(!fm1_mod_set_order(&doc,0,2) && !fm1_mod_set_order(&doc,0,0));clean_document();
    assert(doc.patterns==3 && doc.sample_offset==offset);remember_document();
    assert(!fm1_mod_set_order(&doc,0,0));unchanged_document();
    assert(fm1_mod_duplicate_pattern(&doc,2)==3);assert(!fm1_mod_cell_get(&doc,3,7,3,&c));
    assert(c.period==214 && c.sample==1 && c.effect==9 && c.param==0x12);
    clean_document();assert(doc.patterns==4 && !memcmp(fm1_mod_sample(&doc,1,0,0,0),pcm,4));
    fm1_mod_new(&doc);for(i=1;i<128;i++)assert(fm1_mod_add_pattern(&doc)==(int)i);
    assert(!memcmp(doc.bytes+1080,"M!K!",4));clean_document();assert(doc.patterns==128);
    remember_document();assert(fm1_mod_add_pattern(&doc)==-1 && fm1_mod_duplicate_pattern(&doc,128)==-1);unchanged_document();
    fm1_mod_new(&doc);assert(fm1_mod_add_pattern(&doc)==1);
    assert(!fm1_mod_set_order(&doc,127,1));clean_document();assert(doc.orders==128);
    remember_document();assert(fm1_mod_append_order(&doc,0)==-1 && fm1_mod_insert_order(&doc,0,0)==-1);
    assert(fm1_mod_duplicate_order(&doc,0)==-1 && fm1_mod_add_pattern(&doc)==-1);
    assert(fm1_mod_set_order(&doc,127,0)==-1);unchanged_document();
    assert(!fm1_mod_delete_order(&doc,127));clean_document();assert(doc.orders==127 && doc.patterns==2);
    remember_document();assert(fm1_mod_get_order(&doc,127)==-1 && fm1_mod_set_order(&doc,128,0)==-1);
    assert(fm1_mod_insert_order(&doc,128,0)==-1 && fm1_mod_delete_order(&doc,127)==-1);unchanged_document();
    fm1_mod_new(&doc);remember_document();assert(fm1_mod_delete_order(&doc,0)==-1);unchanged_document();
}
static void test_arranger_playback_and_stop_sync(void){
    fm1_mod_player p;fm1_mod_editor e;unsigned saved_field,saved_sample,saved_channel;
    fm1_mod_new(&doc);assert(fm1_mod_add_pattern(&doc)==1);
    assert(!fm1_mod_cell_set(&doc,0,0,0,(fm1_mod_cell){428,1,0xf,1}));
    assert(!fm1_mod_cell_set(&doc,1,0,0,(fm1_mod_cell){214,1,0xf,1}));
    assert(!fm1_mod_insert_order(&doc,0,1));assert(!fm1_mod_duplicate_order(&doc,0));
    assert(!fm1_mod_player_start_at(&p,&doc,1,63));render_frames(&p,1);p.speed=1;
    /* Trigger a row boundary explicitly; the editor must use last triggered,
     * rather than the scheduler's next order/row. */
    p.current_order=1;p.current_row=63;p.order=2;p.row=0;
    fm1_mod_editor_init(&e,&doc);e.channel=3;e.sample=7;e.field=FM1_FIELD_PARAM;
    saved_field=e.field;saved_sample=e.sample;saved_channel=e.channel;fm1_mod_player_stop(&p);
    assert(!fm1_mod_editor_sync_player(&e,&p));assert(e.order==1 && e.row==63);
    assert(e.field==saved_field && e.sample==saved_sample && e.channel==saved_channel);
    p.current_order=doc.orders;assert(fm1_mod_editor_sync_player(&e,&p)==-1 && e.row==63);
    assert(!fm1_mod_player_start_at(&p,&doc,0,0));render_frames(&p,1);assert(p.voices[0].base_period==214);
    assert(!fm1_mod_player_start_at(&p,&doc,2,0));render_frames(&p,1);assert(p.voices[0].base_period==428);
}
static void test_block_rectangles_clipping_and_validation(void){
    fm1_mod_editor e;unsigned row,ch;fm1_mod_cell a,b;
    fm1_mod_new(&doc);assert(fm1_mod_add_pattern(&doc)==1);assert(!fm1_mod_append_order(&doc,1));
    for(row=0;row<64;row++)for(ch=0;ch<4;ch++)assert(!fm1_mod_cell_set(&doc,0,row,ch,
        (fm1_mod_cell){fm1_mod_period_for_midi(36+(row+ch)%36),(uint8_t)(1+(row+ch)%31),(uint8_t)((row+ch)&15),(uint8_t)(row*4+ch)}));
    fm1_mod_editor_init(&e,&doc);assert(sizeof(e.clipboard)==1024);e.row=4;e.channel=2;e.edit=0;
    assert(!fm1_mod_editor_block_start(&e));e.row=2;e.channel=1;assert(!fm1_mod_editor_block_update(&e));
    clean_document();assert(!fm1_mod_editor_block_copy(&e) && !doc.dirty);
    assert(e.clipboard_width==2 && e.clipboard_height==3);e.order=1;e.row=10;e.channel=0;e.edit=1;
    assert(!fm1_mod_editor_block_paste(&e));assert(e.row==10 && e.channel==0 && e.order==1);
    for(row=0;row<3;row++)for(ch=0;ch<2;ch++){
        assert(!fm1_mod_cell_get(&doc,0,row+2,ch+1,&a) && !fm1_mod_cell_get(&doc,1,row+10,ch,&b));
        assert(a.period==b.period && a.sample==b.sample && a.effect==b.effect && a.param==b.param);
    }
    e.order=0;e.row=0;e.channel=0;assert(!fm1_mod_editor_block_start(&e));e.row=63;e.channel=3;
    assert(!fm1_mod_editor_block_update(&e) && !fm1_mod_editor_block_copy(&e));
    assert(e.clipboard_width==4 && e.clipboard_height==64);
    e.order=1;e.row=0;e.channel=0;assert(!fm1_mod_editor_block_paste(&e));
    assert(!memcmp(doc.bytes+1084,doc.bytes+2108,1024));clean_document();
    assert(!fm1_mod_editor_block_paste(&e) && !doc.dirty);
    memset(doc.bytes+2108,0,1024);clean_document();remember_document();e.row=62;e.channel=3;
    assert(!fm1_mod_editor_block_paste(&e));
    for(row=0;row<64;row++)for(ch=0;ch<4;ch++){
        assert(!fm1_mod_cell_get(&doc,1,row,ch,&b));
        if(row>=62 && ch==3){assert(!fm1_mod_cell_get(&doc,0,row-62,0,&a));
            assert(a.period==b.period && a.sample==b.sample && a.effect==b.effect && a.param==b.param);}
        else assert(!b.period && !b.sample && !b.effect && !b.param);
    }
    e.edit=0;clean_document();remember_document();assert(!fm1_mod_editor_block_paste(&e) && !doc.dirty);
    e.edit=1;e.clipboard_width=5;assert(fm1_mod_editor_block_paste(&e)==-1);
    e.clipboard_width=4;e.clipboard_height=65;assert(fm1_mod_editor_block_paste(&e)==-1);
    e.clipboard_height=64;e.row=64;assert(fm1_mod_editor_block_paste(&e)==-1);
    e.row=0;e.block_end_row=64;assert(fm1_mod_editor_block_copy(&e)==-1);unchanged_document();
    fm1_mod_editor_block_clear(&e);assert(!e.block_active && e.clipboard_valid);
    assert(fm1_mod_editor_block_copy(&e)==-1);
}
static void test_sample_materialization_transaction_and_budget(void){
    static const int8_t a[]={1,2,3,4},b[]={5,6,7,8,9,10},c[]={11,12,13,14,15,16,17,18};
    const int8_t *same;size_t size,loop,repeat,base;unsigned i;
    fm1_mod_new(&doc);remember_document();assert(!fm1_mod_replace_sample(&doc,1,"",0,0,0,0,0,0));unchanged_document();
    assert(!fm1_mod_replace_sample(&doc,1,"A",a,4,64,0,0,4));
    assert(!fm1_mod_replace_sample(&doc,3,"B",b,6,32,15,0,0));base=doc.sample_offset;
    assert(!fm1_mod_replace_sample(&doc,2,"C",c,8,40,3,2,6));assert(doc.sample_offset==base && doc.length==base+18);
    assert(!memcmp(fm1_mod_sample(&doc,1,0,0,0),a,4) && !memcmp(fm1_mod_sample(&doc,3,0,0,0),b,6));
    same=fm1_mod_sample(&doc,2,&size,&loop,&repeat);assert(size==8 && loop==2 && repeat==6 && !memcmp(same,c,8));
    clean_document();remember_document();assert(!fm1_mod_replace_sample(&doc,2,"C",same,8,40,3,2,6));unchanged_document();
    assert(!fm1_mod_replace_sample(&doc,2,"SHORT",c,2,50,0,0,0));assert(doc.length==base+12);
    assert(!memcmp(fm1_mod_sample(&doc,3,0,0,0),b,6));
    assert(!fm1_mod_replace_sample(&doc,2,"",0,0,0,0,0,0));assert(doc.length==base+10);clean_document();
    assert(!memcmp(fm1_mod_sample(&doc,3,0,0,0),b,6));remember_document();
    assert(fm1_mod_replace_sample(&doc,0,"BAD",a,4,64,0,0,0)==-1);
    assert(fm1_mod_replace_sample(&doc,32,"BAD",a,4,64,0,0,0)==-1);
    assert(fm1_mod_replace_sample(&doc,1,"BAD",0,4,64,0,0,0)==-1);
    assert(fm1_mod_replace_sample(&doc,1,"BAD",a,3,64,0,0,0)==-1);
    assert(fm1_mod_replace_sample(&doc,1,"BAD",a,SIZE_MAX,64,0,0,0)==-1);
    assert(fm1_mod_replace_sample(&doc,1,"BAD",a,4,65,0,0,0)==-1);
    assert(fm1_mod_replace_sample(&doc,1,"BAD",a,4,64,16,0,0)==-1);
    assert(fm1_mod_replace_sample(&doc,1,"BAD",a,4,64,0,1,2)==-1);
    assert(fm1_mod_replace_sample(&doc,1,"BAD",a,4,64,0,2,4)==-1);
    assert(fm1_mod_replace_sample(&doc,1,"BAD",a,4,64,0,0,3)==-1);
    assert(fm1_mod_replace_sample(&doc,1,"BAD",a,4,64,0,SIZE_MAX,0)==-1);
    assert(fm1_mod_replace_sample(&doc,1,"12345678901234567890123",a,4,64,0,0,0)==-1);
    assert(fm1_mod_replace_sample(&doc,1,"ALIAS",fm1_mod_sample(&doc,3,0,0,0),2,64,0,0,0)==-1);unchanged_document();
    for(i=0;i<sizeof(extension_pcm);i++)extension_pcm[i]=(int8_t)i;
    fm1_mod_new(&doc);assert(!fm1_mod_replace_sample(&doc,1,"MAX",extension_pcm,sizeof(extension_pcm),64,0,0,0));
    size=FM1_MOD_CAPACITY-doc.length;assert(!fm1_mod_replace_sample(&doc,2,"FILL",extension_pcm,size,64,0,0,0));
    assert(doc.length==FM1_MOD_CAPACITY);clean_document();remember_document();
    assert(fm1_mod_add_pattern(&doc)==-1);
    assert(fm1_mod_replace_sample(&doc,2,"OVER",extension_pcm,size+2,64,0,0,0)==-1);unchanged_document();
}
static void test_direct_pcm_preview_is_independent_and_validated(void){
    static const int8_t pcm[]={64,32,-64,-32};fm1_mod_player p={0};int16_t audio[2];unsigned i;
    fm1_mod_new(&doc);remember_document();p.document=&doc;p.stereo=1;
    assert(!fm1_mod_preview_pcm(&p,pcm,4,64,0,0,4,48,2));fm1_mod_render(&p,audio,1);
    assert(!audio[0] && audio[1]==4096 && p.preview_active && p.preview_pcm==pcm);unchanged_document();
    render_frames(&p,1024);assert(p.preview_active && p.preview_pcm==pcm);unchanged_document();
    fm1_mod_preview_stop(&p);assert(!p.preview_active && !p.preview_pcm);fm1_mod_render(&p,audio,1);
    assert(!audio[0] && !audio[1]);p.document=0;
    assert(!fm1_mod_preview_pcm(&p,pcm,4,32,15,0,0,48,0));fm1_mod_render(&p,audio,1);
    assert(audio[0]==2048 && !audio[1]);render_frames(&p,1024);assert(!p.preview_active);unchanged_document();
    for(i=0;i<9;i++){
        assert(!fm1_mod_preview_pcm(&p,pcm,4,64,0,0,4,48,0));
        assert(fm1_mod_preview_pcm(&p,i==0?0:pcm,i==1?3:4,i==2?65:64,i==3?16:0,
            i==4?1:i==5?4:0,i==5?4:0,i==6?72:48,i==7?4:0)==(i==8?0:-1));
        if(i!=8)assert(!p.preview_active && !p.preview_pcm);
    }
    assert(fm1_mod_preview_pcm(&p,pcm,SIZE_MAX,64,0,0,0,48,0)==-1);
    assert(!p.preview_active && !p.preview_pcm);unchanged_document();
    assert(!fm1_mod_preview_pcm(&p,pcm,4,64,0,0,4,48,0));fm1_mod_player_stop(&p);
    assert(!p.preview_active && !p.preview_pcm);
    assert(!fm1_mod_replace_sample(&doc,1,"NORMAL",pcm,4,64,0,0,4));clean_document();remember_document();
    assert(!fm1_mod_preview_pcm(&p,pcm,4,64,0,0,4,48,0));assert(!fm1_mod_preview_start(&p,&doc,1,48,1));
    assert(!p.preview_pcm && p.preview_sample==1);unchanged_document();
}
static void test_nonterminal_loops_match_protracker_initial_pass(void){
    static const int8_t pcm[]={1,2,3,4,5,6,7,8};
    fm1_mod_player p;int16_t audio[2];unsigned start,i,expected,mode;
    for(start=0;start<=2;start+=2){
        fm1_mod_new(&doc);assert(!fm1_mod_replace_sample(&doc,1,"LOOP",pcm,8,64,0,start,4));
        assert(!fm1_mod_cell_set(&doc,0,0,0,(fm1_mod_cell){428,1,0,0}));clean_document();remember_document();
        for(mode=0;mode<3;mode++){
            memset(&p,0,sizeof(p));p.stereo=1;
            if(mode==0){
                fm1_mod_player_start(&p,&doc);p.stereo=1;fm1_mod_render(&p,audio,1);
                p.voices[0].phase=0;p.voices[0].step=65536;
            }else{
                if(mode==1)assert(!fm1_mod_preview_start(&p,&doc,1,48,0));
                else assert(!fm1_mod_preview_pcm(&p,pcm,8,64,0,start,4,48,0));
                p.preview_step=65536;
            }
            for(i=0;i<24;i++){
                /* PT3.15 uses loop-end on the first pass when start>0. A
                 * zero-start loop retains the complete initial sample pass. */
                expected=start?(i<6?i:2+(i-6)%4):(i<8?i:(i-8)%4);
                fm1_mod_render(&p,audio,1);
                assert(audio[0]==pcm[expected]*64 && !audio[1]);
                if(mode==0)assert(p.voices[0].active);
                else assert(p.preview_active);
            }
            unchanged_document();
        }
    }
}
static void test_editor_extensions(void){
    test_editor_fields_and_nibbles();test_editor_step_quick_and_validation();test_explicit_edit_actions_and_noops();
    test_arranger_extent_and_capacity();test_arranger_playback_and_stop_sync();
    test_block_rectangles_clipping_and_validation();test_sample_materialization_transaction_and_budget();
    test_direct_pcm_preview_is_independent_and_validated();test_nonterminal_loops_match_protracker_initial_pass();
}
