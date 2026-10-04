#include "fm1_wl82_keyscan.h"
#if defined(FM1_KEYSCAN_IRQ) && !defined(FM1_KEYSCAN_DMA2)
#error IRQ scanner requires DMA2
#endif
#if defined(FM1_KEYSCAN_PACED) && !defined(FM1_KEYSCAN_IRQ)
#error Paced scanner requires IRQ scanner
#endif
#ifndef FM1_WL82_KEYSCAN_TEST
#include "asm/WL82.h"
#include <stddef.h>
typedef char keyscan_spi_address_check[JL_SPI2_BASE==0x11e00?1:-1];
typedef char keyscan_spi_layout_check[
    offsetof(JL_SPI_TypeDef,BAUD)==4 && offsetof(JL_SPI_TypeDef,BUF)==8 &&
    offsetof(JL_SPI_TypeDef,ADR)==12 && offsetof(JL_SPI_TypeDef,CNT)==16?1:-1];
typedef char keyscan_mux_address_check[
    JL_IOMAP_BASE+offsetof(JL_IOMAP_TypeDef,CON1)==0x51020?1:-1];
static uint32_t rd(uint32_t a){return *(volatile uint32_t *)(uintptr_t)a;}
static void wr(uint32_t a,uint32_t v){*(volatile uint32_t *)(uintptr_t)a=v;}
#else
extern uint32_t fm1_keyscan_test_read(uint32_t);
extern void fm1_keyscan_test_write(uint32_t,uint32_t);
#define rd fm1_keyscan_test_read
#define wr fm1_keyscan_test_write
#endif
#define PA 0x50000u
#define PB 0x50040u
#define PH 0x501c0u
#define MUX 0x51020u
#define SPI 0x11e00u
#define BAUD_WRITE_VALUE 29u
#define LATCH 2u
#define A_COLS 0x1e1u
#define B_COLS 0x80u
#define A_LEDS 0x600u
#define H_LEDS 0x240u

#ifdef FM1_KEYSCAN_DMA2
/* One exclusively owned, persistent internal-RAM DMA source. ELF placement
   is checked by audit_boot.py. Never reuse before completion or SPI shutdown. */
