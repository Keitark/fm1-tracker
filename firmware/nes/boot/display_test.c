/* T1: recovered SPI1/panel protocol only. No audio, scanner, ROM, flash writes,
 * guessed reset/backlight pins, clock changes, or stock function calls.
 * A blank result does not distinguish SDK startup from panel power/reset.
 */
#include "display_test.h"
#include "clock_contract.h"
#include <stddef.h>
#ifdef FM1_LCD_ASYNC
#include <string.h>
#if !(defined(FM1_NES_PLAYER) || defined(FM1_MOD_PLAYER)) || !defined(FM1_LCD_STOCK_SEQUENCE)
#error Asynchronous LCD requires NES or MOD and the qualified stock sequence
#endif
#endif
#if defined(FM1_LCD_STOCK_SEQUENCE) && (!defined(FM1_LCD_STOCK_DMA) || !defined(FM1_LCD_STOCK_FILL))
#error "Stock sequence requires stock DMA and fill"
#endif
#ifdef FM1_DISPLAY_TEST_HOST
extern volatile uint32_t *fm1_display_test_register(uint32_t);
extern uint32_t timer_get_ms(void);
extern void os_time_dly(int);
extern void wdt_clear(void);
extern int clk_get(const char *);
extern int gpio_direction_output(unsigned int,int);
extern int gpio_set_direction(unsigned int,unsigned int);
#define REG32(a) (*fm1_display_test_register(a))
#else
#include "system/includes.h"
#include "system/sys_time.h"
#include "os/os_api.h"
#include "asm/clock.h"
#include "asm/wdt.h"
#include "asm/WL82.h"
#include "asm/gpio.h"
typedef char spi_mux_check[JL_IOMAP_BASE+offsetof(JL_IOMAP_TypeDef,CON1)==0x51020?1:-1];
#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#endif
#define OUT REG32(0x50080)
#define DIR REG32(0x50088)
#define DIE REG32(0x5008c)
#define PU REG32(0x50090)
#define PD REG32(0x50094)
#define MUX REG32(0x51020)
#define CON REG32(0x11d00)
#define BAUD REG32(0x11d04)
#define BUF REG32(0x11d08)
#define DMA_ADR REG32(0x11d0c)
#define DMA_CNT REG32(0x11d10)
#define CS (1u<<7)
#define DC (1u<<8)
#define PINS (CS|DC|(1u<<9)|(1u<<10))

/* Byte-for-byte stock table, also used by fm1_board.c. A host test checks
 * these copies agree. Index 1 encodes a delay, never an LCD command. */
static const uint8_t panel_init[21][18] = {
    {0x11,0}, {0x45,120},
    {0x2a,4,0,0,0,0xef}, {0x2b,4,0,0x28,1,0x17},
    {0xb2,5,0x0c,0x0c,0x0c,0,0x33,0x33}, {0x20,0},
    {0xb7,1,0x56}, {0xbb,1,0x18}, {0xc0,1,0x2c},
    {0xc2,1,1}, {0xc3,1,0x1f}, {0xc4,1,0x20}, {0xc6,1,0x0f},
    {0xd0,2,0xa6,0xa1},
    {0xe0,14,0xd0,0x0d,0x14,0x0b,0x0b,7,0x3a,0x44,0x50,8,0x13,0x13,0x2d,0x32},
    {0xe1,14,0xd0,0x0d,0x14,0x0b,0x0b,7,0x3a,0x44,0x50,8,0x13,0x13,0x2d,0x32},
    {0x36,1,0}, {0x3a,1,0x55}, {0xe7,1,0}, {0x51,1,0xff}, {0x21,0}
};
volatile uint32_t fm1_display_stage,fm1_display_frames;
volatile int fm1_display_error,fm1_display_sys_hz,fm1_display_lsb_hz;
static int enabled;
#ifdef FM1_LCD_ASYNC
/* NES uses two owned wire frames. MOD keeps one to preserve its 256 KiB song
 * and heap reserve; it is only rebuilt after the previous DMA completes. */
