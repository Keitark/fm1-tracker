#include "fm1_wl82_keyscan.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#if defined(FM1_MOD_PLAYER) && defined(FM1_LCD_ASYNC)
#define NO_PROGRESS_LIMIT_US 30000u
#else
#define NO_PROGRESS_LIMIT_US 10000u
#endif
static uint32_t gpio[128],mux,con,baud,cnt,ticks,reads,writes,clocks;
static const uint8_t *source;
static uint8_t matrix[11],armed[2];
static unsigned words,pending,prepared,addressed,early;
static int selected=-1,shifted=-1;
static uint32_t clock_us(void *ctx){CHECK(ctx==&ticks);clocks++;return ticks;}
static uint32_t *reg(uint32_t a){
    if(a>=0x50000 && a<0x50200 && !(a&3))return &gpio[(a-0x50000)/4];
    if(a==0x51020)return &mux;
    if(a==0x11e00)return &con;
    if(a==0x11e10)return &cnt;
    CHECK(0);return 0;
}
void fm1_keyscan_test_dma_source(const uint8_t *p){CHECK(p && !pending && !prepared && !addressed);source=p;prepared=1;}
uint32_t fm1_keyscan_test_read(uint32_t a){
    uint8_t c=selected<0?0:matrix[(selected+1)%11];reads++;
    CHECK(a!=0x11e04 && a!=0x11e0c && a!=0x11e08);
    if(a==0x50004)return (c&1u)|((c&0x1eu)<<4);
    if(a==0x50044)return (c&0x20u)<<2;
    return *reg(a);
}
static void complete(void){
    CHECK(pending && source && !memcmp(source,armed,2));
    CHECK(!(gpio[0]&2));pending=0;cnt=0;con|=0x8000;
}
void fm1_keyscan_test_write(uint32_t a,uint32_t v){
    writes++;
    if(a==0x11e00){
        con=(v&~0xc000u)|(con&0x8000u);if(v&0x4000u)con&=~0x8000u;
        if(!v){con=cnt=pending=prepared=addressed=words=0;}
        return;
    }
    if(a==0x11e04){CHECK(con==0x2020 && v==29);baud=v;return;}
    if(a==0x11e0c){CHECK(prepared && !addressed && !pending && v==(uint32_t)(uintptr_t)source);addressed=1;return;}
    if(a==0x11e10){
        unsigned row=words%11;uint16_t expected=(uint16_t)(0xffffu^(1u<<row));
        CHECK(v==2 && prepared && addressed && !pending && baud==29 && (con&0x2000));
        CHECK(words?((con&1) && (gpio[0]&2)):(!(con&1) && !(gpio[0]&2)));
        if(words && row<2)expected&=(uint16_t)~(0x800u<<row);
        CHECK(((source[0]<<8)|source[1])==expected);
        memcpy(armed,source,2);prepared=addressed=0;cnt=2;pending=1;shifted=(int)row;words++;
        if(early && words==1)complete();return;
    }
    CHECK(a!=0x11e08);
    if(a==0x50000 && !(gpio[0]&2) && (v&2)){
        CHECK(!pending);selected=shifted;
    }
    *reg(a)=v;
}
static void pump(fm1_wl82_keyscan *s,unsigned n){
    unsigned i,c=clocks;
    for(i=0;i<n;i++){
#ifdef FM1_KEYSCAN_PACED
        fm1_wl82_keyscan_async_kick(s); /* Simulated periodic tick at boundaries. */
#endif
        complete();fm1_wl82_keyscan_async_step(s);
    }
    CHECK(clocks==c); /* No timer or waits in interrupt step. */
}
int main(void){
    fm1_wl82_keyscan s={0};uint8_t out[11],save[11];unsigned i,w,r,c;
    memset(matrix,0x3f,11);memset(out,0xa5,11);memcpy(save,out,11);
    CHECK(fm1_wl82_keyscan_async_start(0,&ticks,clock_us)==FM1_NES_INVALID);
    CHECK(fm1_wl82_keyscan_async_start(&s,&ticks,0)==FM1_NES_INVALID);
    CHECK(!reads && !writes);
    CHECK(!fm1_wl82_keyscan_async_start(&s,&ticks,clock_us));
    CHECK(s.running && pending && words==1 && s.completions==0 && clocks==1);
    w=writes;r=reads;c=clocks;
    CHECK(fm1_wl82_keyscan_async_start(&s,&ticks,clock_us)==FM1_NES_BUSY);
    fm1_wl82_keyscan_async_step(&s); /* Spurious IRQ: no completion, no rearm. */
    CHECK(writes==w && reads==r+1 && clocks==c);
    CHECK(fm1_wl82_keyscan_async_raw(&s,out)==FM1_NES_BUSY && !memcmp(out,save,11));
    pump(&s,11);CHECK(s.primed && !s.sequence);
#ifdef FM1_KEYSCAN_PACED
    CHECK(s.paused && !pending && !(con&0x8000) && (gpio[0]&2) && words==11);
    w=writes;
    for(i=0;i<100;i++)fm1_wl82_keyscan_async_step(&s);
    CHECK(writes==w && s.completions==11 && !pending); /* No free-running IRQ. */
    fm1_wl82_keyscan_async_kick(&s);CHECK(!s.paused && pending && words==12);
    w=writes;fm1_wl82_keyscan_async_kick(&s);CHECK(writes==w); /* Never overlap DMA. */
#endif
    CHECK(fm1_wl82_keyscan_async_raw(&s,out)==FM1_NES_BUSY);
    matrix[4]&=~0x10;matrix[3]&=~0x10;matrix[8]&=~0x10; /* F/F#/A held chord */
    pump(&s,10);CHECK(fm1_wl82_keyscan_async_raw(&s,out)==FM1_NES_BUSY && !memcmp(out,save,11));
    pump(&s,1);w=writes;
    CHECK(!fm1_wl82_keyscan_async_raw(&s,out) && !memcmp(out,matrix,11));
    CHECK(writes==w && fm1_stock_decode_keys(out)==((UINT64_C(1)<<14)|(UINT64_C(1)<<15)|(UINT64_C(1)<<18)));
    memcpy(save,out,11);
    CHECK(fm1_wl82_keyscan_async_raw(&s,out)==FM1_NES_BUSY && !memcmp(out,save,11));
    /* Arbitrarily many autonomous IRQ sweeps, with no task call required. */
    pump(&s,1100);CHECK(!fm1_wl82_keyscan_async_raw(&s,out) && !memcmp(out,save,11));
    memset(matrix,0x3f,11);pump(&s,11);
    CHECK(!fm1_wl82_keyscan_async_raw(&s,out) && !fm1_stock_decode_keys(out));
    /* A partial sweep cannot replace the last complete snapshot. */
    matrix[4]=0;pump(&s,5);memcpy(save,out,11);
    CHECK(fm1_wl82_keyscan_async_raw(&s,out)==FM1_NES_BUSY && !memcmp(out,save,11));
    ticks=NO_PROGRESS_LIMIT_US-1;CHECK(fm1_wl82_keyscan_async_raw(&s,out)==FM1_NES_BUSY);
    ticks=NO_PROGRESS_LIMIT_US;CHECK(fm1_wl82_keyscan_async_raw(&s,out)==FM1_NES_IO_ERROR);
    CHECK(!s.running && !con && !pending && !memcmp(out,save,11));
    CHECK(s.failure.valid && s.failure.reason==4 && s.failure.phase==2 && s.failure.dma_count==2);
    w=writes;r=reads;fm1_wl82_keyscan_async_step(&s);fm1_wl82_keyscan_stop(&s);
    CHECK(writes==w && reads==r);
    /* Restart discards a fresh priming sweep and does not retain stale rows. */
    memset(matrix,0x3f,11);ticks=UINT32_MAX-5000;
    CHECK(!fm1_wl82_keyscan_async_start(&s,&ticks,clock_us) && !s.failure.valid);
    pump(&s,11);CHECK(fm1_wl82_keyscan_async_raw(&s,out)==FM1_NES_BUSY);
    pump(&s,11);CHECK(!fm1_wl82_keyscan_async_raw(&s,out) && !fm1_stock_decode_keys(out));
    /* Completed DMA but no ISR delivery is also a task-detected stall. */
    fm1_wl82_keyscan_async_kick(&s);
    complete();ticks+=NO_PROGRESS_LIMIT_US;
    CHECK(fm1_wl82_keyscan_async_raw(&s,out)==FM1_NES_IO_ERROR);
    CHECK(s.failure.dma_count==0 && (s.failure.con&0x8000) && s.failure.reason==4);
    /* Completion before enable survives startup (no second PND clear). */
    early=1;CHECK(!fm1_wl82_keyscan_async_start(&s,&ticks,clock_us));
    CHECK(con&0x8000);fm1_wl82_keyscan_async_step(&s);CHECK(s.completions==1 && pending);
    fm1_wl82_keyscan_stop(&s);
    w=writes;fm1_wl82_keyscan_async_kick(&s);CHECK(writes==w);
    for(i=0;i<10;i++)fm1_wl82_keyscan_async_step(&s);
#ifdef FM1_KEYSCAN_PACED
    /* A lost timer is a real failure; normal short inter-sweep idle is not. */
    early=0;CHECK(!fm1_wl82_keyscan_async_start(&s,&ticks,clock_us));
    pump(&s,22);CHECK(!fm1_wl82_keyscan_async_raw(&s,out));
    ticks+=1000;CHECK(fm1_wl82_keyscan_async_raw(&s,out)==FM1_NES_BUSY);
    CHECK(s.paused && !pending && !s.failure.valid);
    ticks+=NO_PROGRESS_LIMIT_US-1000u;CHECK(fm1_wl82_keyscan_async_raw(&s,out)==FM1_NES_IO_ERROR);
    CHECK(s.failure.reason==4 && !s.running && !con);
#endif
    puts("PASS IRQ DMA model: bounded step, priming, coherent snapshots, held chords, release, watchdog, restart; NOT hardware qualification");
    return 0;
}