#ifdef FM1_WL82_KEYSCAN_TEST
static uint8_t fm1_key_dma[64];
extern void fm1_keyscan_test_dma_source(const uint8_t *);
#else
__attribute__((aligned(64),used)) static uint8_t fm1_key_dma[64];
#endif
#endif
static void change(uint32_t a,uint32_t clear,uint32_t set){wr(a,(rd(a)&~clear)|set);}
static void outputs(uint32_t port,uint32_t mask){
    change(port,mask,0);change(port+0x10,mask,0);change(port+0x14,mask,0);
    change(port+0x0c,0,mask);change(port+8,mask,0);
}
static void inputs(uint32_t port,uint32_t mask){
    change(port+0x14,mask,0);change(port+0x10,0,mask);
    change(port+0x0c,0,mask);change(port+8,0,mask);
}
void fm1_wl82_keyscan_stop(fm1_wl82_keyscan *s){
    if(!s || !s->running)return;
    wr(SPI,0);change(PA,LATCH,0);s->running=0;s->row=0;
#if defined(FM1_KEYSCAN_DMA2) && !defined(FM1_WL82_KEYSCAN_TEST)
    __asm__ volatile("csync" ::: "memory");
#endif
    /* Shift registers retain their last value; no invented power-off command. */
}
static int transfer(fm1_wl82_keyscan *s,uint16_t value,int lower_latch,unsigned phase){
    uint32_t start,polls=0,now,status,pre,cleared,first;
    pre=rd(SPI);wr(SPI,pre|0x4000u);
    cleared=rd(SPI);
#ifdef FM1_KEYSCAN_DMA2
    fm1_key_dma[0]=(uint8_t)(value>>8);fm1_key_dma[1]=(uint8_t)value;
#ifdef FM1_WL82_KEYSCAN_TEST
    fm1_keyscan_test_dma_source(fm1_key_dma);
#else
    __asm__ volatile("csync" ::: "memory");
#endif
    wr(SPI+12,(uint32_t)(uintptr_t)fm1_key_dma);wr(SPI+16,2);
    if(!lower_latch) {
        /* Stock020044fe sets ONLY enable after ADR/CNT. Writing4021 here
           would strobe pending-clear again and can discard completion. */
        change(SPI,0,1);
        s->trace.init_con=rd(SPI);s->trace.init_mux=rd(MUX);
    }
#else
    wr(SPI+8,value);
#endif
    /* As in stock, latch goes low AFTER starting the next transfer. */
    if(lower_latch)change(PA,LATCH,0);
    /* Do not charge preemption before starting the byte to its timeout. */
    start=s->now_us(s->context);
    first=status=rd(SPI);
    while(!(status&0x8000u)){
        ++polls;
        now=s->now_us(s->context);
        if(polls>=1000000u || (uint32_t)(now-start)>=10000u){
            /* Hardware may have completed since the first status read while
               an IRQ/task delayed our timer read. Completion wins that race;
               a genuinely stuck controller still fails closed as before. */
            status=rd(SPI);
            if(status&0x8000u)break;
            /* Failure-only reads: no printing, locking, retry or reset until
               the evidence is captured. Never read BUF (may have side effects). */
            s->failure.reason=((uint32_t)(now-start)>=10000u?1u:0u) |
                              (polls>=1000000u?2u:0u);
            s->failure.row=s->row;s->failure.phase=phase;s->failure.value=value;
            s->failure.start_us=start;s->failure.end_us=now;s->failure.polls=polls;
            s->failure.con=status;s->failure.baud_written=BAUD_WRITE_VALUE;
            s->failure.pre_con=pre;s->failure.cleared_con=cleared;s->failure.first_con=first;
#ifdef FM1_KEYSCAN_DMA2
            s->failure.dma_count=rd(SPI+16);
#endif
            s->failure.mux=rd(MUX);s->failure.pa_out=rd(PA);s->failure.pa_dir=rd(PA+8);
            s->failure.valid=1;
            fm1_wl82_keyscan_stop(s);return FM1_NES_IO_ERROR;
        }
        status=rd(SPI);
    }
    change(SPI,0,0x4000);return 0;
}
static int shift(fm1_wl82_keyscan *s,uint16_t word,int lower_latch){
#ifdef FM1_KEYSCAN_DMA2
    /* Poll one completion for the full two-byte word, not one per BUF byte.
       IRQ stays disabled: this experiment changes only the transfer path. */
    return transfer(s,word,lower_latch,2);
#else
    if(transfer(s,(uint8_t)(word>>8),lower_latch,0))return FM1_NES_IO_ERROR;
    return transfer(s,(uint8_t)word,0,1);
#endif
}
int fm1_wl82_keyscan_raw(fm1_wl82_keyscan *s,uint8_t rows[FM1_STOCK_SCAN_ROWS]){
    unsigned i;
    if(!s || !rows)return FM1_NES_INVALID;
    if(!s->running)return FM1_NES_BSP_UNVERIFIED;
#ifdef FM1_KEYSCAN_IRQ
    if(s->async_mode)return FM1_NES_INVALID; /* Never mix synchronous and IRQ ownership. */
#endif
    s->trace.scan_con=rd(SPI);s->trace.scan_mux=rd(MUX);
    if(s->trace.scans!=UINT32_MAX)++s->trace.scans;
    if(!s->trace.change_scan &&
       (((s->trace.scan_con^s->trace.init_con)&~0xc000u) ||
        ((s->trace.scan_mux^s->trace.init_mux)&0x30000u))) {
        s->trace.change_scan=s->trace.scans;
        s->trace.change_con=s->trace.scan_con;s->trace.change_mux=s->trace.scan_mux;
    }
    for(i=0;i<FM1_STOCK_SCAN_ROWS;++i){
        uint16_t word;
        /* Stock samples the previously latched selection BEFORE the latch edge.
           Preserve its logical row labels, which are pipeline-phase labels. */
        rows[s->row]=fm1_stock_pack_columns(rd(PA+4),rd(PB+4));
        change(PA,0,LATCH);
        if(++s->row==FM1_STOCK_SCAN_ROWS)s->row=0;
        word=(uint16_t)(0xffffu^(1u<<s->row));
        if(s->row<2)word&=(uint16_t)~(0x800u<<s->row);
        if(shift(s,word,1))return FM1_NES_IO_ERROR;
    }
    return 0;
}
int fm1_wl82_keyscan_poll(fm1_wl82_keyscan *s,uint64_t *pressed){
    uint8_t rows[FM1_STOCK_SCAN_ROWS];int rc;
    if(!pressed)return FM1_NES_INVALID;
    rc=fm1_wl82_keyscan_raw(s,rows);
    if(!rc)*pressed=fm1_stock_decode_keys(rows);
    return rc;
}
static int configure(fm1_wl82_keyscan *s,void *ctx,uint32_t (*clock)(void *),uint32_t con){
    if(!s || !clock)return FM1_NES_INVALID;
    if(s->running)return FM1_NES_BUSY;
    s->failure=(fm1_wl82_keyscan_failure){0};
    s->trace=(fm1_wl82_keyscan_trace){0};
    s->context=ctx;s->now_us=clock;s->row=0;s->running=1;
#ifdef FM1_KEYSCAN_IRQ
    s->async_mode=s->primed=s->paused=0;
    s->completions=s->observed_completions=s->sequence=s->consumed=0;
#endif
    wr(SPI,0);
    outputs(PA,LATCH|(1u<<3)|(1u<<4)|A_LEDS);outputs(PH,H_LEDS);
    /* Stock's four multiplex outputs additionally enable both drive controls. */
    change(PA+0x18,0,A_LEDS);change(PA+0x1c,0,A_LEDS);
    change(PH+0x18,0,H_LEDS);change(PH+0x1c,0,H_LEDS);
    inputs(PA,A_COLS);inputs(PB,B_COLS);
    change(MUX,3u<<16,1u<<17);
    /* Stock 0x020044a4..aa configures CON before writing the divider, then
       enables at 0x020044fe. Do not write BAUD while CON is reset to zero. */
    wr(SPI,con);wr(SPI+4,BAUD_WRITE_VALUE);
    return 0;
}
int fm1_wl82_keyscan_start(fm1_wl82_keyscan *s,void *ctx,uint32_t (*clock)(void *)){
    uint64_t discarded;int rc=configure(s,ctx,clock,0x4020);
    if(rc)return rc;
    /* Same master/write configuration; bit 13 (IRQ enable) deliberately clear.
       The DMA variant arms its first word before enabling, as stock does. */
#ifndef FM1_KEYSCAN_DMA2
    wr(SPI,0x4021);
    /* WL82.h declares BAUD/ADR write-only. Never infer their state by reading. */
    s->trace.init_con=rd(SPI);s->trace.init_mux=rd(MUX);
#endif
    if(shift(s,0xfffe,0))return FM1_NES_IO_ERROR;
    /* The first sample precedes a known latch; never publish this first sweep. */
    return fm1_wl82_keyscan_poll(s,&discarded);
}
#ifdef FM1_KEYSCAN_IRQ
static void async_arm(uint16_t word){
    fm1_key_dma[0]=(uint8_t)(word>>8);fm1_key_dma[1]=(uint8_t)word;
#ifdef FM1_WL82_KEYSCAN_TEST
    fm1_keyscan_test_dma_source(fm1_key_dma);
#else
    __asm__ volatile("csync" ::: "memory");
#endif
    wr(SPI+12,(uint32_t)(uintptr_t)fm1_key_dma);wr(SPI+16,2);
}
int fm1_wl82_keyscan_async_start(fm1_wl82_keyscan *s,void *ctx,uint32_t (*clock)(void *)){
    int rc=configure(s,ctx,clock,0x6020);if(rc)return rc;
    s->async_mode=1;s->observed_at=clock(ctx);
    async_arm(0xfffe);change(SPI,0,1); /* Stock: arm then enable, no second clear. */
    s->trace.init_con=rd(SPI);s->trace.init_mux=rd(MUX);
    return 0; /* No priming wait: the first complete ISR sweep is discarded. */
}
void fm1_wl82_keyscan_async_kick(fm1_wl82_keyscan *s){
    if(!s || !s->running || !s->async_mode || !s->paused)return;
    /* The previous ISR already latched row10. Preserve that pipeline state:
       shift row0 with latch falling AFTER arm, as with continuous scanning. */
    s->paused=0;
    async_arm(0xf7fe);change(PA,LATCH,0);
}
void fm1_wl82_keyscan_async_step(fm1_wl82_keyscan *s){
    uint16_t word;unsigned i;
    if(!s || !s->running || !s->async_mode || !(rd(SPI)&0x8000u))return;
    change(SPI,0,0x4000u);
    s->work_rows[s->row]=fm1_stock_pack_columns(rd(PA+4),rd(PB+4));
    change(PA,0,LATCH);
    if(++s->row==11){
        s->row=0;
        if(s->primed){
            for(i=0;i<11;i++)s->ready_rows[i]=s->work_rows[i];
            ++s->sequence;
        } else s->primed=1;
        if(s->trace.scans!=UINT32_MAX)++s->trace.scans;
#ifdef FM1_KEYSCAN_PACED
        /* No outstanding transfer or pending interrupt. Keep the just-latched
           selection stable until the independent 1ms timer resumes row0. */
        s->paused=1;++s->completions;
        return;
#endif
    }
    word=(uint16_t)(0xffffu^(1u<<s->row));
    if(s->row<2)word&=(uint16_t)~(0x800u<<s->row);
    async_arm(word);change(PA,LATCH,0);
    ++s->completions;
}
int fm1_wl82_keyscan_async_raw(fm1_wl82_keyscan *s,uint8_t rows[11]){
    unsigned i;uint32_t now,status;
#if defined(FM1_MOD_PLAYER) && defined(FM1_LCD_ASYNC)
    /* The MOD screen streams a complete RGB444 frame through SPI1 DMA.
       Its 23 ms transfer can defer the independent 1 ms pacing callback;
       leave room for that bounded display burst before declaring SPI2 dead. */
    const uint32_t no_progress_limit_us=30000u;
#else
    const uint32_t no_progress_limit_us=10000u;
#endif
    if(!s || !rows)return FM1_NES_INVALID;
    if(!s->running || !s->async_mode)return FM1_NES_BSP_UNVERIFIED;
    now=s->now_us(s->context);
    s->trace.scan_con=rd(SPI);s->trace.scan_mux=rd(MUX);
    if(s->trace.scans && !s->trace.change_scan &&
       (((s->trace.scan_con^s->trace.init_con)&~0xc000u) ||
        ((s->trace.scan_mux^s->trace.init_mux)&0x30000u))) {
        s->trace.change_scan=s->trace.scans;s->trace.change_con=s->trace.scan_con;
        s->trace.change_mux=s->trace.scan_mux;
    }
    if(s->completions!=s->observed_completions){
        s->observed_completions=s->completions;s->observed_at=now;
    } else if((uint32_t)(now-s->observed_at)>=no_progress_limit_us){
        /* Task-side no-progress watchdog, including a lost/masked IRQ with
           PND already set. Never wait here or re-arm a possibly active DMA. */
        status=rd(SPI);
        s->failure=(fm1_wl82_keyscan_failure){0};
        s->failure.valid=1;s->failure.reason=4;s->failure.phase=2;
        s->failure.row=s->row;s->failure.value=(uint32_t)(fm1_key_dma[0]<<8)|fm1_key_dma[1];
        s->failure.start_us=s->observed_at;s->failure.end_us=now;
        s->failure.con=status;s->failure.baud_written=BAUD_WRITE_VALUE;
        s->failure.dma_count=rd(SPI+16);s->failure.mux=rd(MUX);
        s->failure.pa_out=rd(PA);s->failure.pa_dir=rd(PA+8);
        fm1_wl82_keyscan_stop(s);return FM1_NES_IO_ERROR;
    }
    if(s->sequence==s->consumed)return FM1_NES_BUSY;
    for(i=0;i<11;i++)rows[i]=s->ready_rows[i];
    s->consumed=s->sequence;
    return 0;
}
#endif
