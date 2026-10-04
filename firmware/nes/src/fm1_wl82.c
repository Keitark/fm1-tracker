#include "fm1_wl82.h"
#include "asm/iis.h"
#include <string.h>
#ifndef FM1_WL82_TEST
#include "asm/WL82.h"
/* Check the hand-written MMIO address against the actual SDK struct layout. */
typedef char fm1_spi1_mux_address_check[
    JL_IOMAP_BASE + offsetof(JL_IOMAP_TypeDef, CON1) == 0x51020 ? 1 : -1];
#endif

/* Addresses checked against SDK wl82.h and this device's stock disassembly.
   Single-owner polled SPI: no IRQ/DMA, cache-coherency or reused-buffer hazard.
   Throughput is NOT yet qualified for full-rate NES rendering. */
#ifdef FM1_WL82_TEST
/* Only the dedicated host test build substitutes register storage. */
extern volatile uint32_t *fm1_test_register(uint32_t);
#define REG32(a) (*fm1_test_register(a))
#else
#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#endif
#define PORTC_OUT REG32(0x50080)
#define PORTC_DIR REG32(0x50088)
#define PORTC_DIE REG32(0x5008c)
#define PORTC_PU  REG32(0x50090)
#define PORTC_PD  REG32(0x50094)
#define IOMAP_CON1 REG32(0x51020)
#define SPI_CON REG32(0x11d00)
#define SPI_BAUD REG32(0x11d04)
#define SPI_BUF REG32(0x11d08)
#define LCD_CS (1u<<7)
#define LCD_DC (1u<<8)
#define LCD_PINS (LCD_CS|LCD_DC|(1u<<9)|(1u<<10))

static fm1_board board;
static fm1_audio_queue queue;
static fm1_audio_startup startup_audio;
static fm1_wl82_services services;
static fm1_wl82_audio_config audio_config;
static int installed,running;
static int output_muted;

static int set_mute(int state) {
    unsigned flags=services.irq_save(services.context);
    output_muted=state;
    services.irq_restore(services.context,flags);
    if(services.mute_policy==FM1_AUDIO_MUTE_HARDWARE &&
       services.mute(services.context,state)) {
        flags=services.irq_save(services.context);output_muted=1;
        services.irq_restore(services.context,flags);
        return FM1_NES_IO_ERROR;
    }
    return 0;
}

void fm1_wl82_stock_audio_route(fm1_wl82_audio_config *a) {
    /* FM-1_010 channel records and ALINK0 CON0; see STOCK_BOARD_PROFILE.md.
       Not an installation, hardware test or mute control. Adapter uses the
       stock 24-bit-in-32-bit-words DMA layout with 64 stereo frames per half. */
    if(!a)return;
    a->output_channel=3;a->mclk_output=1;
    a->update_edge=0;a->sclk_32_per_frame=0;
}

static int lcd_write(void *ctx,int data,const uint8_t *p,size_t n) {
    uint32_t start;
    size_t i;
    (void)ctx;
    if(!p || !n)return FM1_NES_INVALID;
    if(data)PORTC_OUT|=LCD_DC;else PORTC_OUT&=~LCD_DC;
    PORTC_OUT&=~LCD_CS;
    start=services.now_us(services.context);
    for(i=0;i<n;++i) {
        uint32_t polls=0;
        SPI_CON|=0x4000u;SPI_BUF=p[i];
        while(!(SPI_CON&0x8000u)) {
            if(++polls>=1000000u ||
               (uint32_t)(services.now_us(services.context)-start)>=100000u) {
                PORTC_OUT|=LCD_CS;SPI_CON=0;
                return FM1_NES_IO_ERROR;
            }
        }
    }
    SPI_CON|=0x4000u;PORTC_OUT|=LCD_CS;
    return 0;
}
static uint32_t now_us(void *ctx) {(void)ctx;return services.now_us(services.context);}
static int wait_us(void *ctx,uint32_t n) {(void)ctx;return services.wait_us(services.context,n);}
static int delay_ms(void *ctx,uint32_t n) {return wait_us(ctx,n*1000u);}
static uint64_t read_keys(void *ctx) {(void)ctx;return services.read_keys(services.context);}
static int stop_requested(void *ctx) {(void)ctx;return services.stop_requested(services.context);}

