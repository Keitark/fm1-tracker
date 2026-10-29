#include "capture.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

static fm1_mod_document song;
static fm1_mod_editor editor;
static fm1_mod_player player;
static fm1_mod_library library;
typedef struct {
    uint32_t before;
    fm1_tracker_capture capture;
    uint32_t after;
} guarded_capture;
static void fixture(fm1_tracker_view *view,unsigned mode){
    static const int8_t pcm[]={64,32,0,-32,-64,-32,0,32};
    fm1_tracker_view_context context={0};unsigned channel,x;
    fm1_mod_new(&song);assert(!fm1_mod_replace_sample(&song,1,"CAPTURE WAVE",pcm,sizeof(pcm),64,0,0,sizeof(pcm)));
    fm1_mod_editor_init(&editor,&song);editor.row=mode?63:0;editor.channel=2;editor.edit=1;
    assert(!fm1_mod_cell_set(&song,0,editor.row,2,(fm1_mod_cell){428,1,0xc,32}));
    fm1_mod_player_start(&player,&song);player.current_row=editor.row;
    for(channel=0;channel<4;channel++)for(x=0;x<48;x++)player.scope[channel][x]=(int8_t)((int)x-24+(int)channel*5);
    memset(&library,0,sizeof(library));
    fm1_tracker_view_update(view,&song,&player,&editor,&library,mode==1?2:0,1,1,0);
    if(mode==1){context.sample_open=1;context.sample_field=FM1_SAMPLE_FIELD_LOOP_START;}
    if(mode==2){context.help_open=1;context.help_topic=4;}
    fm1_tracker_view_context_apply(view,&song,&editor,&context);
}
static void guard_init(guarded_capture *g,const fm1_tracker_view *view){
    memset(g,0xa5,sizeof(*g));g->before=0x12345678u;g->after=0x87654321u;
    fm1_tracker_capture_init(&g->capture);assert(!g->capture.valid);
    fm1_tracker_capture_accept(&g->capture,view);assert(g->capture.valid);
    assert(sizeof(g->capture.rgb)==1024);
    assert(sizeof(g->capture)<=sizeof(fm1_tracker_view)+1026u);
}
static void guarded(const guarded_capture *g){assert(g->before==0x12345678u && g->after==0x87654321u);}
static void expected(const fm1_tracker_view *view,const fm1_uvc_write *write,uint8_t *output){
    uint8_t native[960],raster[1024];unsigned row,x,offset=write->first_byte%(FM1_UVC_WIDTH*2u);
    fm1_tracker_view_render(view,write->first_row,write->rows,native);
    /* Independent scatter reference avoids duplicating the production reverse
     * in-place expansion and catches any row-overwrite or padding mistakes. */
    for(row=0;row<write->rows;row++)for(x=0;x<FM1_UVC_WIDTH;x++){
        unsigned dest=(row*FM1_UVC_WIDTH+x)*2u;
        if(x>=FM1_UVC_HORIZONTAL_PAD && x<FM1_UVC_HORIZONTAL_PAD+240u){
            unsigned source=(row*240u+x-FM1_UVC_HORIZONTAL_PAD)*2u;
            raster[dest]=native[source];raster[dest+1]=native[source+1];
        }else raster[dest]=raster[dest+1]=0;
    }
    assert(fm1_uvc_rgb565be_to_yuy2(output,raster+offset,(write->length-FM1_UVC_HEADER_BYTES)/2u)==FM1_UVC_OK);
}
static void frozen_full_frame(unsigned mode){
    guarded_capture g;fm1_tracker_view current,frozen;
    fm1_uvc_stream stream;fm1_uvc_write write;fm1_uvc_read read;
    uint8_t reference[FM1_UVC_PAYLOAD_BYTES],header[2];unsigned packets=0,final=0;
    fixture(&current,mode);frozen=current;guard_init(&g,&current);
    memset(&current,0,sizeof(current)); /* Later UI changes must not leak into a frame. */
    fm1_uvc_init(&stream);fm1_uvc_start(&stream,100);
    assert(fm1_uvc_begin_frame(&stream,100)==FM1_UVC_OK);
    while(fm1_uvc_reserve_bytes(&stream,100,&write)==FM1_UVC_OK){
        unsigned payload=write.length-FM1_UVC_HEADER_BYTES;
        memcpy(header,write.bytes,2);memset(reference,0xa5,sizeof(reference));
        expected(&frozen,&write,reference);
        assert(fm1_tracker_capture_packet(&g.capture,&write)==FM1_UVC_OK);
        assert(!memcmp(header,write.bytes,2) && !memcmp(reference,write.bytes+2,payload));
        assert(!memcmp(&frozen,&g.capture.view,sizeof(frozen)));guarded(&g);
        if(write.bytes[1]&FM1_UVC_HEADER_EOF){assert(write.first_byte+payload==FM1_UVC_FRAME_BYTES);final++;}
#if FM1_UVC_WIDTH == 256u
        if(!write.first_byte){
            static const uint8_t black[4]={16,128,16,128};unsigned i;
            for(i=0;i<FM1_UVC_HORIZONTAL_PAD*2u;i+=4)assert(!memcmp(write.bytes+2+i,black,4));
        }
#endif
        assert(fm1_uvc_publish_bytes(&stream,100,&write)==FM1_UVC_OK);
        assert(fm1_uvc_acquire(&stream,&read)==FM1_UVC_OK);
        assert(fm1_uvc_commit(&stream,&read,1)==FM1_UVC_OK);packets++;
    }
    assert(packets==FM1_UVC_FRAME_BYTES/FM1_UVC_PAYLOAD_BYTES && final==1 && stream.stats.frames_sent==1);
    fm1_tracker_capture_cancel(&g.capture);assert(!g.capture.valid);guarded(&g);
}
static void rejected(guarded_capture *g,const fm1_uvc_write *write){
    uint8_t scratch[1024],bytes[FM1_UVC_PACKET_BYTES];
    memcpy(scratch,g->capture.rgb,sizeof(scratch));
    if(write && write->bytes)memcpy(bytes,write->bytes,sizeof(bytes));
    assert(fm1_tracker_capture_packet(&g->capture,write)==FM1_UVC_INVALID);
    assert(!memcmp(scratch,g->capture.rgb,sizeof(scratch)));
    if(write && write->bytes)assert(!memcmp(bytes,write->bytes,sizeof(bytes)));
    guarded(g);
}
static void malformed_and_final_fragments(void){
    guarded_capture g;fm1_tracker_view view;
    fm1_uvc_write write={0},bad;uint8_t storage[FM1_UVC_PACKET_BYTES+16],reference[FM1_UVC_PAYLOAD_BYTES];
    unsigned stride=FM1_UVC_WIDTH*2u;
    fixture(&view,0);guard_init(&g,&view);memset(storage,0x5a,sizeof(storage));
    storage[0]=2;storage[1]=FM1_UVC_HEADER_EOH;
    write.bytes=storage;write.length=FM1_UVC_PACKET_BYTES;write.rows=2;
    assert(fm1_tracker_capture_packet(0,&write)==FM1_UVC_INVALID);
    rejected(&g,0);
    bad=write;bad.bytes=0;rejected(&g,&bad);
    bad=write;bad.rows=0;rejected(&g,&bad);
    bad=write;bad.rows=3;rejected(&g,&bad);
    bad=write;bad.rows=1;rejected(&g,&bad); /* Exact crossed-row count is required. */
    bad=write;bad.first_row=240;rejected(&g,&bad);
    bad=write;bad.first_row=1;rejected(&g,&bad);
    bad=write;bad.length=0;rejected(&g,&bad);
    bad=write;bad.length=2;rejected(&g,&bad);
    bad=write;bad.length=8;rejected(&g,&bad); /* Six payload bytes split a chroma pair. */
    bad=write;bad.length=FM1_UVC_PACKET_BYTES+1;rejected(&g,&bad);
    bad=write;bad.first_byte=2;rejected(&g,&bad);
    bad=write;bad.first_byte=FM1_UVC_FRAME_BYTES;rejected(&g,&bad);
    bad=write;bad.first_byte=FM1_UVC_FRAME_BYTES-4;bad.first_row=239;rejected(&g,&bad);
    storage[0]=3;rejected(&g,&write);storage[0]=2;
    storage[1]=0;rejected(&g,&write);
    storage[1]=FM1_UVC_HEADER_EOH|FM1_UVC_HEADER_ERR;rejected(&g,&write);
    storage[1]=FM1_UVC_HEADER_EOH|FM1_UVC_HEADER_EOF;rejected(&g,&write);
    storage[1]=FM1_UVC_HEADER_EOH;
    /* A valid small fragment crossing a raster boundary must convert exactly
     * the two requested rows, preserving both header and unused packet tail. */
    write.first_byte=2u*stride-4u;write.first_row=1;write.rows=2;write.length=10;
    expected(&view,&write,reference);assert(fm1_tracker_capture_packet(&g.capture,&write)==FM1_UVC_OK);
    assert(!memcmp(storage+2,reference,8) && storage[0]==2 && storage[1]==FM1_UVC_HEADER_EOH);
    for(unsigned i=write.length;i<sizeof(storage);i++)assert(storage[i]==0x5a);
    /* The final chroma pair fits one final row and must carry EOF. */
    memset(storage+2,0x5a,sizeof(storage)-2);write.first_byte=FM1_UVC_FRAME_BYTES-4;
    write.first_row=239;write.rows=1;write.length=6;
    rejected(&g,&write);storage[1]|=FM1_UVC_HEADER_EOF;
    expected(&view,&write,reference);assert(fm1_tracker_capture_packet(&g.capture,&write)==FM1_UVC_OK);
    assert(!memcmp(storage+2,reference,4));
    for(unsigned i=write.length;i<sizeof(storage);i++)assert(storage[i]==0x5a);
    fm1_tracker_capture_cancel(&g.capture);rejected(&g,&write);
}
static void reset_during_foreground_work(void){
    guarded_capture g;fm1_tracker_view view;fm1_uvc_stream stream;fm1_uvc_write write;
    fixture(&view,1);guard_init(&g,&view);fm1_uvc_init(&stream);fm1_uvc_start(&stream,0);
    assert(fm1_uvc_begin_frame(&stream,0)==FM1_UVC_OK && fm1_uvc_reserve_bytes(&stream,0,&write)==FM1_UVC_OK);
    assert(fm1_tracker_capture_packet(&g.capture,&write)==FM1_UVC_OK);
    fm1_uvc_reset(&stream);
    assert(fm1_uvc_publish_bytes(&stream,0,&write)==FM1_UVC_STALE);
    fm1_tracker_capture_cancel(&g.capture);rejected(&g,&write);
    fm1_uvc_start(&stream,1);assert(fm1_uvc_begin_frame(&stream,1)==FM1_UVC_OK);
    assert(fm1_uvc_reserve_bytes(&stream,1,&write)==FM1_UVC_OK);
    fm1_tracker_capture_accept(&g.capture,&view);
    assert(fm1_tracker_capture_packet(&g.capture,&write)==FM1_UVC_OK);
    assert(fm1_uvc_publish_bytes(&stream,1,&write)==FM1_UVC_OK);guarded(&g);
}
int main(void){
    frozen_full_frame(0);frozen_full_frame(1);frozen_full_frame(2);
    malformed_and_final_fragments();reset_during_foreground_work();
    printf("Tracker capture %ux240: frozen view, bounded fragments, padding, EOF and reset passed\n",FM1_UVC_WIDTH);
    return 0;
}
