#include "fm1_volume.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"volume line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint32_t adc,result,ana,pll,pb[9],writes,reads;
uint32_t fm1_volume_test_read(uint32_t a) {
    reads++;
    if(a==0x13100)return adc;if(a==0x13104)return result;
    if(a==0x11900)return ana;if(a==0x119a4)return pll;
    CHECK(a>=0x50040 && a<=0x50060 && !(a&3));return pb[(a-0x50040)/4];
}
void fm1_volume_test_write(uint32_t a,uint32_t v) {
    writes++;
    if(a==0x13100){CHECK(!(v&0x20));adc=v&~0xc0u;return;}
    if(a==0x11900){ana=v;return;}if(a==0x119a4){pll=v;return;}
    CHECK(a>=0x50040 && a<=0x50060 && !(a&3));
    CHECK(a!=0x50040 && a!=0x50044);pb[(a-0x50040)/4]=v;
}
static void sample(fm1_volume *s,unsigned raw) {
    unsigned n=reads;result=raw;adc|=0x80;fm1_volume_tick(s);CHECK(reads-n<=4);
}
int main(void) {
    fm1_volume s;unsigned i,n;adc=0;ana=pll=0xffffffff;
    for(i=0;i<9;i++)pb[i]=0xffffffff;
    CHECK(!fm1_volume_start(&s) && !s.target && !s.valid && s.running);
    CHECK(ana==((0xffffffffu&~0x3c0feu)|0x20005u));CHECK(pll==0xfffeffff);
    CHECK(pb[2]==0xffffffff && pb[3]==0xffffffbf && pb[4]==0xffffffbf && pb[5]==0xffffffbf && pb[8]==0xffffffbf);
    CHECK((adc&0xf00)==0x400 && !(adc&0x20) && (adc&0x10));
    for(i=0;i<9;i++)sample(&s,1023);CHECK(!s.target);
    sample(&s,1023);CHECK(s.target==127 && s.accepted==1023 && s.samples==10);
    for(i=0;i<100;i++)sample(&s,1017+i%7);CHECK(s.target==127);
    for(i=0;i<10;i++)sample(&s,512);CHECK(s.target==64);
    for(i=0;i<9;i++)sample(&s,400);sample(&s,512);sample(&s,400);CHECK(s.target==64 && s.debounce==1);
    for(i=0;i<10;i++)sample(&s,0);CHECK(!s.target && !s.accepted);
    for(i=0;i<10;i++)sample(&s,511);CHECK(s.target==63);
    for(i=0;i<9;i++)fm1_volume_tick(&s);CHECK(s.running);
    fm1_volume_tick(&s);CHECK(!s.running && !s.valid && !s.target && s.errors==1);
    n=writes;fm1_volume_tick(&s);fm1_volume_stop(&s);CHECK(writes==n);
    CHECK(!fm1_volume_start(&s));sample(&s,0xffff);CHECK(s.errors==1 && !s.running && !s.target);
    adc=0x20;n=writes;CHECK(fm1_volume_start(&s)<0 && writes==n);fm1_volume_stop(&s);CHECK(writes==n);
    adc=0x10;CHECK(fm1_volume_start(&s)<0 && writes==n);
    adc=0;CHECK(!fm1_volume_start(&s));for(i=0;i<10;i++)sample(&s,1023);
    CHECK(s.target==127);fm1_volume_stop(&s);CHECK(!adc && !s.target && !s.valid);
    puts("PASS volume ADC4 registers, preserved bits, no IRQ/waits, raw endpoints, stock filter, timeout/invalid fail-mute, restart and ownership");return 0;
}
