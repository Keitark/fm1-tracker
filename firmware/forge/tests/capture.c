#include "capture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(v) do{if(!(v)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#v);exit(1);}}while(0)
static unsigned calls,irq_disabled;
static fm1_forge_frame observed;
static void render(const fm1_forge_frame *frame,unsigned row,unsigned rows,uint8_t *out,unsigned bpp){
    unsigned y,x;CHECK(!irq_disabled && frame && out && bpp==16 && rows<=2);
    observed=*frame;calls++;
    for(y=0;y<rows;y++)for(x=0;x<240;x++){
        uint16_t pixel=(uint16_t)((frame->uptime_ms+x+(row+y)*240u)&0xffffu);
        unsigned pos=(y*240u+x)*2u;out[pos]=(uint8_t)(pixel>>8);out[pos+1]=(uint8_t)pixel;
    }
}
static void row_fragment_and_freeze(void){
    fm1_forge_capture capture;fm1_forge_frame frame={0},saved;
    fm1_uvc_stream stream;fm1_uvc_write w;fm1_uvc_read r;uint8_t expected[1024],rgb[1024];
    unsigned packet=0,y,x;frame.uptime_ms=135;frame.input.keys=UINT64_C(1)<<14;frame.note=60;
    fm1_forge_capture_init(&capture);CHECK(!capture.valid);
    fm1_forge_capture_accept(&capture,&frame);saved=frame;frame.uptime_ms=9999;frame.note=70;
    fm1_uvc_init(&stream);fm1_uvc_start(&stream,0);CHECK(fm1_uvc_begin_frame(&stream,0)==FM1_UVC_OK);
    while(fm1_uvc_reserve_bytes(&stream,0,&w)==FM1_UVC_OK){
        unsigned pos=w.first_byte%(FM1_UVC_WIDTH*2u);
        memset(expected,0xa5,sizeof(expected));
        CHECK(fm1_forge_capture_packet(&capture,&w,render)==FM1_UVC_OK);
        CHECK(!memcmp(&capture.frame,&saved,sizeof(saved)) && !memcmp(&observed,&saved,sizeof(saved)));
        for(y=0;y<w.rows;y++)for(x=0;x<FM1_UVC_WIDTH;x++){
            uint16_t pixel=0;
            if(x>=FM1_UVC_HORIZONTAL_PAD && x<FM1_UVC_HORIZONTAL_PAD+240u)
                pixel=(uint16_t)((saved.uptime_ms+x-FM1_UVC_HORIZONTAL_PAD+(w.first_row+y)*240u)&0xffffu);
            {unsigned at=(y*FM1_UVC_WIDTH+x)*2u;rgb[at]=(uint8_t)(pixel>>8);rgb[at+1]=(uint8_t)pixel;}
        }
        CHECK(fm1_uvc_rgb565be_to_yuy2(expected,rgb+pos,(w.length-2u)/2u)==FM1_UVC_OK);
        CHECK(!memcmp(expected,w.bytes+2,w.length-2u));
#if FM1_UVC_WIDTH == 256u
        if(w.first_byte==0){static const uint8_t black[4]={16,128,16,128};CHECK(!memcmp(w.bytes+2,black,4));}
#endif
        CHECK(fm1_uvc_publish_bytes(&stream,0,&w)==FM1_UVC_OK);
        CHECK(fm1_uvc_acquire(&stream,&r)==FM1_UVC_OK);CHECK(fm1_uvc_commit(&stream,&r,1)==FM1_UVC_OK);packet++;
    }
    CHECK(packet==FM1_UVC_FRAME_BYTES/640u && stream.stats.frames_sent==1);
    CHECK(calls==packet);
    fm1_forge_capture_cancel(&capture);CHECK(!capture.valid);
}
static void cancellation_and_bounds(void){
    fm1_forge_capture c;fm1_forge_frame frame={0};fm1_uvc_stream s;fm1_uvc_write w,bad;uint8_t before[642];
    fm1_forge_capture_init(&c);fm1_uvc_init(&s);fm1_uvc_start(&s,100);
    CHECK(fm1_uvc_begin_frame(&s,100)==FM1_UVC_OK && fm1_uvc_reserve_bytes(&s,100,&w)==FM1_UVC_OK);
    memcpy(before,w.bytes,642);CHECK(fm1_forge_capture_packet(&c,&w,render)==FM1_UVC_INVALID);CHECK(!memcmp(before,w.bytes,642));
    fm1_forge_capture_accept(&c,&frame);
    bad=w;bad.rows=3;CHECK(fm1_forge_capture_packet(&c,&bad,render)==FM1_UVC_INVALID);
    bad=w;bad.first_byte=2;CHECK(fm1_forge_capture_packet(&c,&bad,render)==FM1_UVC_INVALID);
    bad=w;bad.length=643;CHECK(fm1_forge_capture_packet(&c,&bad,render)==FM1_UVC_INVALID);
    CHECK(fm1_forge_capture_packet(&c,&w,0)==FM1_UVC_OK);
    /* Reset during foreground conversion invalidates the authentic token;
     * publishing cannot revive a packet from the stopped epoch. */
    fm1_uvc_reset(&s);CHECK(fm1_uvc_publish_bytes(&s,100,&w)==FM1_UVC_STALE);
    fm1_forge_capture_cancel(&c);CHECK(fm1_forge_capture_packet(&c,&w,render)==FM1_UVC_INVALID);
}
int main(void){row_fragment_and_freeze();cancellation_and_bounds();puts("Forge capture: frozen frame, bounded rows, padded YUY2 and cancellation passed");return 0;}