static int pcm_write(void *ctx,const int16_t *p,size_t n) {
    uint32_t start=now_us(ctx);
    int rc;
    do {
        unsigned irq=services.irq_save(services.context);
        rc=fm1_audio_queue_push(&queue,p,n);
        services.irq_restore(services.context,irq);
        if(rc!=FM1_NES_BUSY)return rc;
        if(stop_requested(ctx) || (uint32_t)(now_us(ctx)-start)>=100000u)
            return FM1_NES_IO_ERROR;
        if(wait_us(ctx,1000))return FM1_NES_IO_ERROR;
    }while(1);
}
static void iis_output(void *ctx,u8 *data,int len,u8 channel) {
    int32_t stereo[128];
    unsigned irq;
    int muted;
    (void)ctx;
    if(!data || len<=0)return;
    memset(data,0,(size_t)len);
    /* This configuration promises exactly one 512-byte DMA half. Reject
       changed SDK geometry without advancing the queue or startup envelope. */
    if(channel!=audio_config.output_channel || len!=512)return;
    irq=services.irq_save(services.context);
    fm1_audio_queue_stereo24(&queue,stereo,64);
    muted=output_muted;
    services.irq_restore(services.context,irq);
    if(muted)memset(stereo,0,sizeof(stereo));
    else fm1_audio_startup_process24(&startup_audio,stereo);
    memcpy(data,stereo,sizeof(stereo));
}
static void audio_irq_dispatch(void) {iis_irq_handler(0);}
int fm1_wl82_install(const fm1_wl82_services *s,const fm1_board_config *c,
                      const fm1_wl82_audio_config *a) {
    fm1_board_io io;
    int rc;
    if(running)return FM1_NES_BUSY;
    installed=0;
    if(!s || !c || !a || !s->prepare || !s->finish || !s->now_us ||
       !s->wait_us || !s->read_keys || !s->stop_requested ||
       !s->audio_irq_start || !s->audio_irq_stop ||
       !s->irq_save || !s->irq_restore || a->output_channel>3 ||
       a->mclk_output>1 || a->update_edge>1 || a->sclk_32_per_frame!=0)
        return FM1_NES_INVALID;
    if(s->mute_policy>FM1_AUDIO_MUTE_DIGITAL_ONLY ||
       (s->mute_policy==FM1_AUDIO_MUTE_HARDWARE && !s->mute) ||
       (s->mute_policy==FM1_AUDIO_MUTE_DIGITAL_ONLY && s->mute))
        return FM1_NES_INVALID;
    memset(&io,0,sizeof(io));io.lcd_write=lcd_write;io.delay_ms=delay_ms;
    io.now_us=now_us;io.wait_us=wait_us;io.read_keys=read_keys;
    io.stop_requested=stop_requested;io.pcm_write=pcm_write;
    rc=fm1_board_construct(&board,&io,c);if(rc)return rc;
    services=*s;audio_config=*a;installed=1;return 0;
}
int fm1_wl82_run_with_stats(const uint8_t *rom,size_t size,fm1_nes_stats *stats) {
    struct iis_platform_data pd;
    int rc,opened=0,spi_enabled=0,prepared=0,irq_attempted=0;
    if(stats)memset(stats,0,sizeof(*stats));
    if(!installed)return FM1_NES_BSP_UNVERIFIED;
    if(running)return FM1_NES_BUSY;
    rc=fm1_nes_validate_rom(rom,size);if(rc)return rc;
    running=1;
    rc=services.prepare(services.context);
    if(rc){rc=FM1_NES_BSP_UNVERIFIED;goto done;}
    prepared=1;
    if(set_mute(1)){rc=FM1_NES_IO_ERROR;goto done;}
    /* prepare must ensure exclusive ownership; no stock OS is assumed resident. */
    SPI_CON=0;PORTC_OUT|=LCD_CS;
    PORTC_PU&=~LCD_PINS;PORTC_PD&=~LCD_PINS;
    PORTC_DIE|=LCD_PINS;PORTC_DIR&=~LCD_PINS;
    IOMAP_CON1|=1u<<4;SPI_BAUD=4;SPI_CON=0x4021;spi_enabled=1;
    rc=fm1_board_lcd_init(&board);if(rc)goto done;
    fm1_audio_queue_reset(&queue);
    fm1_audio_startup_reset(&startup_audio);
    memset(&pd,0,sizeof(pd));pd.port_sel=IIS_PORTC;
    pd.channel_out=(u8)(1u<<audio_config.output_channel);
    pd.data_width=pd.channel_out; /* 24-bit samples in 32-bit DMA words. */
    pd.mclk_output=audio_config.mclk_output;pd.update_edge=audio_config.update_edge;
    /* SDK always passes sr_points*4 bytes: 128 -> 512 bytes / 64 stereo frames.
       Full ping-pong buffer is 1024 bytes, as in stock. */
    pd.f32e=audio_config.sclk_32_per_frame;pd.sr_points=128;
    rc=iis_open(&pd,0);
    if(rc){rc=FM1_NES_IO_ERROR;goto done;}
    opened=1;
    iis_set_dec_data_handler(0,iis_output,0);
    if(iis_set_sample_rate(44100,0)){rc=FM1_NES_IO_ERROR;goto done;}
    irq_attempted=1;
    if(services.audio_irq_start(services.context,audio_irq_dispatch)){rc=FM1_NES_IO_ERROR;goto done;}
    iis_channel_on(pd.channel_out,0);
    if(board.config.gain && set_mute(0)){rc=FM1_NES_IO_ERROR;goto done;}
    rc=fm1_board_run(&board,rom,size,0,stats);
done:
    if(prepared && set_mute(1))rc=FM1_NES_IO_ERROR;
    if(irq_attempted)services.audio_irq_stop(services.context);
    if(opened){iis_channel_off(pd.channel_out,0);iis_close(0);}
    if(spi_enabled){PORTC_OUT|=LCD_CS;SPI_CON=0;}
    services.finish(services.context);
    running=0;return rc;
}
int fm1_wl82_run(const uint8_t *rom,size_t size) {
    return fm1_wl82_run_with_stats(rom,size,0);
}
