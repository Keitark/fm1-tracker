/* SPDX-License-Identifier: GPL-3.0-only
 * Reused from Keitark/fm1-mdx at 4141057; see licenses/fm1-mdx-GPL-3.0.txt. */
#include "bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static fm1_uac_bridge b;
static void packet(uint8_t *p,int16_t l,int16_t r) {
    unsigned i;for(i=0;i<48;i++){p[4*i]=(uint8_t)l;p[4*i+1]=(uint16_t)l>>8;p[4*i+2]=(uint8_t)r;p[4*i+3]=(uint16_t)r>>8;}
}
static int16_t word(const uint8_t *p){return (int16_t)(p[0]|(p[1]<<8));}
static void mic_run(int16_t l,int16_t r,unsigned frames) {
    uint8_t p[192];int32_t pcm[2];unsigned i;
    packet(p,l,r);
    for(i=0;i<frames;i++) {
        if(i%40==0)CHECK(!fm1_uac_receive(&b,p,192));
        pcm[0]=pcm[1]=20000*256; /* NES audio must never activate mic. */
        fm1_uac_dac(&b,pcm,1);
    }
}
static void mic_test(void) {
    unsigned i;int32_t pcm[2];uint8_t p[192];
    memset(&b,0,sizeof(b));fm1_uac_stream(&b,0,1);
    mic_run(8000,-8000,400);CHECK(!b.mic_enabled && !b.mic_active);
    fm1_uac_microphone(&b,1);mic_run(8000,-8000,100);CHECK(b.mic_active);
    /* Drain old sound then silence longer than the hold. */
    mic_run(0,0,1600);CHECK(!b.mic_active);
    mic_run(1023,-1023,1600);CHECK(!b.mic_active);
    mic_run(1024,-1024,1600);CHECK(b.mic_active);
    mic_run(512,-512,1600);CHECK(b.mic_active); /* release hysteresis */
    mic_run(511,-511,1600);CHECK(!b.mic_active);
    mic_run(0,-32768,1600);CHECK(b.mic_active); /* right-only, full negative */
    fm1_uac_microphone(&b,0);CHECK(!b.mic_active && !b.mic_hold);
    mic_run(32767,0,400);CHECK(!b.mic_active);
    fm1_uac_microphone(&b,1);mic_run(32767,0,400);CHECK(b.mic_active);
    for(i=0;i<1800;i++){pcm[0]=pcm[1]=0;fm1_uac_dac(&b,pcm,1);}
    CHECK(!b.mic_active && !b.mic_hold && !b.playback.primed);
    packet(p,2048,-2048);for(i=0;i<8;i++)CHECK(!fm1_uac_receive(&b,p,192));
    pcm[0]=pcm[1]=0;fm1_uac_dac(&b,pcm,1);CHECK(b.mic_active);
    fm1_uac_stream(&b,0,0);CHECK(b.mic_enabled && !b.mic_active);
    fm1_uac_stream(&b,0,1);CHECK(!b.mic_active);
    mic_run(0,0,1200);CHECK(!b.mic_active); /* no stale audio replay */
    /* Exact hold duration with valid silent samples, no underrun shortcut. */
    memset(&b,0,sizeof(b));fm1_uac_stream(&b,0,1);fm1_uac_microphone(&b,1);
    packet(p,0,0);for(i=0;i<16;i++)CHECK(!fm1_uac_receive(&b,p,192));
    b.mic_active=1;b.mic_hold=FM1_MIC_HOLD;
    for(i=0;i<FM1_MIC_HOLD-1;i++){pcm[0]=pcm[1]=0;fm1_uac_dac(&b,pcm,1);CHECK(b.mic_active);}
    pcm[0]=pcm[1]=0;fm1_uac_dac(&b,pcm,1);CHECK(!b.mic_active && !b.playback.underruns);
}
static void run(int ppm) {
    uint8_t in[192],out[192];int32_t pcm[2*46];unsigned ms,i,n;uint64_t clock=0;
    memset(&b,0,sizeof(b));fm1_uac_stream(&b,0,1);fm1_uac_stream(&b,1,1);packet(in,6000,-6000);
    fm1_uac_microphone(&b,1); /* Mic must not change playback or isolated capture. */
    for(ms=0;ms<60000;ms++) {
        CHECK(!fm1_uac_receive(&b,in,sizeof(in)));
        clock+=(uint64_t)44100*(1000000+ppm);n=(unsigned)(clock/1000000000);clock%=1000000000;
        CHECK(n<=46);
        for(i=0;i<n;i++){pcm[2*i]=10000*256;pcm[2*i+1]=-10000*256;}
        fm1_uac_dac(&b,pcm,n);CHECK(fm1_uac_transmit(&b,out,sizeof(out))==192);
        if(ms>1000) {
            CHECK(pcm[0]==16000*256 && pcm[1]==-16000*256);
            CHECK(word(out)==10000 && word(out+2)==-10000); /* no PC loopback */
        }
    }
    CHECK(!b.capture.underruns && !b.playback.underruns && !b.capture.overruns && !b.playback.overruns);
    CHECK(b.capture.wr-b.capture.rd<1024 && b.playback.wr-b.playback.rd<1024);
}
static void recovery(void) {
    uint8_t p[192];int32_t pcm[2];int previous=0;unsigned i;
    memset(&b,0,sizeof(b));fm1_uac_stream(&b,0,1);packet(p,6000,-6000);
    for(i=0;i<8;i++)CHECK(!fm1_uac_receive(&b,p,192));
    /* Starve nonzero PC audio. FM keeps playing, and PC fades to silence
       in bounded steps rather than an instantaneous 6000-count jump. */
    for(i=0;i<500;i++) {
        pcm[0]=10000*256;pcm[1]=-10000*256;fm1_uac_dac(&b,pcm,1);
        CHECK(abs(pcm[0]/256-10000-previous)<=188);
        CHECK(pcm[1]==-pcm[0]);previous=pcm[0]/256-10000;
    }
    CHECK(b.playback.underruns==1 && !previous && !b.playback.primed);
    for(i=0;i<8;i++)CHECK(!fm1_uac_receive(&b,p,192));
    for(i=0;i<64;i++) {
        pcm[0]=10000*256;pcm[1]=-10000*256;fm1_uac_dac(&b,pcm,1);
        CHECK(abs(pcm[0]/256-10000-previous)<=188);previous=pcm[0]/256-10000;
    }
    CHECK(previous==6000);
    fm1_uac_stream(&b,0,0);
    for(i=0;i<40;i++) {
        pcm[0]=10000*256;pcm[1]=-10000*256;fm1_uac_dac(&b,pcm,1);
        CHECK(abs(pcm[0]/256-10000-previous)<=188);previous=pcm[0]/256-10000;
    }
    CHECK(!previous && b.playback.underruns==1);
}
int main(int argc,char **argv) {
    unsigned i,offset=0;uint8_t p[192];int32_t pcm[128];
    if(argc>1 && !strcmp(argv[1],"--descriptor")){for(i=0;i<173;i++)printf("%02x",fm1_uac_descriptor[i]);puts("");return 0;}
    while(offset<173){unsigned n=fm1_uac_descriptor[offset];CHECK(n>=2 && n<=173-offset);offset+=n;}CHECK(offset==173);
    CHECK(fm1_uac_interface_request(1,11,1,3,0));CHECK(fm1_uac_interface_request(0x81,10,0,4,1));
    CHECK(!fm1_uac_interface_request(1,11,1,2,0));CHECK(!fm1_uac_interface_request(1,11,2,4,0));
    CHECK(!fm1_uac_interface_request(0x21,11,1,3,0));CHECK(!fm1_uac_interface_request(1,11,1,0x103,0));
    CHECK(!fm1_uac_interface_request(1,11,1,3,65535));CHECK(!fm1_uac_interface_request(0x21,1,0,3,3));
    mic_test();recovery();run(0);run(500);run(-500);
    CHECK(fm1_uac_receive(&b,p,193)==-1 && fm1_uac_receive(&b,p,3)==-1);
    CHECK(fm1_uac_receive(&b,NULL,4)==-1 && b.bad_packets==3);
    packet(p,32767,-32768);fm1_uac_stream(&b,0,1);
    for(i=0;i<20;i++)fm1_uac_receive(&b,p,192);
    for(i=0;i<64;i++){pcm[2*i]=32767*256;pcm[2*i+1]=-32768*256;}
    fm1_uac_dac(&b,pcm,64);CHECK(pcm[0]==32767*256 && pcm[1]==-32768*256);
    fm1_uac_stream(&b,0,0);fm1_uac_stream(&b,1,0);
    CHECK(fm1_uac_transmit(&b,p,192)==0 && !b.playback.primed && b.playback.wr==b.playback.rd);
    fm1_uac_stream(&b,0,1);memset(pcm,0,sizeof(pcm));fm1_uac_dac(&b,pcm,64);
    CHECK(pcm[126]==0 && pcm[127]==0 && !b.playback.primed); /* bounded old-sample fade, no FIFO replay */
    puts("PASS duplex48k bridge, stereo/loopback isolation, +/-500ppm clock drift, malformed packets, clipping and reset");return 0;
}
