/* SPDX-License-Identifier: Apache-2.0 */
#include "fm1_midi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned calls,command,length;static uint8_t received[520];
static void message(void *ctx,unsigned cmd,const uint8_t *p,unsigned n){CHECK(ctx==(void *)1);calls++;command=cmd;length=n;if(n)memcpy(received,p,n);}
int main(void){
    fm1_midi_parser p;uint8_t data[520],frame[FM1_MIDI_FRAME_MAX+2],events[1024],d[102];unsigned i,n,bytes,total,offset,ep_in=0,ep_out=0;
    for(i=0;i<sizeof(data);i++)data[i]=(uint8_t)(i*37u);
    for(n=0;n<=520;n++){
        memset(frame,0xa5,sizeof(frame));fm1_midi_parser_init(&p,message,(void *)1);calls=0;
        bytes=fm1_midi_frame(0x30,data,n,frame+1,FM1_MIDI_FRAME_MAX);CHECK(bytes && frame[0]==0xa5 && frame[bytes+1]==0xa5);
        CHECK(!fm1_midi_frame(0x30,data,n,frame,bytes-1));
        total=fm1_midi_event_encode(frame+1,bytes,events,sizeof(events));CHECK(total && !(total&3));
        for(offset=0;offset<total;offset+=4)fm1_midi_events(&p,events+offset,4);
        CHECK(calls==1 && command==0x30 && length==n && !memcmp(received,data,n) && !p.errors);
    }
    CHECK(!fm1_midi_frame(0x30,data,521,frame,sizeof(frame)));
    {const uint8_t trigger[]={4,0xf0,0x22,0x24,7,0x35,0x7f,0xf7};calls=0;fm1_midi_events(&p,trigger,sizeof(trigger));CHECK(calls==1 && command==0x100);}
    bytes=fm1_midi_frame(0x31,0,0,frame,sizeof(frame));total=fm1_midi_event_encode(frame,bytes,events,sizeof(events));
    calls=0;events[0]|=0x10;fm1_midi_events(&p,events,total);CHECK(!calls && p.errors);
    fm1_midi_parser_init(&p,message,(void *)1);bytes=fm1_midi_frame(0x31,0,0,frame,sizeof(frame));frame[3]^=1;total=fm1_midi_event_encode(frame,bytes,events,sizeof(events));fm1_midi_events(&p,events,total);CHECK(!calls && p.errors);
    {uint8_t malformed[]={4,0xf0,0xf7,0};fm1_midi_parser_init(&p,message,(void *)1);fm1_midi_events(&p,malformed,4);CHECK(p.errors==1 && !p.used);}
    {uint8_t overflow[]={4,0x01,0x01,0x01};const uint8_t start[]={4,0xf0,0x01,0x01};fm1_midi_parser_init(&p,message,(void *)1);fm1_midi_events(&p,start,4);for(i=0;i<300;i++)fm1_midi_events(&p,overflow,4);CHECK(p.errors && !p.used);}
    memset(d,0xa5,sizeof(d));CHECK(fm1_midi_descriptor(d+1,5)==100 && d[0]==0xa5 && d[101]==0xa5);
    CHECK(d[1+2]==5 && d[1+10]==5 && d[1+25]==6 && d[1+28]==6);
    for(i=1;i<101;i+=d[i]){CHECK(d[i]>=2 && i+d[i]<=101);if(d[i+1]==5){if(d[i+2]&128)ep_in=d[i+2];else ep_out=d[i+2];CHECK(d[i+4]==64 && d[i+5]==0);}}
    CHECK(ep_in==0x84 && ep_out==3 && i==101);
    puts("PASS all payload lengths, USB packet fragmentation, framing rejection, trigger and exact MIDI descriptors");return 0;
}