enum {LCD_FREE,LCD_BUILD,LCD_PENDING,LCD_ACTIVE};
#ifdef FM1_MOD_PLAYER
#define FM1_LCD_FRAME_COUNT 1
#else
#define FM1_LCD_FRAME_COUNT 2
#endif
#ifdef FM1_DISPLAY_TEST_HOST
static uint8_t fm1_lcd_frames[FM1_LCD_FRAME_COUNT][FM1_LCD_WIRE_FRAME_BYTES];
#else
__attribute__((aligned(64),used)) static uint8_t fm1_lcd_frames[FM1_LCD_FRAME_COUNT][FM1_LCD_WIRE_FRAME_BYTES];
#endif
static struct {
    int running,active,building,pending;
    unsigned state[2],rows,offset,seen_irq;
    uint32_t progress_ms;
    fm1_lcd_async_stats stats;
} lcd_async;
#ifdef FM1_DISPLAY_TEST_HOST
extern unsigned fm1_lcd_test_lock(void);
extern void fm1_lcd_test_unlock(unsigned);
extern void fm1_lcd_test_irq_enable(void (*)(void));
extern void fm1_lcd_test_irq_disable(void);
#define lcd_take fm1_lcd_test_lock
#define lcd_release fm1_lcd_test_unlock
#define LCD_BARRIER() ((void)0)
#else
#include "system/spinlock.h"
#include "asm/hwi.h"
static spinlock_t lcd_lock;
static unsigned lcd_take(void) {
    unsigned flags;local_irq_save(flags);arch_spin_lock(&lcd_lock);return flags;
}
static void lcd_release(unsigned flags){arch_spin_unlock(&lcd_lock);local_irq_restore(flags);}
#define LCD_BARRIER() __asm__ volatile("csync" ::: "memory")
#endif
static void async_shutdown(void);
#endif
#ifdef FM1_LCD_STOCK_DMA
#ifdef FM1_DISPLAY_TEST_HOST
static uint8_t fm1_lcd_dma_params[64];
#if defined(FM1_NES_PLAYER) || defined(FM1_MDX_PLAYER) || (defined(FM1_MOD_PLAYER) && !defined(FM1_LCD_ASYNC))
static uint8_t fm1_lcd_dma_pixels[3840];
#endif
extern void fm1_display_test_dma_source(const uint8_t *);
#else
/* Dedicated internal RAM, not flash/SDRAM/cache RAM; placement audited in ELF.
 * Kept alive through DMA completion or controller shutdown after a timeout. */
__attribute__((aligned(64),used)) static uint8_t fm1_lcd_dma_params[64];
#if defined(FM1_NES_PLAYER) || defined(FM1_MDX_PLAYER) || (defined(FM1_MOD_PLAYER) && !defined(FM1_LCD_ASYNC))
/* One eight-row strip. Startup continues to use the original 64-byte buffer. */
__attribute__((aligned(64),used)) static uint8_t fm1_lcd_dma_pixels[3840];
#endif
#endif
static void snapshot(unsigned phase) {
    static const uint32_t addresses[FM1_LCD_REGISTER_COUNT]={
        0x50080,0x50084,0x50088,0x5008c,0x50090,0x50094,0x50098,0x5009c,
        0x500a0,0x5101c,0x51020,0x51024,0x51028,0x51030,
        0x11d00,0x11d04,0x11d0c,0x11d10
#ifdef FM1_LCD_STOCK_SEQUENCE
        ,0x50000,0x50008
#endif
    };
    uint32_t values[FM1_LCD_REGISTER_COUNT];unsigned i;
    for(i=0;i<FM1_LCD_REGISTER_COUNT;i++)values[i]=REG32(addresses[i]);
    fm1_display_snapshot(phase,values);
}
#endif
#ifndef FM1_LCD_STOCK_FILL
static uint8_t row[480];
#endif

#ifdef FM1_BOOT_EARLY_DISPLAY
/* T2 only: conservative, finite delay independent of jiffies/scheduling.
 * At <=480 MHz, 480,000 explicit nops cannot take less than 1 ms. Loop/flash
 * overhead can make it much longer. This is not calibrated millisecond time.
 * Feed watchdog every chunk. No PLL, timer, rail or reset GPIO is changed. */
#ifndef FM1_DISPLAY_TEST_HOST
__attribute__((noinline,used))
static void fm1_early_delay_chunk(void) {
    unsigned n;
    for(n=0;n<480000u;++n)__asm__ volatile("nop");
}
#else
extern void fm1_early_delay_chunk(void);
#endif
#endif

