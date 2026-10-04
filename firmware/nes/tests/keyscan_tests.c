#include "fm1_wl82_keyscan.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint32_t gpio[128],mux,con,baud,ticks,accesses,writes;
static uint8_t rows[11],bytes[2];
static unsigned nbytes,words,latches,completed_bytes,pending,fail_byte;
static int selected,shifted;
static unsigned delayed_ready_reads;
static int completion_at_deadline;
#ifdef FM1_KEYSCAN_DMA2
static const uint8_t *dma_source;
static uint32_t dma_address,dma_count;
static unsigned dma_prepared,dma_addressed;
static unsigned early_completion,early_completed;
void fm1_keyscan_test_dma_source(const uint8_t *p){
    CHECK(p && !pending && !dma_prepared && !dma_addressed);
    dma_source=p;dma_prepared=1;
}
#endif
static void complete(void){
#ifdef FM1_KEYSCAN_DMA2
    CHECK(dma_source && !memcmp(dma_source,bytes,2));
    CHECK(!(gpio[0]&2)); /* latch is low before completion is consumed */
    dma_count=0;
#endif
    con|=0x8000;pending=0;++completed_bytes;
}
static uint32_t frozen_clock(void *c){CHECK(c==&ticks);return ticks;}
static uint32_t clock_us(void *c){CHECK(c==&ticks);ticks+=1000;return ticks;}
static uint32_t preempted_clock(void *c){
    CHECK(c==&ticks);
    if(completion_at_deadline && pending && !delayed_ready_reads){
        ticks+=20000;complete();
        completion_at_deadline=0;
    }
    return ticks;
}
static uint32_t *reg(uint32_t a){
    if(a>=0x50000 && a<0x50200 && !(a&3))return &gpio[(a-0x50000)/4];
    if(a==0x51020)return &mux;
    if(a==0x11e00)return &con;
    if(a==0x11e04)return &baud;
#ifdef FM1_KEYSCAN_DMA2
    if(a==0x11e10)return &dma_count;
#endif
    CHECK(0);return 0;
}
uint32_t fm1_keyscan_test_read(uint32_t a){
    uint8_t columns=selected<0?0:rows[(selected+1)%11];++accesses;
    /* WL82 BAUD and ADR are write-only. A read is a driver defect, not a
       simulated retention check. Keep baud only for checking bus writes. */
    CHECK(a!=0x11e04 && a!=0x11e0c);
    if(a==0x50004)return (columns&1u)|((columns&0x1eu)<<4);
    if(a==0x50044)return (columns&0x20u)<<2;
    if(a==0x11e00 && pending && (con&1) && completed_bytes!=fail_byte){
        if(delayed_ready_reads){--delayed_ready_reads;return con;}
        complete();
    }
    return *reg(a);
}
void fm1_keyscan_test_write(uint32_t a,uint32_t v){
    ++accesses;++writes;
    /* Regress the reset-CON/divider ordering; do not model BAUD as ordinary
       RAM that always retains writes while the peripheral is reset. */
    if(a==0x11e04){CHECK(con==0x20 && v==29);baud=v;return;}
    if(a==0x11e00){
#ifdef FM1_KEYSCAN_DMA2
        /* Inject completion during the enable read/modify/write window too.
           This tests preservation; it is not a claim about silicon timing. */
        if(early_completion==2 && pending && !(con&1) && (v&1)) {
            complete();++early_completed;
        }
#endif
        /* Pending is read-only; only the clear strobe (or reset) clears it. */
        con=(v&~0xc000u)|(con&0x8000u);if(v&0x4000)con&=~0x8000u;
        if(v==0){
            con=0;pending=0;nbytes=0;words=0;
#ifdef FM1_KEYSCAN_DMA2
            dma_count=dma_prepared=dma_addressed=0;
#endif
        } /* peripheral reset, not external latch */
        return;
    }
#ifdef FM1_KEYSCAN_DMA2
    if(a==0x11e0c){
        CHECK(dma_prepared && !dma_addressed && !pending && v==(uint32_t)(uintptr_t)dma_source);
        dma_address=v;dma_addressed=1;return;
    }
    if(a==0x11e10){
        unsigned row=words%11;uint16_t word,expected=(uint16_t)(0xffffu^(1u<<row));
        CHECK(v==2 && dma_prepared && dma_addressed && dma_address==(uint32_t)(uintptr_t)dma_source);
        CHECK(!pending && !(con&0x2000) && baud==29);
        CHECK(words ? ((con&1) && (gpio[0]&2)) : (!(con&1) && !(gpio[0]&2)));
        memcpy(bytes,dma_source,2);word=(uint16_t)((bytes[0]<<8)|bytes[1]);
        if(words && row<2)expected&=(uint16_t)~(0x800u<<row);
        CHECK(word==expected);shifted=(int)row;++words;
        dma_prepared=dma_addressed=0;dma_count=v;pending=1;
        if(early_completion==1 && words==1){complete();++early_completed;}
        return;
    }
    CHECK(a!=0x11e08); /* DMA mode must never fall back to byte BUF writes. */
#else
    if(a==0x11e08){
        unsigned row;uint16_t word,expected;
        CHECK((con&1)!=0 && (con&0x2000)==0 && baud==29 && !pending);
        if(words && nbytes==0)CHECK(gpio[0]&2); /* latch high when next transfer starts */
        if(nbytes==1)CHECK(!(gpio[0]&2));
        bytes[nbytes++]=(uint8_t)v;pending=1;
        if(nbytes<2)return;
        nbytes=0;word=(uint16_t)((bytes[0]<<8)|bytes[1]);
        row=words%11;expected=(uint16_t)(0xffffu^(1u<<row));
        if(words && row<2)expected&=(uint16_t)~(0x800u<<row);
        CHECK(word==expected);shifted=(int)row;++words;return;
    }
#endif
    if(a==0x50000 && !(gpio[0]&2) && (v&2)){
        CHECK(!pending && nbytes==0);selected=shifted;++latches;
    }
    *reg(a)=v;
}
static void reset(void){
    memset(gpio,0,sizeof(gpio));memset(rows,0x3f,sizeof(rows));
    mux=0x5a510010u;con=baud=ticks=accesses=writes=0;
    nbytes=words=latches=completed_bytes=pending=0;fail_byte=~0u;selected=shifted=-1;
    delayed_ready_reads=0;completion_at_deadline=0;
#ifdef FM1_KEYSCAN_DMA2
    dma_source=0;dma_address=dma_count=dma_prepared=dma_addressed=0;
    early_completion=early_completed=0;
#endif
}
int main(void){
    fm1_wl82_keyscan s={0};fm1_wl82_keyscan_failure saved;
    uint64_t pressed=UINT64_C(0xdeadbeef);unsigned before;
    reset();CHECK(fm1_wl82_keyscan_poll(&s,&pressed)==FM1_NES_BSP_UNVERIFIED && accesses==0);
    CHECK(fm1_wl82_keyscan_start(0,&ticks,clock_us)==FM1_NES_INVALID && accesses==0);
    CHECK(fm1_wl82_keyscan_start(&s,&ticks,0)==FM1_NES_INVALID && accesses==0);
    CHECK(fm1_wl82_keyscan_start(&s,&ticks,clock_us)==0 && s.running && s.row==0);
    CHECK((s.trace.init_con&~0x8000u)==0x21 && s.trace.init_mux==mux && s.trace.scans==1 && !s.trace.change_scan);
    CHECK(words==12 && latches==11 && (mux&0x30000)==0x20000);
    CHECK((mux&~0x30000u)==(0x5a510010u&~0x30000u));
    CHECK((gpio[2]&0x1e1)==0x1e1 && (gpio[0]&0x602)==0);
    CHECK(fm1_wl82_keyscan_poll(&s,&pressed)==0 && pressed==0);
    /* Chord: first F slot14, F# slot15, A slot18, E slot25, last G slot40. */
    rows[4]&=~0x10;rows[3]&=~0x18;rows[8]&=~0x10;rows[6]&=~4;
    CHECK(fm1_wl82_keyscan_poll(&s,&pressed)==0);
    CHECK(pressed==((UINT64_C(1)<<14)|(UINT64_C(1)<<15)|(UINT64_C(1)<<18)|
                    (UINT64_C(1)<<25)|(UINT64_C(1)<<40)));
    memset(rows,0x3f,sizeof(rows));CHECK(fm1_wl82_keyscan_poll(&s,&pressed)==0 && pressed==0);
    /* A pending-status read sees busy, then preemption advances the wall clock
       past the timeout while hardware completes. Completion must win. */
    s.now_us=preempted_clock;delayed_ready_reads=1;completion_at_deadline=1;
    CHECK(fm1_wl82_keyscan_poll(&s,&pressed)==0 && s.running && pressed==0);
    CHECK(!s.failure.valid); /* Completion won; no false timeout record. */
    s.now_us=clock_us;
    /* Ignore the completion flag and mux bits owned by other peripherals. */
    con|=0x8000;mux^=0x10;
    CHECK(fm1_wl82_keyscan_poll(&s,&pressed)==0 && !s.trace.change_scan);
    /* Capture the first readable control/routing change, not later values. */
    con^=0x10;mux^=0x10000;
    CHECK(fm1_wl82_keyscan_poll(&s,&pressed)==0 && s.trace.change_scan==s.trace.scans);
    CHECK(s.trace.change_con==0x31 && s.trace.change_mux==mux);
    before=s.trace.change_scan;con^=0x10;mux^=0x10000;
    CHECK(fm1_wl82_keyscan_poll(&s,&pressed)==0 && s.trace.change_scan==before && s.trace.change_con==0x31);
    s.trace.scans=UINT32_MAX;
    CHECK(fm1_wl82_keyscan_poll(&s,&pressed)==0 && s.trace.scans==UINT32_MAX);
    before=accesses;CHECK(fm1_wl82_keyscan_start(&s,&ticks,clock_us)==FM1_NES_BUSY && accesses==before);
    CHECK(fm1_wl82_keyscan_poll(&s,0)==FM1_NES_INVALID && accesses==before);
    /* Timeout halfway through a scan never publishes a partial key state. */
#ifdef FM1_KEYSCAN_DMA2
    fail_byte=completed_bytes+2;
#else
    fail_byte=completed_bytes+5;
#endif
    ticks=UINT32_MAX-3000;pressed=UINT64_C(0x12345678);
    CHECK(fm1_wl82_keyscan_poll(&s,&pressed)==FM1_NES_IO_ERROR);
    CHECK(pressed==UINT64_C(0x12345678) && !s.running && con==0 && !(gpio[0]&2));
    CHECK(s.failure.valid && s.failure.reason==1 && s.failure.row==3);
#ifdef FM1_KEYSCAN_DMA2
    CHECK(s.failure.phase==2 && s.failure.value==0xfff7 && s.failure.dma_count==2 && dma_count==0);
#else
    CHECK(s.failure.phase==1 && s.failure.value==0xf7);
#endif
    CHECK(s.failure.con==0x21 && s.failure.baud_written==29);
    CHECK(s.failure.pre_con==0x21 && s.failure.cleared_con==0x21 && s.failure.first_con==0x21);
    CHECK(s.failure.mux==mux && !(s.failure.pa_out&2) && s.failure.pa_dir==gpio[2]);
    CHECK((uint32_t)(s.failure.end_us-s.failure.start_us)==10000 && s.failure.polls==10);
    saved=s.failure;
    before=accesses;fm1_wl82_keyscan_stop(&s);CHECK(accesses==before);
    CHECK(fm1_wl82_keyscan_poll(&s,&pressed)==FM1_NES_BSP_UNVERIFIED);
    CHECK(!memcmp(&saved,&s.failure,sizeof(saved)) && accesses==before);
    fail_byte=~0u; /* Recover without resetting simulated external shift registers. */
    CHECK(fm1_wl82_keyscan_start(&s,&ticks,clock_us)==0 && s.running);
    CHECK(!s.failure.valid && !s.failure.con && !s.failure.polls);
    CHECK(!s.trace.change_scan && s.trace.scans==1 && (s.trace.init_con&~0x8000u)==0x21);
    CHECK(fm1_wl82_keyscan_poll(&s,&pressed)==0 && pressed==0);
    fm1_wl82_keyscan_stop(&s);
    reset();fail_byte=0;CHECK(fm1_wl82_keyscan_start(&s,&ticks,clock_us)==FM1_NES_IO_ERROR && !s.running);
    CHECK(s.failure.valid && s.failure.row==0);
#ifdef FM1_KEYSCAN_DMA2
    CHECK(s.failure.phase==2 && s.failure.value==0xfffe && s.failure.dma_count==2);
#else
    CHECK(s.failure.phase==0 && s.failure.value==0xff);
#endif
    CHECK(s.failure.reason==1 && s.failure.con==0x21);
    reset();fail_byte=0;CHECK(fm1_wl82_keyscan_start(&s,&ticks,frozen_clock)==FM1_NES_IO_ERROR && !s.running);
    CHECK(s.failure.valid && s.failure.reason==2 && s.failure.polls==1000000);
    CHECK(s.failure.start_us==s.failure.end_us && s.failure.con==0x21);
    CHECK(accesses<1000100u && con==0 && !(gpio[0]&2));
    reset();CHECK(fm1_wl82_keyscan_start(&s,&ticks,clock_us)==0);
    fm1_wl82_keyscan_stop(&s);CHECK(!s.running && con==0);
#ifdef FM1_KEYSCAN_DMA2
    /* Old4021 re-clears the already completed first word; it times out.
       Enable-only must retain both pre-existing and concurrently raised PND. */
    for(before=1;before<=2;before++) {
        reset();early_completion=before;
        CHECK(fm1_wl82_keyscan_start(&s,&ticks,clock_us)==0 && s.running);
        CHECK(early_completed==1 && words==12 && completed_bytes==12);
        CHECK(!s.failure.valid && !s.trace.change_scan && s.trace.scans==1);
        CHECK(fm1_wl82_keyscan_poll(&s,&pressed)==0 && pressed==0);
        fm1_wl82_keyscan_stop(&s);
    }
#endif
    puts("PASS: SPI2 simulated pipeline, all row words, latch ordering, priming, chords, release, wrap-safe timeout, restart; NOT a hardware test");
    return 0;
}
