#include "forge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(v) do{if(!(v)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#v);exit(1);}}while(0)
static void inputs(void){
    fm1_forge_controls s;unsigned i;int32_t enc[7]={0};uint32_t bad[7]={0};fm1_forge_controls_init(&s);
    CHECK(s.input.last_key==255 && s.input.volume_min==1023);
    for(i=0;i<41;i++){uint32_t now=i*100;uint64_t bit=UINT64_C(1)<<i;
        fm1_forge_controls_scan(&s,bit,now);fm1_forge_controls_scan(&s,bit,now+9);CHECK(!(s.input.keys&bit));
        fm1_forge_controls_scan(&s,bit,now+10);CHECK(s.input.keys&bit);CHECK(s.input.last_key==i);CHECK(s.input.seen&bit);
        fm1_forge_controls_scan(&s,0,now+11);fm1_forge_controls_scan(&s,0,now+21);CHECK(!s.input.keys);}
    CHECK(s.input.seen==((UINT64_C(1)<<41)-1));
    fm1_forge_controls_scan(&s,3,5000);fm1_forge_controls_scan(&s,3,5010);CHECK(s.input.keys==3);
    fm1_forge_controls_scan(&s,2,5011);fm1_forge_controls_scan(&s,2,5021);CHECK(s.input.keys==2);
    fm1_forge_controls_stale(&s,5120);CHECK(s.input.keys==2);fm1_forge_controls_stale(&s,5121);CHECK(!s.input.keys);
    fm1_forge_controls_init(&s);fm1_forge_controls_scan(&s,1,0xfffffffau);fm1_forge_controls_scan(&s,1,4);CHECK(s.input.keys==1);
    for(i=0;i<7;i++){enc[i]=(int)i+1;bad[i]=i;fm1_forge_controls_encoders(&s,enc,bad);}
    CHECK(s.input.encoder_seen==127 && s.input.encoder_invalid[6]==6);
    fm1_forge_controls_volume(&s,900,112,1);fm1_forge_controls_volume(&s,10,1,1);CHECK(s.input.volume_min==10 && s.input.volume_max==900);
    fm1_forge_controls_volume(&s,1024,128,1);CHECK(!s.input.volume_valid && !s.input.volume_gain);
    CHECK(fm1_forge_note_for_keys(1)==255 && fm1_forge_note_for_keys(UINT64_C(1)<<14)==14 && fm1_forge_note_for_keys((UINT64_C(1)<<14)|(UINT64_C(1)<<40))==40);
}
static void tone(void){
    fm1_forge_voice v;int32_t block[128];unsigned i,slot,n;fm1_forge_voice_init(&v);
    memset(block,1,sizeof(block));fm1_forge_voice_render(&v,block,64);for(i=0;i<128;i++)CHECK(!block[i]);
    for(slot=14;slot<41;slot++){
        int32_t peak=0;fm1_forge_voice_init(&v);fm1_forge_voice_note(&v,slot);
        for(n=0;n<1400;n++){fm1_forge_voice_render(&v,block,64);for(i=0;i<64;i++){
            CHECK(block[2*i]==block[2*i+1]);CHECK(block[2*i]>=-FM1_FORGE_TONE_PEAK && block[2*i]<=FM1_FORGE_TONE_PEAK);
            if(abs(block[2*i])>peak)peak=abs(block[2*i]);}}
        CHECK(peak>120000 && v.note==255);fm1_forge_voice_render(&v,block,64);for(i=0;i<128;i++)CHECK(!block[i]);
    }
    fm1_forge_voice_note(&v,26);for(i=0;i<10;i++)fm1_forge_voice_render(&v,block,64);fm1_forge_voice_release(&v);
    for(i=0;i<8;i++)fm1_forge_voice_render(&v,block,64);CHECK(v.note==255);
    fm1_forge_voice_init(&v);fm1_forge_voice_note(&v,14);fm1_forge_voice_render(&v,block,64);CHECK(abs(block[0])<500);
    block[0]=INT32_MAX;block[1]=INT32_MIN;block[2]=-13;fm1_forge_audio_limit(block,64);CHECK(block[0]==8388607 && block[1]==-8388608 && block[2]==-13);
}
static void render(void){
    fm1_forge_frame f={0};uint8_t rgb[480*8+16],packed[360*8+16];unsigned y,x;
    f.input.volume_raw=500;f.input.volume_valid=1;f.input.volume_gain=62;f.input.volume_min=0;f.input.volume_max=1023;f.sys_hz=480000000;f.lsb_hz=60000000;
    for(y=0;y<240;y+=8){memset(rgb,0xa5,sizeof(rgb));memset(packed,0xa5,sizeof(packed));
        CHECK(!fm1_forge_render(&f,y,8,rgb,16));CHECK(!fm1_forge_render(&f,y,8,packed,12));
        for(x=0;x<240*8;x+=2){unsigned a=(rgb[2*x]<<8)|rgb[2*x+1],b=(rgb[2*x+2]<<8)|rgb[2*x+3],p=x*3/2;
            CHECK(packed[p]==(((a>>12)<<4)|((a>>7)&15)));
            CHECK(packed[p+1]==((((a>>1)&15)<<4)|((b>>12)&15)));
            CHECK(packed[p+2]==((((b>>7)&15)<<4)|((b>>1)&15)));}
        for(x=480*8;x<sizeof(rgb);x++)CHECK(rgb[x]==0xa5);for(x=360*8;x<sizeof(packed);x++)CHECK(packed[x]==0xa5);}
    CHECK(fm1_forge_render(&f,239,8,rgb,16)==-1);CHECK(fm1_forge_render(&f,0,8,rgb,24)==-1);
    CHECK(!strcmp(fm1_forge_key_name(14),"F3") && !strcmp(fm1_forge_key_name(40),"G5"));
}
int main(void){inputs();tone();render();puts("Forge41-key debounce/seen,7-encoder,volume,tone bounds and RGB565/444 renderer passed");return 0;}