static int fail(int rc) {
    fm1_display_error=rc;
#ifdef FM1_LCD_ASYNC
    async_shutdown();
#endif
#ifdef FM1_LCD_STOCK_DMA
    if(enabled)snapshot(3);
#endif
    if(enabled){OUT|=CS;CON=0;enabled=0;}
    return rc;
}
static int send(int data,const uint8_t *p,size_t size) {
    size_t i;
#ifdef FM1_LCD_STOCK_DMA
    if(data) {
        uint32_t start,polls=0;
        uint8_t *buffer=fm1_lcd_dma_params;
        size_t capacity=sizeof(fm1_lcd_dma_params);
#if defined(FM1_NES_PLAYER) || defined(FM1_MDX_PLAYER) || (defined(FM1_MOD_PLAYER) && !defined(FM1_LCD_ASYNC))
        if(size>capacity){buffer=fm1_lcd_dma_pixels;capacity=sizeof(fm1_lcd_dma_pixels);}
#endif
        if(!p || !size || size>capacity)return fail(-5);
        if (buffer != p) for(i=0;i<size;i++)buffer[i]=p[i];
#ifdef FM1_DISPLAY_TEST_HOST
        fm1_display_test_dma_source(buffer);
#else
        /* Internal RAM DMA has no SDRAM writeback-cache dependency. */
        __asm__ volatile("csync" ::: "memory");
#endif
        OUT|=DC;OUT&=~CS;CON|=0x4000;
        start=timer_get_ms();
        DMA_ADR=(uint32_t)(uintptr_t)buffer;
        DMA_CNT=(uint32_t)size;
        while(!(CON&0x8000)) {
            if(++polls>=1000000u || (uint32_t)(timer_get_ms()-start)>=100u)return fail(-6);
        }
        CON|=0x4000;OUT|=CS;return 0;
    }
#endif
    if(data)OUT|=DC;else OUT&=~DC;
    OUT&=~CS;
    for(i=0;i<size;++i) {
        uint32_t polls=0;
#ifndef FM1_BOOT_EARLY_DISPLAY
        uint32_t start=timer_get_ms();
#endif
        CON|=0x4000;BUF=p[i];
        while(!(CON&0x8000)) {
            /* The count also bounds a stopped timer, unlike the first NES build. */
            if(++polls>=1000000u
#ifndef FM1_BOOT_EARLY_DISPLAY
               || (uint32_t)(timer_get_ms()-start)>=100u
#endif
               )
                return fail(-2);
        }
    }
    CON|=0x4000;OUT|=CS;return 0;
}
static int command(uint8_t c,const uint8_t *p,size_t n) {
    if(send(0,&c,1))return -2;
    return n?send(1,p,n):0;
}
#if defined(FM1_NES_PLAYER) || defined(FM1_MDX_PLAYER) || defined(FM1_MOD_PLAYER)
#if defined(FM1_NES_PLAYER) && defined(FM1_LCD_STOCK_DMA)
uint8_t *fm1_display_strip_buffer(size_t *length) {
    if (length) *length=sizeof(fm1_lcd_dma_pixels);
    return fm1_lcd_dma_pixels;
}
#endif
int fm1_display_write(int data,const uint8_t *p,size_t n) {
    size_t count;
#ifdef FM1_LCD_ASYNC
    /* Once enabled, the async producer owns all LCD commands and DMA. */
    if(lcd_async.running)return -8;
#endif
    if(!enabled || (!p && n))return -4;
    while(n) {
        /* Complete each persistent-RAM strip DMA before reusing its buffer.
           Startup still calls send() directly with stock 40-byte blocks.
           Commands remain byte-polled; never DMA directly from NES buffers. */
#if defined(FM1_MOD_PLAYER) && defined(FM1_LCD_ASYNC)
        count=n>sizeof(fm1_lcd_dma_params)?sizeof(fm1_lcd_dma_params):n;
#else
        count=n>sizeof(fm1_lcd_dma_pixels)?sizeof(fm1_lcd_dma_pixels):n;
#endif
        if(send(data,p,count))return -2;
        p+=count;n-=count;
    }
    wdt_clear();return 0;
}
#endif
#ifdef FM1_LCD_ASYNC
/* Called with the short IRQ-safe lock held. No waits, copies or log output. */
static void async_arm(void) {
    const uint8_t *source=fm1_lcd_frames[lcd_async.active]+lcd_async.offset;
    LCD_BARRIER();
    OUT|=DC;OUT&=~CS;
    CON=0x6021; /* qualified mode + pending clear + completion interrupt */
#ifdef FM1_DISPLAY_TEST_HOST
    fm1_display_test_dma_source(source);
#endif
    DMA_ADR=(uint32_t)(uintptr_t)source;DMA_CNT=FM1_LCD_WIRE_STRIP_BYTES;
}
#ifndef FM1_DISPLAY_TEST_HOST
___interrupt
#endif
static void fm1_lcd_spi1_isr(void) {
    unsigned flags=lcd_take();
    if(lcd_async.running && lcd_async.active>=0 && (CON&0x8000)) {
        CON=0x4021; /* acknowledge and mask while updating the descriptor */
        ++lcd_async.stats.irqs;lcd_async.stats.bytes+=FM1_LCD_WIRE_STRIP_BYTES;
        lcd_async.offset+=FM1_LCD_WIRE_STRIP_BYTES;
        if(lcd_async.offset==FM1_LCD_WIRE_FRAME_BYTES) {
            OUT|=CS;
            lcd_async.state[lcd_async.active]=LCD_FREE;lcd_async.active=-1;
            ++lcd_async.stats.completed;
        } else async_arm();
    }
    lcd_release(flags);
}
static void async_shutdown(void) {
    unsigned flags;
#ifdef FM1_DISPLAY_TEST_HOST
    fm1_lcd_test_irq_disable();
#else
    bit_clr_ie(IRQ_SPI1_IDX,0);
#endif
    flags=lcd_take();
    if(lcd_async.running) {
        /* Stop the controller BEFORE making either buffer reusable. */
        CON=0;OUT|=CS;lcd_async.running=0;
        lcd_async.active=lcd_async.pending=lcd_async.building=-1;
        lcd_async.state[0]=lcd_async.state[1]=LCD_FREE;
    }
    lcd_release(flags);
#ifndef FM1_DISPLAY_TEST_HOST
    unrequest_irq(IRQ_SPI1_IDX,0);
#endif
}
int fm1_display_async_start(void) {
    if(!enabled || lcd_async.running)return -4;
#ifdef FM1_LCD_FAST_SPI
    /* Bench-only single-variable test: retain stock startup at BAUD4, then
     * change only idle SPI1. Refuse an unexpected source clock; never raise
     * LSB/PLL/CPU clocks to obtain the requested nominal SPI rate. */
    if(clk_get("lsb")!=60000000)return fail(-11);
#endif
#ifdef FM1_LCD_RGB444
    /* Stock startup/fill remain RGB565. Switch only while DMA/IRQ are idle.
     * COLMOD: preserve RGB-interface 101, MCU/serial 011 = packed 12 bpp.
     * ST7789V v1.2 sections 8.8.41 and 9.1.33 (3Ah). */
    {static const uint8_t format=0x53;
     int rc=command(0x3a,&format,1);if(rc)return rc;}
#endif
    memset(&lcd_async,0,sizeof(lcd_async));
    lcd_async.active=lcd_async.building=lcd_async.pending=-1;
    lcd_async.running=1;CON=0x4021;
#ifdef FM1_LCD_FAST_SPI
    BAUD=FM1_LCD_STREAM_BAUD;
#endif
#ifdef FM1_DISPLAY_TEST_HOST
    fm1_lcd_test_irq_enable(fm1_lcd_spi1_isr);
#else
    request_irq(IRQ_SPI1_IDX,2,fm1_lcd_spi1_isr,0);
#endif
    return 0;
}
int fm1_display_async_service(void) {
    static const uint8_t window[4]={0,0,0,239};
    uint32_t now=timer_get_ms();unsigned flags=lcd_take();int index;
    if(!lcd_async.running){lcd_release(flags);return -4;}
    if(lcd_async.active>=0) {
        int timeout=0;
        if(lcd_async.seen_irq!=lcd_async.stats.irqs) {
            lcd_async.seen_irq=lcd_async.stats.irqs;lcd_async.progress_ms=now;
        } else timeout=(uint32_t)(now-lcd_async.progress_ms)>=250u;
        if(timeout)++lcd_async.stats.errors;
        lcd_release(flags);
        return timeout?fail(-9):0;
    }
    index=lcd_async.pending;
    if(index<0){lcd_release(flags);return 0;}
    lcd_async.pending=-1;lcd_async.active=index;lcd_async.state[index]=LCD_ACTIVE;
    lcd_async.offset=0;lcd_async.progress_ms=now;lcd_async.seen_irq=lcd_async.stats.irqs;
    lcd_release(flags);
    /* No DMA active here; interrupt source remains masked. Commands retain
     * the qualified bounded synchronous path, with interrupts enabled. */
    if(command(0x2a,window,4)||command(0x2b,window,4)||command(0x2c,0,0))return -2;
    flags=lcd_take();async_arm();lcd_release(flags);return 0;
}
int fm1_display_async_begin(int wanted) {
    unsigned flags,i;
    int rc=fm1_display_async_service();if(rc)return rc;
    flags=lcd_take();
    if(lcd_async.building>=0){lcd_release(flags);return fail(-10);}
    if(!wanted){lcd_release(flags);return 0;}
    /* DMA may complete after service() releases the lock, leaving a FREE
       buffer beside an older PENDING frame. Do not reserve a new BUILD until
       that pending frame has been serviced; otherwise publication collides. */
    if(lcd_async.pending>=0) {
        ++lcd_async.stats.busy_skips;lcd_release(flags);return 0;
    }
    for(i=0;i<FM1_LCD_FRAME_COUNT;++i)if(lcd_async.state[i]==LCD_FREE) {
        lcd_async.building=(int)i;lcd_async.state[i]=LCD_BUILD;lcd_async.rows=0;
        lcd_release(flags);return 1;
    }
    ++lcd_async.stats.busy_skips;lcd_release(flags);return 0;
}
#ifdef FM1_LCD_RGB444
#ifndef FM1_DISPLAY_TEST_HOST
__attribute__((noinline,used))
#endif
static void fm1_lcd_pack_rgb444(uint8_t *dst,const uint8_t *src,unsigned pairs) {
    /* RGB565 big-endian -> R0G0 B0R1 G1B1. Truncate to the top 4 bits
     * of each channel. Every 240-pixel row and 8-row DMA ends on a pair. */
    while(pairs--) {
        unsigned a=((unsigned)src[0]<<8)|src[1];
        unsigned b=((unsigned)src[2]<<8)|src[3];
        dst[0]=(uint8_t)(((a>>8)&0xf0)|((a>>7)&0x0f));
        dst[1]=(uint8_t)(((a<<3)&0xf0)|((b>>12)&0x0f));
        dst[2]=(uint8_t)(((b>>3)&0xf0)|((b>>1)&0x0f));
        src+=4;dst+=3;
    }
}
#endif
static int async_rows_valid(unsigned y,unsigned rows,const void *source) {
    return lcd_async.running && lcd_async.building>=0 && source && rows && rows<=8 &&
           y==lcd_async.rows && y<240 && rows<=240-y;
}
static int async_publish_rows(unsigned rows) {
    unsigned flags;int index=lcd_async.building;
    lcd_async.rows+=rows;
    if(lcd_async.rows!=240)return 0;
    flags=lcd_take();
    if(lcd_async.pending>=0){lcd_release(flags);return fail(-10);}
    LCD_BARRIER();lcd_async.state[index]=LCD_PENDING;lcd_async.pending=index;
    lcd_async.building=-1;++lcd_async.stats.submitted;lcd_release(flags);
    return fm1_display_async_service();
}
int fm1_display_async_rows(unsigned y,unsigned rows,const uint8_t *wire) {
    int index=lcd_async.building;
    if(!async_rows_valid(y,rows,wire))return fail(-10);
    /* BUILD is task-owned; DMA/IRQ cannot access this buffer. Copy with
     * interrupts enabled. Publication occurs only after all 240 rows exist. */
#ifdef FM1_LCD_RGB444
    fm1_lcd_pack_rgb444(fm1_lcd_frames[index]+y*FM1_LCD_WIRE_ROW_BYTES,wire,rows*120u);
#else
    memcpy(fm1_lcd_frames[index]+y*FM1_LCD_WIRE_ROW_BYTES,wire,rows*480u);
#endif
    return async_publish_rows(rows);
}
#ifdef FM1_LCD_DIRECT
#include "fm1_lcd_pack.h"
int fm1_display_async_native_rows(unsigned y,unsigned rows,const uint16_t *pixels,unsigned crop) {
    if(!async_rows_valid(y,rows,pixels) || crop>1)return fail(-10);
    /* Write only the task-owned BUILD buffer; publish after all rows exist. */
    fm1_lcd_pack_native_rgb444(fm1_lcd_frames[lcd_async.building]+y*360u,pixels,rows,crop);
    return async_publish_rows(rows);
}
#endif
void fm1_display_async_snapshot(fm1_lcd_async_stats *stats) {
    unsigned flags=lcd_take();*stats=lcd_async.stats;
    stats->active=lcd_async.running && lcd_async.active>=0;
    stats->pending=lcd_async.running && lcd_async.pending>=0;
    lcd_release(flags);
}
#endif
static int panel_delay(uint32_t ms) {
#ifdef FM1_LCD_STOCK_SEQUENCE
    /* Stock0205b9d2: max(1, ms/10 + (ms%10 > 5)) scheduler ticks. */
    uint32_t start=timer_get_ms(),ticks=ms/10u+(ms%10u>5u);
    if(!ticks)ticks=1;
    wdt_clear();os_time_dly((int)ticks);
    if(timer_get_ms()==start)return fail(-3);
#elif defined(FM1_BOOT_EARLY_DISPLAY)
    uint32_t i;
    if(ms>250u)return fail(-3);
    for(i=0;i<ms;++i){wdt_clear();fm1_early_delay_chunk();}
#else
    uint32_t start=timer_get_ms(),iterations=0;
    while((uint32_t)(timer_get_ms()-start)<ms+1u) {
        wdt_clear();os_time_dly(1);
        if(++iterations>ms+100u)return fail(-3);
    }
#endif
    return 0;
}
#ifndef FM1_DISPLAY_TEST_HOST
__attribute__((noinline,used))
#endif
int fm1_display_test_frame(uint32_t elapsed_ms) {
#ifdef FM1_LCD_ASYNC
    if(lcd_async.running)return -8;
#endif
#ifdef FM1_LCD_STOCK_FILL
    /* Stock 010 0x02021600: r0=r1=240, rev8, then four bytes per
     * CASET/RASET. This OVERRIDES the earlier panel-init table window.
     * Replay the observed bytes/count, not inferred inclusive geometry.
     * Accepted V14 polled-white patch emits exactly 115200 bytes of FF.
     * No CPU framebuffer, per-row window or RAM pixel-buffer dependency. */
    static const uint8_t stock_window[4]={0,0,0,0xf0};
    unsigned i;(void)elapsed_ms;
    if(!enabled)return -4;
    if(command(0x2a,stock_window,4)||command(0x2b,stock_window,4)||command(0x2c,0,0))return -2;
#ifdef FM1_LCD_STOCK_SEQUENCE
    {
        uint8_t block[40];
        /* Unmodified stock's first fill is black. White is a later diagnostic
         * frame, only after init has sent display-on. DMA source is the audited
         * persistent internal-RAM buffer inside send(), not this stack array. */
        for(i=0;i<sizeof(block);i++)block[i]=fm1_display_frames?0xff:0;
        for(i=0;i<2880u;i++) {
            if(send(1,block,sizeof(block)))return -2;
            /* Deliberate recovery difference: yield with CS HIGH, between
             * stock-sized transactions, never splitting a DMA transfer. */
            if((i&63u)==63u){wdt_clear();os_time_dly(1);}
        }
    }
#else
    OUT|=DC;OUT&=~CS;
    for(i=0;i<115200u;++i) {
        uint32_t polls=0,start=timer_get_ms();
        CON|=0x4000;BUF=0xff;
        while(!(CON&0x8000)) {
            if(++polls>=1000000u || (uint32_t)(timer_get_ms()-start)>=100u)return fail(-2);
        }
        /* CS remains low and DC high across the entire stream. Yielding
         * keeps serial recovery responsive; it does not readdress the LCD. */
        if((i&4095u)==4095u){wdt_clear();os_time_dly(1);}
    }
    CON|=0x4000;OUT|=CS;
#endif
    ++fm1_display_frames;fm1_display_stage=4;return 0;
#else
    unsigned y,x;
    uint8_t columns[4]={0,0,0,239};
    if(!enabled)return -4;
    if(command(0x2a,columns,4))return -2;
    for(y=0;y<240;++y) {
        unsigned yy=y+40;
        uint8_t rows[4]={(uint8_t)(yy>>8),(uint8_t)yy,(uint8_t)(yy>>8),(uint8_t)yy};
        uint16_t color=y<80?0xf800:(y<160?0x07e0:0x001f);
        /* Top strip blinks white/black each second; bottom turns white at 20 s. */
        if(y<12)color=(elapsed_ms/1000u)&1u?0xffff:0;
#ifdef FM1_BOOT_EARLY_DISPLAY
        /* First board-level picture has a yellow top strip. Worker frames
         * use the ordinary black/white blink; no timer needed for first fill. */
        if(y<12 && fm1_display_stage==3)color=0xffe0;
#endif
        if(y>=228 && elapsed_ms>=20000u)color=0xffff;
        for(x=0;x<240;++x){row[2*x]=(uint8_t)(color>>8);row[2*x+1]=(uint8_t)color;}
        if(command(0x2b,rows,4)||command(0x2c,0,0)||send(1,row,sizeof(row)))return -2;
        wdt_clear();
        /* Yield without tying rendering to scanner/audio services. */
#ifndef FM1_BOOT_EARLY_DISPLAY
        if((y&7u)==7u)os_time_dly(1);
#endif
    }
    ++fm1_display_frames;fm1_display_stage=4;
    return 0;
#endif
}
#ifndef FM1_DISPLAY_TEST_HOST
__attribute__((noinline,used))
#endif
int fm1_display_test_init(void) {
    unsigned i;
    if(enabled)return -4;
    fm1_display_stage=1;fm1_display_error=0;fm1_display_frames=0;
#ifndef FM1_BOOT_EARLY_DISPLAY
    fm1_display_sys_hz=clk_get("sys");fm1_display_lsb_hz=clk_get("lsb");
    if(!fm1_clock_report_valid(fm1_display_sys_hz,fm1_display_lsb_hz))return fail(-1);
#ifndef FM1_LCD_STOCK_DMA
    if(panel_delay(100))return -3;
#endif
#else
    /* No clock-report gate in T2: the minimum delay assumes at most 480 MHz,
     * the same upper reporting bound reviewed in stock and SDK clock code. */
    fm1_display_sys_hz=0;fm1_display_lsb_hz=0;
#endif
    fm1_display_stage=2;
#ifdef FM1_LCD_STOCK_DMA
    snapshot(0);
#endif
#ifdef FM1_LCD_STOCK_SEQUENCE
    /* Stock02004504 explicitly drives PA2 low before the UI LCD task.
     * Its electrical role is NOT identified here as backlight/reset/OE.
     * Keep that established level after STOP; do not invent an off level. */
    if(gpio_direction_output(0x02,0))return fail(-7);
    /* Stock02023086..020230a8. Do not add pull/DIE writes absent here. */
    MUX|=1u<<4;
    if(gpio_direction_output(0x27,1) || gpio_set_direction(0x28,0) ||
       gpio_set_direction(0x29,0) || gpio_set_direction(0x2a,0))return fail(-7);
#else
    CON=0;OUT|=CS;PU&=~PINS;PD&=~PINS;DIE|=PINS;DIR&=~PINS;
    MUX|=1u<<4;
#endif
    /* Stock010 020230b6/020230b8: enable SPI1 before writing its divider.
     * BAUD is write-only; a zero readback cannot validate or reject this write. */
    CON=0x4021;BAUD=4;enabled=1;
#ifdef FM1_LCD_STOCK_DMA
    snapshot(1);
#endif
#if defined(FM1_BOOT_EARLY_DISPLAY) || defined(FM1_LCD_STOCK_DMA)
    /* Stock configures SPI1 before this initial wait. */
    if(panel_delay(100))return -3;
#endif
    for(i=0;i<21;++i) {
        if(i==1){if(panel_delay(120))return -3;}
        else if(command(panel_init[i][0],panel_init[i]+2,panel_init[i][1]))return -2;
    }
    fm1_display_stage=3;
    if(fm1_display_test_frame(0))return -2;
    if(command(0x29,0,0))return -2;
#ifdef FM1_LCD_STOCK_DMA
    snapshot(2);
#endif
    return 0;
}
void fm1_display_test_stop(void) {
#ifdef FM1_LCD_ASYNC
    async_shutdown();
#endif
    if(enabled){OUT|=CS;CON=0;enabled=0;
#ifdef FM1_LCD_STOCK_DMA
        snapshot(4);
#endif
    }
}
