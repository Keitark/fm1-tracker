/* Actual target.c with mocked peripherals, never device IO. */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define fm1_forge_render test_diagnostic_render
#include "../target.c"
#undef fm1_forge_render
int fm1_forge_render(const fm1_forge_frame *,unsigned,unsigned,uint8_t *,unsigned);
#define CHECK(v) do{if(!(v)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#v);exit(1);}}while(0)
static jmp_buf end;
static task_fn task;
static irq_fn irqs[40];
static void (*timer_cb)(void *);
static void (*dac_cb)(void *,u8 *,int,u8);
static unsigned ms,turn,order,closes,lcd_stops,uac_stops,clock_writes,wrong_readback,submitted,completed,scan_starts,injected;
static int current_clock=480000000;
static char replies[4096];
static unsigned diagnostic_renders,usb_audio_blocks;
static fm1_forge_frame diagnostic_frame;
#ifdef FM1_FORGE_USER_MODULE
extern unsigned test_module_inits,test_module_updates,test_module_renders,test_module_audio,test_module_stops;
extern fm1_forge_input test_module_update_input;
extern fm1_forge_frame test_module_render_frame;
#endif
int test_diagnostic_render(const fm1_forge_frame *frame,unsigned y,unsigned rows,uint8_t *wire,unsigned bpp){diagnostic_renders++;diagnostic_frame=*frame;return fm1_forge_render(frame,y,rows,wire,bpp);}
static void check_user_input(const fm1_forge_input *input){unsigned i;
#if defined(FM1_FORGE_USER_MODULE) && !FM1_FORGE_HAL_BUTTONS
    CHECK(!input->keys && !input->seen && input->last_key==255);
#else
    CHECK(input->keys==(UINT64_C(1)<<14) && input->seen==(UINT64_C(1)<<14) && input->last_key==14);
#endif
#if defined(FM1_FORGE_USER_MODULE) && !FM1_FORGE_HAL_ENCODERS
    CHECK(!input->encoder_seen);for(i=0;i<7;i++)CHECK(!input->encoder[i] && !input->encoder_invalid[i]);
#else
    (void)i;CHECK(input->encoder[6]==17 && input->encoder_invalid[6]>=3 && (input->encoder_seen&64));
#endif
#if defined(FM1_FORGE_USER_MODULE) && !FM1_FORGE_HAL_VOLUME
    CHECK(!input->volume_valid && !input->volume_gain && !input->volume_raw && !input->volume_min && !input->volume_max && !input->volume_samples && !input->volume_errors);
#else
    CHECK(input->volume_valid && input->volume_raw==500 && input->volume_gain==62 && input->volume_samples>0);
#endif
    CHECK(input->scans>0);
}
volatile uint32_t fm1_display_stage;
volatile int fm1_display_error;
static void reply(void *u,const char *line){(void)u;CHECK(strlen(replies)+strlen(line)<sizeof(replies));strcat(replies,line);}
void arch_spin_lock(spinlock_t *l){CHECK(!l->held);l->held=1;}
void arch_spin_unlock(spinlock_t *l){CHECK(l->held);l->held=0;}
int clk_get(const char *name){if(!strcmp(name,"sys"))return current_clock;if(!strcmp(name,"lsb"))return current_clock==320000000?53000000:60000000;if(!strcmp(name,"hsb"))return 160000000;if(!strcmp(name,"sfc"))return 80000000;return -1;}
int system_clock_set(uint32_t hz){CHECK(hz==320000000u && !audio_enabled && !timer_cb && !dac_cb);clock_writes++;if(current_clock==(int)hz)return -22;if(!wrong_readback)current_clock=(int)hz;return 0;}
uint32_t timer_get_ms(void){return ms;}
int sys_usec_timer_add(void *u,void (*cb)(void *),unsigned period,unsigned once,unsigned cpu){(void)u;CHECK(order==3 && period==1000 && once==1 && !cpu);order=4;timer_cb=cb;return 1;}
void sys_usec_timer_del(int id){CHECK(id==1);timer_cb=0;}
int task_create(task_fn cb,void *u,const char *name){(void)u;CHECK(!strcmp(name,"#C0peripheral"));task=cb;return 0;}
void request_irq(unsigned id,unsigned priority,irq_fn cb,unsigned cpu){CHECK(id<40 && !cpu && !irqs[id]);if(id==11){CHECK(priority==3 && order==4);order=5;}else CHECK(id==37 && priority==5);irqs[id]=cb;}
void unrequest_irq(unsigned id,unsigned cpu){CHECK(!cpu);irqs[id]=0;}
void bit_clr_ie(unsigned id,unsigned cpu){CHECK(id<40 && !cpu);/* masking preserves registered handler */}
void wdt_clear(void){}
int iis_open(struct iis_platform_data *p,unsigned id){CHECK(!id && !order && p->port_sel==IIS_PORTC && p->channel_out==8 && p->data_width==8 && p->sr_points==128);order=1;return 0;}
void iis_close(unsigned id){CHECK(!id);closes++;dac_cb=0;}
void iis_set_dec_data_handler(unsigned id,void (*cb)(void *,u8 *,int,u8),void *u){CHECK(!id && !u && order==1);dac_cb=cb;}
int iis_set_sample_rate(unsigned rate,unsigned id){CHECK(rate==44100 && !id && dac_cb && order==1);order=2;return 0;}
void iis_channel_on(unsigned channel,unsigned id){CHECK(channel==8 && !id && order==5);order=6;}
void iis_channel_off(unsigned channel,unsigned id){CHECK(channel==8 && !id);}
void iis_irq_handler(unsigned id){int32_t pcm[128];unsigned i;CHECK(!id && dac_cb);dac_cb(0,(u8 *)pcm,512,3);for(i=0;i<128;i++)CHECK(pcm[i]>=-8388608 && pcm[i]<=8388607);}
int fm1_volume_start(fm1_volume *v){CHECK(order==2);order=3;memset(v,0,sizeof(*v));v->running=1;return 0;}
void fm1_volume_stop(fm1_volume *v){v->running=v->valid=0;}
void fm1_volume_tick(fm1_volume *v){if(v->running){v->samples++;v->valid=1;v->raw=500;v->target=62;}}
int fm1_wl82_keyscan_async_start(fm1_wl82_keyscan *s,void *u,uint32_t (*clock)(void *)){CHECK(order==7 && !u && clock);s->running=1;scan_starts++;return 0;}
void fm1_wl82_keyscan_stop(fm1_wl82_keyscan *s){s->running=0;}
void fm1_wl82_keyscan_async_step(fm1_wl82_keyscan *s){s->sequence++;encoders.count[6]=17;encoders.invalid[6]=3;}
void fm1_wl82_keyscan_async_kick(fm1_wl82_keyscan *s){(void)s;}
int fm1_wl82_keyscan_async_raw(fm1_wl82_keyscan *s,uint8_t rows[11]){CHECK(s->running);if(turn==10 && !injected){injected=1;s->running=0;return -7;}memset(rows,0x3f,11);rows[4]&=0x2f;return 0;}
int fm1_display_test_init(void){CHECK(order==6 && audio_enabled && timer_cb);order=7;return 0;}
void fm1_display_test_stop(void){lcd_stops++;}
int fm1_display_async_start(void){return 0;}
int fm1_display_async_service(void){return 0;}
int fm1_display_async_begin(int wanted){CHECK(wanted);submitted++;return 1;}
int fm1_display_async_wire_rows(unsigned y,unsigned rows,const uint8_t *p,size_t bytes){CHECK(y<240 && rows==8 && p && bytes==FM1_LCD_WIRE_STRIP_BYTES);if(y==232)completed++;return 0;}
void fm1_display_async_snapshot(fm1_lcd_async_stats *s){memset(s,0,sizeof(*s));s->submitted=submitted;s->completed=completed;}
#ifdef FM1_USB_AUDIO
void fm1_usb_audio_stop(void){uac_stops++;}
void fm1_usb_audio_dac(int32_t *p,unsigned frames){CHECK(p && frames==64 && audio_enabled);
#if defined(FM1_FORGE_USER_MODULE) && FM1_FORGE_HAL_AUDIO
    CHECK(p[0]==8388607 && p[1]==-8388608);
#elif defined(FM1_FORGE_USER_MODULE)
    {unsigned i;for(i=0;i<128;i++)CHECK(!p[i]);}
#endif
    usb_audio_blocks++;
}
void fm1_usb_audio_status(char *out,size_t n){snprintf(out,n,"USB AUDIO rate=48000\n");}
void fm1_usb_audio_transport_status(char *out,size_t n){snprintf(out,n,"USB TRANSPORT submitted=1\n");}
#endif
void os_time_dly(int ticks){
    if(quiescent){CHECK(!running && !pending && !scan_enabled && !audio_enabled && !timer_cb && !dac_cb && !scanner.running && !volume.running);CHECK(closes==1 && lcd_stops==1);longjmp(end,1);}
    CHECK(ticks==1);ms+=10;turn++;
    if(timer_cb)timer_cb(0);if(irqs[37] && scan_enabled)irqs[37]();if(irqs[11] && audio_enabled)irqs[11]();
    if(turn==8){fm1_forge_input input;fm1_forge_hal_snapshot(&input);check_user_input(&input);CHECK(volume.valid && volume.target==62 && envelope.target_q7==62);
        fm1_peripheral_session_cancel();CHECK(running && !pending && audio_enabled);
        {unsigned writes=clock_writes;CHECK(fm1_forge_clock_prepare()==-83);CHECK(clock_writes==writes);}
        CHECK(fm1_forge_usb_command("FORGE STATUS",ms,reply,0));CHECK(strstr(replies,"ready=1 running=1 pending=0"));CHECK(!pending);
        CHECK(fm1_forge_usb_command("FORGE INPUT",ms,reply,0));CHECK(strstr(replies,"FORGE KNOB id=6"));}
    if(turn==45){CHECK(completed>0 && audio_frames>0 && controls.input.scans>0);CHECK(!published.fault);CHECK(scan_starts==2 && controls.input.input_errors==1 && controls.input.seen==(UINT64_C(1)<<14));
        {fm1_forge_input input;fm1_forge_hal_snapshot(&input);check_user_input(&input);}
#ifdef FM1_FORGE_USER_MODULE
        check_user_input(&test_module_update_input);
#if FM1_FORGE_HAL_LCD
        CHECK(test_module_renders>0 && !diagnostic_renders);check_user_input(&test_module_render_frame.input);
#else
        CHECK(!test_module_renders && diagnostic_renders>0);check_user_input(&diagnostic_frame.input);
#endif
#else
        CHECK(diagnostic_renders>0);check_user_input(&diagnostic_frame.input);
#endif
        CHECK(fm1_forge_usb_command("FORGE STOP",ms,reply,0));CHECK(pending);}
    CHECK(turn<48);
}
int main(void){
    CHECK(!fm1_forge_clock_prepare());
#if FM1_FORGE_BOOT_CLOCK_HZ
    CHECK(clock_writes==1 && sys_hz==320000000);CHECK(!fm1_forge_clock_prepare() && clock_writes==1);clock_prepared=0;wrong_readback=1;current_clock=240000000;CHECK(fm1_forge_clock_prepare()==-81);CHECK(clock_writes==2);wrong_readback=0;clock_error=0;current_clock=320000000;
    clock_prepared=0;CHECK(!fm1_forge_clock_prepare() && sys_hz==320000000 && clock_writes==2);
#else
    CHECK(!clock_writes && sys_hz==480000000);
#endif
    CHECK(!fm1_peripheral_start_task() && task && !fm1_peripheral_idle());
    if(!setjmp(end))task(0);CHECK(fm1_peripheral_idle());
    CHECK(fm1_forge_usb_command("FORGE STATUS",ms,reply,0));CHECK(strstr(replies,"running=0 pending=0"));
#ifdef FM1_USB_AUDIO
    CHECK(uac_stops==1 && usb_audio_blocks>0);
#endif
#ifdef FM1_FORGE_USER_MODULE
    CHECK(test_module_inits==1 && test_module_updates>0 && test_module_stops==1);
#if FM1_FORGE_HAL_AUDIO
    CHECK(test_module_audio>0);
#else
    CHECK(!test_module_audio);
#endif
#endif
    puts("Actual Forge target lifecycle,read-only CDC,persistent diagnostics,clock guards and STOP quiesce passed");return 0;
}
