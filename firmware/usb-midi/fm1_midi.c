/* SPDX-License-Identifier: Apache-2.0
 * Independently authored framing implementation. The wire format is an
 * interoperability fact; no M-VAVE GPL application implementation is copied. */
#include "fm1_midi.h"
#include <string.h>
static const uint8_t trigger[]={0xf0,0x22,0x24,0x35,0x7f,0xf7};
void fm1_midi_parser_init(fm1_midi_parser *p,fm1_midi_message_fn fn,void *ctx){
    memset(p,0,sizeof(*p));p->message=fn;p->context=ctx;
}
static void complete(fm1_midi_parser *p){
    uint8_t raw[FM1_MIDI_RAW_MAX];unsigned i,n=0,bits=0,value=0,bytes,sum=0;
    if(p->used==sizeof(trigger) && !memcmp(p->frame,trigger,sizeof(trigger))){
        if(p->message)p->message(p->context,0x100,0,0);return;
    }
    for(i=1;i+1<p->used;i++){
        if(p->frame[i]&0x80)goto invalid;
        value|=(unsigned)p->frame[i]<<bits;bits+=7;
        if(bits>=8){if(n>=sizeof(raw))goto invalid;raw[n++]=(uint8_t)value;value>>=8;bits-=8;}
    }
    if(value || n<7 || ((n*8u+6u)/7u)!=p->used-2u || raw[0]!=0 || raw[1]!=0x59)goto invalid;
    bytes=(unsigned)raw[3]|((unsigned)raw[4]<<8)|((unsigned)raw[5]<<16);
    if(bytes>FM1_MIDI_PAYLOAD_MAX || n!=bytes+7u)goto invalid;
    for(i=0;i<bytes;i++)sum+=raw[6+i];
    if(raw[6+bytes]!=(uint8_t)~sum)goto invalid;
    if(p->message)p->message(p->context,raw[2],raw+6,bytes);return;
invalid:++p->errors;
}
static void byte(fm1_midi_parser *p,uint8_t b){
    if(b>=0xf8)return; /* MIDI real-time may occur between SysEx bytes. */
    if(b==0xf0){p->used=0;p->discarded=0;p->frame[p->used++]=b;return;}
    if(!p->used)return;
    if(p->used>=sizeof(p->frame)){p->used=0;p->discarded=1;++p->errors;return;}
    if(b&0x80 && b!=0xf7){p->used=0;++p->errors;return;}
    p->frame[p->used++]=b;
    if(b==0xf7){complete(p);p->used=0;}
}
void fm1_midi_events(fm1_midi_parser *p,const uint8_t *events,unsigned n){
    unsigned i,j,cin,count;
    if(!events || (n&3u)){p->used=0;++p->errors;return;}
    for(i=0;i<n;i+=4){
        cin=events[i]&15u;
        if(events[i]>>4){p->used=0;++p->errors;continue;}
        if(cin==15 && events[i+1]>=0xf8)continue;
        if(cin<4 || cin>7){if(p->used){p->used=0;++p->errors;}continue;}
        count=cin==4?3u:cin-4u;
        if((cin==4 && (events[i+1]==0xf7 || events[i+2]==0xf7 || events[i+3]==0xf7)) ||
           (cin!=4 && (events[i+count]!=0xf7 || (count>1 && events[i+1]==0xf7) || (count>2 && events[i+2]==0xf7)))){
            p->used=0;++p->errors;continue;
        }
        for(j=0;j<count;j++)byte(p,events[i+1+j]);
    }
}
unsigned fm1_midi_frame(unsigned cmd,const uint8_t *data,unsigned n,uint8_t *out,unsigned cap){
    uint8_t raw[FM1_MIDI_RAW_MAX];unsigned i,k=1,bits=0,value=0,sum=0,need;
    if(cmd>255 || n>FM1_MIDI_PAYLOAD_MAX || (!data && n) || !out)return 0;
    need=2u+((n+7u)*8u+6u)/7u;if(cap<need)return 0;
    raw[0]=0;raw[1]=0x59;raw[2]=(uint8_t)cmd;raw[3]=(uint8_t)n;raw[4]=(uint8_t)(n>>8);raw[5]=0;
    for(i=0;i<n;i++){raw[6+i]=data[i];sum+=data[i];}raw[6+n]=(uint8_t)~sum;
    out[0]=0xf0;
    for(i=0;i<n+7u;i++){
        value|=(unsigned)raw[i]<<bits;bits+=8;
        while(bits>=7){out[k++]=(uint8_t)(value&127u);value>>=7;bits-=7;}
    }
    if(bits)out[k++]=(uint8_t)value;
    out[k++]=0xf7;return k;
}
unsigned fm1_midi_event_encode(const uint8_t *f,unsigned n,uint8_t *out,unsigned cap){
    unsigned i,k=0,count;
    if(!f || !out || n<2 || f[0]!=0xf0 || f[n-1]!=0xf7 || cap<((n+2u)/3u)*4u)return 0;
    for(i=0;i<n;i+=count){
        count=n-i;if(count>3)count=3;
        out[k++]=(uint8_t)(i+count==n?4u+count:4u);
        out[k++]=f[i];out[k++]=count>1?f[i+1]:0;out[k++]=count>2?f[i+2]:0;
    }return k;
}
unsigned fm1_midi_descriptor(uint8_t *out,unsigned base){
    static const uint8_t d[]={
        8,11,0,2,1,0,0,0,
        9,4,0,0,0,1,1,0,0, 9,0x24,1,0,1,9,0,1,0,
        9,4,0,0,2,1,3,0,0, 7,0x24,1,0,1,65,0,
        6,0x24,2,1,1,0, 6,0x24,2,2,2,0,
        9,0x24,3,1,3,1,2,1,0, 9,0x24,3,2,4,1,1,1,0,
        9,5,3,2,64,0,0,0,0, 5,0x25,1,1,1,
        9,5,0x84,2,64,0,0,0,0, 5,0x25,1,1,3
    };
    if(!out || base>253)return 0;
    memcpy(out,d,sizeof(d));out[2]=(uint8_t)base;out[10]=(uint8_t)base;
    out[25]=(uint8_t)(base+1);out[28]=(uint8_t)(base+1);return sizeof(d);
}
