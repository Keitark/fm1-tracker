#define FM1_MDX_TARGET_HOST 1
#include "../src/fm1_mdx_target.c"
#include <setjmp.h>
#include <stdlib.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
volatile uint32_t fm1_display_stage;
volatile int fm1_display_error;
static unsigned ms,opened,closed,timer_deleted,key_stopped,lcd_stopped;
static uint32_t adc;
static void (*worker)(void *),(*tick)(void *),(*alink)(void),(*keyirq)(void);
static void (*output)(void *,u8 *,int,u8);
static jmp_buf done;
void arch_spin_lock(spinlock_t *l){CHECK(!*l);*l=1;}
void arch_spin_unlock(spinlock_t *l){CHECK(*l);*l=0;}
int task_create(void (*fn)(void *),void *u,const char *name){(void)u;CHECK(!strcmp(name,"peripheral"));worker=fn;return 0;}
uint32_t timer_get_ms(void){return ms;}
void wdt_clear(void){}
int clk_get(const char *s){(void)s;return 60000000;}
int iis_open(struct iis_platform_data *pd,unsigned id){CHECK(!id && pd->data_width==8 && pd->channel_out==8 && pd->sr_points==128);opened++;return 0;}
int iis_set_sample_rate(unsigned rate,unsigned id){CHECK(!id && rate==44100);return 0;}
void iis_set_dec_data_handler(unsigned id,void (*fn)(void *,u8 *,int,u8),void *u){CHECK(!id && !u);output=fn;}
void iis_channel_on(unsigned ch,unsigned id){CHECK(ch==8 && !id);}
void iis_channel_off(unsigned ch,unsigned id){CHECK(ch==8 && !id);}
void iis_close(unsigned id){CHECK(!id);closed++;}
void iis_irq_handler(unsigned id){int32_t b[128];CHECK(!id && output);output(0,(u8 *)b,512,3);}
void request_irq(unsigned id,unsigned priority,void (*fn)(void),unsigned cpu){(void)priority;CHECK(!cpu);if(id==IRQ_ALNK_IDX)alink=fn;else if(id==IRQ_SPI2_IDX)keyirq=fn;else CHECK(0);}
void unrequest_irq(unsigned id,unsigned cpu){CHECK(!cpu);if(id==IRQ_ALNK_IDX)alink=0;else if(id==IRQ_SPI2_IDX)keyirq=0;}
void bit_clr_ie(unsigned id,unsigned cpu){(void)id;CHECK(!cpu);}
int sys_usec_timer_add(void *u,void (*fn)(void *),uint32_t period,unsigned char unit,unsigned char cpu){CHECK(!u && period==1000 && unit==1 && !cpu);tick=fn;return 1;}
void sys_usec_timer_del(int id){CHECK(id==1);tick=0;timer_deleted++;}
int fm1_display_test_init(void){return 0;}
int fm1_display_test_frame(uint32_t n){(void)n;return 0;}
void fm1_display_test_stop(void){lcd_stopped++;}
int fm1_display_write(int data,const uint8_t *b,size_t n){CHECK(b && n && (data==0||data==1));return 0;}
int fm1_wl82_keyscan_async_start(fm1_wl82_keyscan *s,void *u,uint32_t (*clock)(void *)){CHECK(!u && clock);s->running=1;return 0;}
void fm1_wl82_keyscan_async_step(fm1_wl82_keyscan *s){(void)s;}
void fm1_wl82_keyscan_async_kick(fm1_wl82_keyscan *s){s->sequence++;}
int fm1_wl82_keyscan_async_raw(fm1_wl82_keyscan *s,uint8_t rows[11]){if(s->sequence==s->consumed)return FM1_NES_BUSY;s->consumed=s->sequence;memset(rows,0x3f,11);return 0;}
void fm1_wl82_keyscan_stop(fm1_wl82_keyscan *s){s->running=0;key_stopped++;}
uint32_t fm1_volume_test_read(uint32_t a){if(a==0x13100)return adc|0x80;if(a==0x13104)return 256;return 0;}
void fm1_volume_test_write(uint32_t a,uint32_t v){if(a==0x13100)adc=v&~0xc0u;}
static char answer[200];
static void reply(void *u,const char *s){(void)u;snprintf(answer,sizeof(answer),"%s",s);}
static void command(const char *s){CHECK(fm1_mdx_usb_command(s,ms,reply,0));CHECK(strncmp(answer,"ERR",3));}
void os_time_dly(int n){
    unsigned i;
    if(control.quiescent){CHECK(n==100);longjmp(done,1);}
    CHECK(n==1);
    for(i=0;i<(unsigned)n;i++) {
        ms++;if(tick)tick(0);if(keyirq)keyirq();if(alink)alink();
        if(ms==20)command("MDX MUTE 00 1");
        if(ms==22)command("MDX NOTE 3c 1");
        if(ms==35){CHECK(player.live_note[0]==60);CHECK(player.mute_mask&1);}
        if(ms==40)command("MDX STOP");
        if(ms==45){CHECK(!control.running && !control.request);command("MDX BEGIN 00000010 00000000");CHECK(upload.active);}
        if(ms==46){command("MDX ABORT");CHECK(!upload.active);command("MDX DEMO");}
        if(ms==80){CHECK(control.running && !player.error);CHECK(mdx_volume.valid);fm1_peripheral_session_cancel();CHECK(control.running);}
        if(ms==100)fm1_peripheral_cancel();
        CHECK(ms<200);
    }
}
int main(void){CHECK(!fm1_peripheral_start_task());CHECK(worker);if(!setjmp(done))worker(0);
    CHECK(opened==1 && closed==1 && key_stopped==1 && lcd_stopped==1 && timer_deleted==1);
    CHECK(!audio_enabled && !scan_enabled && !alink && !keyirq && control.quiescent && !control.running);
    CHECK(fm1_peripheral_idle());puts("PASS MDX task, stereo DMA, controls, stop/upload exclusion, disconnect continuity, complete UBOOT teardown");return 0;
}
