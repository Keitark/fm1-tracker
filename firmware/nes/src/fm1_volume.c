#include "fm1_volume.h"
#include <string.h>
#ifndef FM1_VOLUME_TEST
#include "generic/typedef.h"
#include "asm/WL82.h"
#include "asm/adc_api.h"
#include <stddef.h>
typedef char volume_register_contract[
    JL_ADC_BASE==0x13100 && JL_PORTB_BASE==0x50040 &&
    JL_ANA_BASE==0x11900 && offsetof(JL_ANA_TypeDef,PLL_CON1)==0xa4 &&
    offsetof(JL_PORT_FLASH_TypeDef,DIR)==8 &&
    offsetof(JL_PORT_FLASH_TypeDef,DIE)==12 && AD_CH_PB06==4 ? 1:-1];
static uint32_t rd(uint32_t a){return *(volatile uint32_t *)(uintptr_t)a;}
static void wr(uint32_t a,uint32_t v){*(volatile uint32_t *)(uintptr_t)a=v;}
#define VOLUME_ENTRY __attribute__((noinline,used))
#else
#define VOLUME_ENTRY
extern uint32_t fm1_volume_test_read(uint32_t);
extern void fm1_volume_test_write(uint32_t,uint32_t);
#define rd fm1_volume_test_read
#define wr fm1_volume_test_write
#endif
#define ADC 0x13100u
#define PB 0x50040u
#define ANA 0x11900u
static void change(uint32_t a,uint32_t clear,uint32_t set){wr(a,(rd(a)&~clear)|set);}
static void launch(void) {
    /* SDK adc_sample: div96, delay15, channel4, bit3. ADC IRQ bit5 stays OFF.
       KICK/clear and enable sequence matches SDK; completion sampled later. */
    wr(ADC,0xf44eu);change(ADC,0,0x10);change(ADC,0,0x40);
}
VOLUME_ENTRY int fm1_volume_start(fm1_volume *s) {
    memset(s,0,sizeof(*s));
    /* Refuse an existing ADC owner, including an enabled ADC interrupt. */
    if(rd(ADC)&0x30u){s->errors=1;return -1;}
    change(PB+8,0,0x40);change(PB+12,0x40,0);
    change(PB+16,0x40,0);change(PB+20,0x40,0);
    change(PB+32,0x40,0);
    /* SDK __adc_init/adc_sample analog gates only: no voltage trim,
       VBAT/reference calibration, unbounded waits or shared ADC ISR. */
    change(ANA+0xa4,1u<<16,0);
    change(ANA,0x3c0feu,0x20005u);
    wr(ADC,0);s->running=1;launch();return 0;
}
VOLUME_ENTRY void fm1_volume_stop(fm1_volume *s) {
    if(s->running){wr(ADC,0x40);wr(ADC,0);}
    s->running=s->valid=s->target=s->debounce=s->waiting=0;
}
VOLUME_ENTRY void fm1_volume_tick(fm1_volume *s) {
    unsigned raw;int delta;
    if(!s->running)return;
    if(!(rd(ADC)&0x80)) {
        if(++s->waiting<10)return; /*20ms timeout; never spin. */
        ++s->errors;fm1_volume_stop(s);return; /* Fail muted until restart. */
    }
    raw=rd(ADC+4);s->waiting=0;
    if(raw>1023){++s->errors;fm1_volume_stop(s);return;}
    ++s->samples;s->raw=(uint16_t)raw;s->valid=1;
    delta=(int)raw-s->accepted;
    /* Stock +/-6 deadband and ten consecutive excursions, then raw>>3.
       Zero initial target prevents a loud startup before first acceptance. */
    if(delta>=-6 && delta<=6)s->debounce=0;
    else if(++s->debounce>=10) {
        s->debounce=0;
        if((raw>>3)!=(s->accepted>>3)) {
            s->accepted=(uint16_t)raw;s->target=(uint8_t)(raw>>3);
        }
    }
    wr(ADC,0x40);wr(ADC,0);launch();
}
