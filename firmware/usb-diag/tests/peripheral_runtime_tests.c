#define FM1_PERIPHERAL_HOST 1
#include "../peripherals.c"
#include <setjmp.h>
#include <stdlib.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}} while(0)
volatile unsigned fm1_cdc_generation;
volatile uint32_t fm1_display_stage;
volatile int fm1_display_error;
static unsigned time_ms,ready=1,opens,closes,irq_on,irq_off,channel_on,channel_off,scenario,task_fail;
static unsigned lcd_init,lcd_stop,key_start,key_stop,key_polls,iterations;
static unsigned timer_adds,timer_dels,sem_creates,sem_dels,wake_iterations,sub_ms,raw_calls;
#ifdef FM1_NES_VOLUME
static uint32_t volume_con;
uint32_t fm1_volume_test_read(uint32_t a){
    if(a==0x13100)return volume_con | ((volume_con&0x10)?0x80:0);
    if(a==0x13104)return 512;
    return 0;
}
void fm1_volume_test_write(uint32_t a,uint32_t v){if(a==0x13100)volume_con=v&~0xc0u;}
#endif
#ifdef FM1_KEYSCAN_IRQ
static void (*keyirq)(void);
static unsigned keyirq_on,keyirq_off,keyirq_steps,async_call;
static uint64_t async_keys;
#ifdef FM1_NES_LIVE_FX
static uint8_t fake_fx_rows[11];
static void fx_contact(unsigned id,unsigned ab) {
    unsigned a=fm1_encoder_contacts[2*id],b=fm1_encoder_contacts[2*id+1];
    fake_fx_rows[a>>4]=(fake_fx_rows[a>>4]|(1u<<(a&15)))&~((ab&1)<<(a&15));
    fake_fx_rows[b>>4]=(fake_fx_rows[b>>4]|(1u<<(b&15)))&~(((ab>>1)&1)<<(b&15));
}
static void fx_sweep(void){unsigned i;for(i=0;i<11;++i)keyirq();}
#endif
#endif
#ifdef FM1_NES_NO_KEYS
#define NES_SCANNER_CALLS 0
#define CHECK_SCANNER_ALLOWED() CHECK(control.mode!=7)
#else
#define NES_SCANNER_CALLS 1
#define CHECK_SCANNER_ALLOWED() ((void)0)
#endif
#ifdef FM1_NES_PLAYER
static unsigned game_runs,lcd_writes,lcd_command,lcd_top,lcd_bottom;
const uint8_t *fm1_rom50_data(size_t *size){static const uint8_t rom[16]={0};*size=sizeof(rom);return rom;}
int fm1_nes_validate_rom(const uint8_t *rom,size_t size){CHECK(rom && size==16);return scenario==14?-1:0;}
int fm1_stock_assign_note_keys(fm1_board_config *c){
    const uint8_t slots[]={21,22,23,24,25,14,15,16,17,18,19,20};
    return fm1_board_assign_note_keys(c,slots);
}
int fm1_display_write(int data,const uint8_t *p,size_t n){
    CHECK(p && n && (data==0 || data==1));lcd_writes++;
    if(!data){CHECK(n==1);lcd_command=p[0];}
    else if(lcd_command==0x2a){CHECK(n==4 && !p[0] && !p[1] && !p[2] && p[3]==239);}
    else if(lcd_command==0x2b){
        CHECK(n==4 && !p[0] && !p[2] && p[1]==p[3]);
        CHECK(p[1]==0 || p[1]==239);
        if(p[1]==0)lcd_top++;else lcd_bottom++;
    }
    return scenario==15?-2:0;
}
int fm1_nes_run(const uint8_t *rom,size_t size,const fm1_nes_platform *p,uint32_t limit,fm1_nes_stats *stats){
    uint16_t pixels[256]={0};uint8_t samples[64]={0};unsigned frame;
    CHECK(rom && size==16 && !limit && p && stats);game_runs++;
    for(frame=0;frame<6;frame++) {
        if(p->video(p->context,0,1,pixels))return FM1_NES_IO_ERROR;
#ifdef FM1_NES_CHANNEL_FX
        {const uint8_t *voices[]={samples,samples,samples,samples};
         CHECK(p->audio_channels);
         if(p->audio_channels(p->context,samples,voices,64))return FM1_NES_IO_ERROR;}
#else
        CHECK(!p->audio_channels);
        if(p->audio(p->context,samples,64))return FM1_NES_IO_ERROR;
#endif
        if(p->video(p->context,239,1,pixels))return FM1_NES_IO_ERROR;
#ifdef FM1_NES_NO_KEYS
        CHECK(p->buttons(p->context)==0);
#else
        p->buttons(p->context);
#endif
        if(p->frame(p->context,frame))break;
    }
    return 0;
}
#endif
static void (*wake)(void *);
static int worker_test;
static jmp_buf done;
static void (*handler)(void *,u8 *,int,u8);
static void (*irq)(void);
void arch_spin_lock(spinlock_t *s){CHECK(!*s);*s=1;}
void arch_spin_unlock(spinlock_t *s){CHECK(*s);*s=0;}
int task_create(void (*f)(void *),void *arg,const char *name){CHECK(f && !arg && !strcmp(name,"peripheral"));return task_fail?-1:0;}
uint32_t timer_get_ms(void){return time_ms;}
#ifdef FM1_NES_PROFILE
unsigned long jiffies_half_msec(void){return time_ms*2u;}
#endif
int clk_get(const char *name){return !strcmp(name,"sys")?480000000:40000000;}
void wdt_clear(void){}
int fm1_cdc_ready(unsigned char id){CHECK(id==0);return ready;}
int iis_open(struct iis_platform_data *pd,unsigned id){
    CHECK(!id && pd->port_sel==IIS_PORTC && pd->channel_out==8 && pd->data_width==8 && pd->mclk_output==1);
    CHECK(!pd->update_edge && !pd->f32e && pd->sr_points==128);opens++;return scenario==1?-1:0;
}
int iis_set_sample_rate(unsigned rate,unsigned id){CHECK(rate==44100 && !id);return scenario==2?-2:0;}
void iis_set_dec_data_handler(unsigned id,void (*fn)(void *,u8 *,int,u8),void *ctx){CHECK(!id && !ctx);handler=fn;}
void iis_channel_on(unsigned mask,unsigned id){CHECK(mask==8 && !id);channel_on++;}
void iis_channel_off(unsigned mask,unsigned id){CHECK(mask==8 && !id && !audio_enabled);channel_off++;}
void iis_close(unsigned id){CHECK(!id && !audio_enabled);closes++;}
void iis_irq_handler(unsigned id){int32_t pcm[128];CHECK(!id);handler(0,(u8 *)pcm,scenario==4?256:512,3);}
void request_irq(unsigned index,unsigned priority,void (*fn)(void),unsigned cpu){
#ifdef FM1_KEYSCAN_IRQ
    if(index==37){
        CHECK(priority==5 && !cpu && !keyirq && nes_input_irq_enabled && scanner.running);
        keyirq=fn;keyirq_on++;keyirq(); /* Pending first completion may dispatch immediately. */
        return;
    }
#endif
    CHECK(index==11 && priority==3 && !cpu);irq=fn;irq_on++;
}
void unrequest_irq(unsigned index,unsigned cpu){
#ifdef FM1_KEYSCAN_IRQ
    if(index==37){
        unsigned steps=keyirq_steps;
        CHECK(!cpu && keyirq && !nes_input_irq_enabled && !scanner.running);
        keyirq();CHECK(keyirq_steps==steps); /* An already-dispatched late IRQ is inert. */
        keyirq=0;keyirq_off++;return;
    }
#endif
    CHECK(index==11 && !cpu && !audio_enabled);irq=0;irq_off++;
}
void bit_clr_ie(unsigned index,unsigned cpu){CHECK((index==11 || index==37) && !cpu);}
int fm1_display_test_init(void){lcd_init++;fm1_display_stage=4;return 0;}
int fm1_display_test_frame(uint32_t elapsed){(void)elapsed;return 0;}
void fm1_display_test_stop(void){lcd_stop++;}
int fm1_wl82_keyscan_start(fm1_wl82_keyscan *s,void *ctx,uint32_t (*clock)(void *)){
    CHECK_SCANNER_ALLOWED();
#ifdef FM1_KEYSCAN_IRQ
    CHECK(control.mode!=7 || async_call);
#endif
    (void)ctx;(void)clock;s->running=1;s->failure=(fm1_wl82_keyscan_failure){0};
    s->trace=(fm1_wl82_keyscan_trace){0x21,0x20010,0x21,0x20010,1,0,0,0};key_start++;
    if(scenario==16){s->running=0;return -16;}return 0;
}
int fm1_wl82_keyscan_raw(fm1_wl82_keyscan *s,uint8_t rows[11]){CHECK_SCANNER_ALLOWED();CHECK(s->running);raw_calls++;memset(rows,0x3f,11);return scenario==7?-7:0;}
int fm1_wl82_keyscan_poll(fm1_wl82_keyscan *s,uint64_t *keys){
    CHECK_SCANNER_ALLOWED();
#ifdef FM1_KEYSCAN_IRQ
    CHECK(control.mode!=7 || async_call);
#endif
    CHECK(s->running && keys);key_polls++;*keys=scenario==17?UINT64_C(1)<<18:0;
    if(scenario==7){
        s->failure=(fm1_wl82_keyscan_failure){1,1,3,1,0xf7,0xfffffff0u,9984,10,0x21,29,0x20000,0,0x1e1,0x21,0x21,0x21};
        return -7;
    }
    return 0;
}
void fm1_wl82_keyscan_stop(fm1_wl82_keyscan *s){CHECK_SCANNER_ALLOWED();CHECK(s->running);s->running=0;key_stop++;}
uint64_t fm1_stock_decode_keys(const uint8_t rows[11]){
    (void)rows;
#ifdef FM1_KEYSCAN_IRQ
    CHECK(!input_lock);if(control.mode==7)return async_keys;
#endif
    return 0;
}
#ifdef FM1_KEYSCAN_IRQ
int fm1_wl82_keyscan_async_start(fm1_wl82_keyscan *s,void *ctx,uint32_t (*clock)(void *)){
    int rc;CHECK(input_lock && !keyirq && !nes_input_irq_enabled);
#ifdef FM1_KEYSCAN_PACED
    s->paused=0;s->completions=s->sequence=0;
#endif
    async_call=1;rc=fm1_wl82_keyscan_start(s,ctx,clock);async_call=0;return rc;
}
#ifdef FM1_KEYSCAN_PACED
static unsigned paced_kicks;
void fm1_wl82_keyscan_async_kick(fm1_wl82_keyscan *s){
    CHECK(input_lock);
    if(s->running && s->paused){s->paused=0;++paced_kicks;}
}
#endif
int fm1_wl82_keyscan_async_raw(fm1_wl82_keyscan *s,uint8_t rows[11]){
    int rc;CHECK(input_lock && keyirq && nes_input_irq_enabled);
    memset(rows,0x3f,11);async_call=1;rc=fm1_wl82_keyscan_poll(s,&async_keys);async_call=0;return rc;
}
void fm1_wl82_keyscan_async_step(fm1_wl82_keyscan *s){
    CHECK(input_lock && keyirq && s->running && nes_input_irq_enabled);
#ifdef FM1_KEYSCAN_PACED
    if(s->paused)return;
#endif
    keyirq_steps++;s->completions++;
#ifdef FM1_KEYSCAN_PACED
    if(s->completions%11u==0)s->paused=1;
#endif
#ifdef FM1_NES_LIVE_FX
    if(s->completions%11u==0){memcpy(s->ready_rows,fake_fx_rows,11);++s->sequence;}
#endif
}
#endif
int os_sem_create(OS_SEM *s,int count){CHECK(!s->created && !count);if(scenario==9)return -1;s->created=1;s->count=0;sem_creates++;return 0;}
int os_sem_post(OS_SEM *s){CHECK(s->created && !s->count);s->count++;return 0;}
int os_sem_del(OS_SEM *s,int force){CHECK(s->created && !force && !input_enabled && !wake);s->created=0;s->count=0;sem_dels++;return 0;}
int sys_usec_timer_add(void *ctx,void (*fn)(void *),uint32_t us,unsigned char in_irq,unsigned char once){
    CHECK(!ctx && fn && in_irq==1 && !once && !wake);timer_adds++;
#ifdef FM1_KEYSCAN_PACED
    CHECK(us==(control.mode==7?1000u:250u));
#else
    CHECK(us==250);
#endif
    if(scenario==10)return -1;wake=fn;return 42;
}
void sys_usec_timer_del(int id){
    CHECK(id==42 && !input_enabled && wake);
#ifdef FM1_KEYSCAN_PACED
    if(control.mode==7)CHECK(!nes_input_irq_enabled && !scanner.running);
#endif
    /* A callback already dispatched at deletion must not post a new token. */
    wake(0);wake=0;timer_dels++;
}
int os_sem_pend(OS_SEM *s,int timeout){
    CHECK(s->created && timeout==1 && !input_lock && wake);wake_iterations++;
    CHECK(wake_iterations<1500);
    if(scenario==11){time_ms+=10;return 1;}
    sub_ms+=250;time_ms+=sub_ms/1000;sub_ms%=1000;
    wake(0);if(scenario==12)wake(0); /* Coalesced, never accumulate a backlog. */
    if(wake_iterations==3) {
        if(scenario==5)ready=0;
        if(scenario==6)fm1_cdc_generation++;
        if(scenario==8 || scenario==12)fm1_peripheral_cancel();
    }
    if(scenario==13 && wake_iterations==1200)fm1_peripheral_cancel();
    CHECK(s->count==1);s->count--;return 0;
}
void os_time_dly(int ticks){
    unsigned i;time_ms+=(unsigned)ticks*10;iterations++;
    if(scenario==5 && iterations==3)ready=0;
    if(scenario==6 && iterations==3)fm1_cdc_generation++;
    if(scenario==8 && iterations==3)fm1_peripheral_cancel();
    if(irq && scenario!=3)for(i=0;i<7;i++)irq();
#ifdef FM1_KEYSCAN_IRQ
    if(keyirq)keyirq();
#endif
    if(worker_test && !control.busy)longjmp(done,1);
    CHECK(iterations<1000);
}
static void reset(unsigned test){
#ifdef FM1_NES_LIVE_FX
    memset(fake_fx_rows,0x3f,sizeof(fake_fx_rows));
#endif
#ifdef FM1_KEYSCAN_IRQ
    CHECK(!keyirq && !nes_input_irq_enabled && !nes_input_irq_registered && keyirq_on==keyirq_off);
    keyirq_on=keyirq_off=keyirq_steps=async_call=0;
#endif
    CHECK(!audio_enabled && !irq && !wake && !input_enabled && !input_sem.created);memset(&control,0,sizeof(control));
    timer_adds=timer_dels=sem_creates=sem_dels=wake_iterations=sub_ms=raw_calls=0;
    time_ms=opens=closes=irq_on=irq_off=channel_on=channel_off=iterations=0;
    lcd_init=lcd_stop=key_start=key_stop=key_polls=task_fail=0;
    ready=1;fm1_cdc_generation=1;scenario=test;worker_test=0;
    CHECK(!fm1_peripheral_start_task());
#ifdef FM1_NES_PLAYER
    CHECK(control.request==7 && control.busy && control.mode==7);
    control.request=control.busy=control.mode=0;game_runs=lcd_writes=0;
    lcd_command=lcd_top=lcd_bottom=0;
#endif
    CHECK(fm1_peripheral_idle());
}
static void worker(unsigned mode){
    worker_test=1;CHECK(!fm1_peripheral_request(mode,1));
    if(!setjmp(done))fm1_peripheral_task(0);
    worker_test=0;CHECK(fm1_peripheral_idle());
}
int main(void){
    unsigned i;char line[128];
    reset(0);CHECK(!opens && !lcd_init && !key_start);
    CHECK(!fm1_peripheral_request(2,1));CHECK(!fm1_peripheral_idle());
    CHECK(fm1_peripheral_request(3,1)<0);
    CHECK(!fm1_peripheral_request(1,1));CHECK(cancelled(control.request_epoch,1));
    /* A STOP before the worker begins must cancel the queued hardware action. */
    worker_test=1;if(!setjmp(done))fm1_peripheral_task(0);worker_test=0;CHECK(!lcd_init);
    reset(0);task_fail=1;CHECK(fm1_peripheral_start_task()<0);CHECK(fm1_peripheral_request(3,1)<0);
    reset(0);for(i=0;i<30;i++)event("x\n");CHECK(control.lost==14);
    for(i=0;i<16;i++)CHECK(fm1_peripheral_event(line,sizeof(line)));CHECK(!fm1_peripheral_event(line,sizeof(line)));
    reset(0);CHECK(audio_test(0,1)==0 && audio_frames==FM1_TONE_FRAMES && audio_irqs>0);
    CHECK(opens==1 && closes==1 && irq_on==1 && irq_off==1 && channel_on==1 && channel_off==1);
    reset(1);CHECK(audio_test(0,1)==-1 && opens==1 && !closes && !irq_on);
    reset(2);CHECK(audio_test(0,1)==-2 && closes==1 && !irq_on);
    reset(3);CHECK(audio_test(0,1)==-22 && closes==1 && time_ms==4000);
    reset(4);CHECK(audio_test(0,1)==-21 && closes==1);
    reset(5);CHECK(audio_test(0,1)==0 && closes==1 && audio_frames<FM1_TONE_FRAMES);
    reset(6);CHECK(audio_test(0,1)==0 && closes==1 && audio_frames<FM1_TONE_FRAMES);
    reset(8);CHECK(audio_test(0,1)==0 && closes==1 && audio_frames<FM1_TONE_FRAMES);
    reset(8);worker(2);CHECK(lcd_init==1 && lcd_stop==1);
    reset(7);worker(4);CHECK(key_start==1 && key_stop==1);
    reset(8);worker(5);CHECK(key_start==1 && key_stop==1);
    CHECK(timer_adds==1 && timer_dels==1 && sem_creates==1 && sem_dels==1 && raw_calls==2);
    reset(5);CHECK(input_test(5,0,1)==0 && key_stop==1 && timer_dels==1);
    reset(6);CHECK(input_test(5,0,1)==0 && key_stop==1 && timer_dels==1);
    reset(7);CHECK(input_test(5,0,1)==-7 && key_stop==1 && timer_dels==1);
    reset(9);CHECK(input_test(5,0,1)==-30 && key_stop==1 && !timer_adds && !sem_dels);
    reset(10);CHECK(input_test(5,0,1)==-31 && key_stop==1 && !timer_dels && sem_dels==1);
    reset(11);CHECK(input_test(5,0,1)==-32 && time_ms==10 && key_stop==1 && timer_dels==1);
    reset(12);CHECK(input_test(5,0,1)==0 && raw_calls==2 && input_coalesced==3 && timer_dels==1);
    reset(13);CHECK(input_test(5,0,1)==0 && time_ms==300 && raw_calls==1199 && !input_coalesced);
#ifdef FM1_KEYSCAN_PACED
    {unsigned kicks,steps,j;static const unsigned forward[]={2,3,1,0};
     reset(0);control.mode=7;memset(&nes_fx_encoders,0,sizeof(nes_fx_encoders));
     CHECK(!nes_scan_start() && wake && timer_adds==1);
     fx_sweep();CHECK(scanner.paused);steps=keyirq_steps;fx_sweep();CHECK(keyirq_steps==steps);
     wake(0);CHECK(!scanner.paused); /* Timer, not frame polling, restarts scan. */
     fx_sweep();wake(0); /* Stable contact priming. */
     for(j=0;j<4;j++){
         fx_contact(3,forward[j]);fx_sweep();wake(0);fx_sweep();
         steps=nes_fx_encoders.count[3];
         CHECK(scanner.paused); /* SPI ISR has not decoded the new snapshot. */
         wake(0);CHECK(nes_fx_encoders.count[3]==(int32_t)steps+1);
         steps=nes_fx_encoders.count[3];wake(0);CHECK(nes_fx_encoders.count[3]==(int32_t)steps);
     }
     CHECK(nes_fx_encoders.count[3]==4 && !nes_fx_encoders.invalid[3]);
     kicks=paced_kicks;nes_scan_stop();CHECK(timer_dels==1 && !wake && !nes_scan_timer);
     fm1_nes_scan_tick(0);CHECK(paced_kicks==kicks); /* Late callback inert. */
     control.mode=0;
     reset(10);worker(7);CHECK(game_runs==1 && !nes_fault && !nes_failure[0]);
     CHECK(timer_adds>=1 && !timer_dels && !wake && !nes_scan_timer && !scanner.running);
    }
#endif
#ifdef FM1_NES_PLAYER
#ifdef FM1_NES_CHANNEL_FX
    {int16_t expected[64];uint8_t mixed[64],a[64],z[64]={0};const uint8_t *voices[]={a,z,z,z};
     fm1_channel_fx reference;unsigned flags;
     reset(0);control.mode=7;nes_epoch=0;nes_session=1;nes_fault=audio_fault=0;
     nes_board.config.gain=4;fm1_channel_fx_reset(&nes_channel_fx);
     memset(nes_fx_previous,0,sizeof(nes_fx_previous));memset(&nes_fx_encoders,0,sizeof(nes_fx_encoders));
     flags=take(&input_lock);nes_fx_encoders.count[0]=-16;nes_fx_encoders.count[2]=-50;nes_fx_encoders.count[1]=4;release(&input_lock,flags);
     fm1_channel_fx_reset(&reference);fm1_channel_fx_edges(&reference,0,-16);
     fm1_channel_fx_edges(&reference,1,4);fm1_channel_fx_edges(&reference,2,-50);
     for(i=0;i<64;++i){a[i]=(uint8_t)(i%16);mixed[i]=(uint8_t)(247u*a[i]/128u);}
     CHECK(!fm1_channel_fx_process(&reference,mixed,voices,4,expected,64));
     fm1_audio_queue_reset(&nes_queue);CHECK(!nes_stems(0,mixed,voices,64));
     CHECK(nes_channel_fx.selected==0 && nes_channel_fx.controls[0].mode==1);
     CHECK(!memcmp(expected,nes_queue.mono,sizeof(expected)) && !memcmp(&reference,&nes_channel_fx,sizeof(reference)));
     /* Already-filtered samples enter the consumer queue once; retry does not
        advance any of the five LFOs a second time. */
     scenario=3;fm1_audio_queue_reset(&nes_queue);nes_queue.write_pos=FM1_AUDIO_CAPACITY;
     reference=nes_channel_fx;CHECK(!fm1_channel_fx_process(&reference,mixed,voices,4,expected,64));
     CHECK(nes_stems(0,mixed,voices,64)==FM1_NES_IO_ERROR);
     CHECK(!memcmp(&reference,&nes_channel_fx,sizeof(reference)) && time_ms==100);
     control.mode=0;}
#endif
#if defined(FM1_NES_LIVE_FX) && !defined(FM1_NES_CHANNEL_FX)
    {static const unsigned forward[]={2,3,1,0};unsigned j;int16_t samples[64]={0};
     reset(0);control.mode=7;nes_epoch=0;nes_session=1;
     memset(&nes_fx_encoders,0,sizeof(nes_fx_encoders));memset(nes_fx_previous,0,sizeof(nes_fx_previous));
     fm1_fx_controls_reset(&nes_fx_controls);fm1_fx_reset(&nes_fx);
     CHECK(!nes_scan_start());fx_sweep();
     /* Eight quadrature cycles of edges, with no task input/audio poll.
        Thus control changes during long LCD writes are not lost. */
     for(i=0;i<8;++i)for(j=0;j<4;++j){fx_contact(3,forward[j]);fx_sweep();fx_sweep();}
     CHECK(nes_fx_encoders.count[3]==32 && !nes_fx_encoders.invalid[3]);
     fm1_audio_queue_reset(&nes_queue);CHECK(!nes_pcm(0,samples,64));CHECK(nes_fx_controls.resonance==32);
     CHECK(!input_lock && !audio_lock);
     /* Invalid simultaneous A/B change cannot change a parameter. */
     fx_contact(3,3);fx_sweep();fx_sweep();CHECK(nes_fx_encoders.count[3]==32 && nes_fx_encoders.invalid[3]==1);
     nes_scan_stop();CHECK(!nes_scan_start());fx_sweep();fx_sweep();
     CHECK(nes_fx_encoders.count[3]==32); /* Recovery only re-primes. */
     for(j=0;j<4;++j){fx_contact(1,forward[j]);fx_sweep();fx_sweep();}
     {fm1_fx_state expected_state=nes_fx;fm1_fx_controls expected_controls=nes_fx_controls;int16_t expected[64];
      fm1_fx_control_edges(&expected_controls,1,4);
      for(j=0;j<64;++j)samples[j]=(int16_t)(j*20-640);
      CHECK(!fm1_fx_process(&expected_state,&expected_controls,samples,expected,64));
      fm1_audio_queue_reset(&nes_queue);CHECK(!nes_pcm(0,samples,64));
      CHECK(nes_fx_controls.mode==FM1_FX_LP && !memcmp(expected,nes_queue.mono,sizeof(expected)));
      CHECK(!memcmp(&expected_state,&nes_fx,sizeof(nes_fx)));}
     nes_scan_stop();control.mode=0;}
#endif
    /* Zero-delay yield must not consume a tick at each frame boundary. */
#ifdef FM1_NES_AUDIO_PRIORITY
    {int16_t input[FM1_AUDIO_PRIME];int32_t output[128];unsigned flags;
     reset(0);fm1_audio_queue_reset(&nes_queue);fm1_audio_startup_reset(&nes_envelope);
     nes_envelope.frames_left=0;nes_envelope.gain_q7=127;audio_nes=1;
#ifdef FM1_NES_VOLUME
     nes_volume.valid=1;nes_volume.target=127;
#endif
     flags=take(&audio_lock);audio_output(0,(u8 *)output,512,3);release(&audio_lock,flags);
     CHECK(nes_queue.priming_frames==64 && !nes_queue.underrun_frames);
     for(i=0;i<128;++i)CHECK(!output[i]);
     for(i=0;i<FM1_AUDIO_PRIME;++i)input[i]=(int16_t)(i-735);
     flags=take(&audio_lock);CHECK(!fm1_audio_queue_push(&nes_queue,input,FM1_AUDIO_PRIME));release(&audio_lock,flags);
     CHECK(nes_buffered(0)==FM1_AUDIO_PRIME && !audio_lock);
     flags=take(&audio_lock);audio_output(0,(u8 *)output,512,3);release(&audio_lock,flags);
     for(i=0;i<64;++i)CHECK(output[2*i]==input[i]*254 && output[2*i+1]==output[2*i]);
     CHECK(nes_buffered(0)==FM1_AUDIO_PRIME-64 && nes_queue.primed);
#ifdef FM1_NES_VOLUME
     nes_volume.target=64;
     flags=take(&audio_lock);audio_output(0,(u8 *)output,512,3);release(&audio_lock,flags);
     CHECK(nes_envelope.gain_q7==126);
     for(i=0;i<64;i++)CHECK(output[2*i]==input[64+i]*252 && output[2*i+1]==output[2*i]);
#endif
     audio_nes=0;}
#endif
#ifdef FM1_NES_VOLUME
    reset(0);control.mode=7;CHECK(!nes_scan_start());
    for(i=0;i<20;i++)fm1_nes_scan_tick(0);
    CHECK(nes_volume.raw==512 && nes_volume.samples==10 && nes_volume.target==64);
    nes_scan_stop();CHECK(!nes_volume.running && !nes_volume.valid && !nes_volume.target);
    control.mode=0;
#endif
    reset(0);control.mode=7;nes_epoch=0;nes_session=1;nes_fault=0;
    CHECK(!nes_stop(0) && time_ms==0 && iterations==1);
    fm1_peripheral_cancel();CHECK(nes_stop(0));control.mode=0;
    reset(0);control.mode=7;fm1_peripheral_session_cancel();CHECK(control.epoch==0);
    ready=0;fm1_cdc_generation++;CHECK(!cancelled(0,1));
    fm1_peripheral_cancel();CHECK(cancelled(0,1));control.mode=0;
    reset(0);fm1_peripheral_session_cancel();CHECK(cancelled(0,1));
    reset(8);worker(7);CHECK(game_runs==1 && lcd_writes>0 && key_stop==NES_SCANNER_CALLS && key_start==NES_SCANNER_CALLS && lcd_stop==1 && closes==1 && !audio_nes);
    CHECK(nes_board.config.gain==128 && nes_board.config.y_offset==0 && nes_board.config.key_for_pad[3]==15);
#if defined(FM1_NES_AUTO_SKIP) || defined(FM1_NES_AUDIO_PRIORITY)
    CHECK(nes_board.config.max_frame_skip==5);
#else
    CHECK(nes_board.config.max_frame_skip==0);
#endif
    CHECK(lcd_top>0 && lcd_top==lcd_bottom);
    reset(1);worker(7);CHECK(!game_runs && key_stop==NES_SCANNER_CALLS && lcd_stop==1 && !closes);
    CHECK(strstr(nes_failure,"source=IIS_OPEN rc=-1"));
    reset(2);worker(7);CHECK(!game_runs && closes==1 && !irq_on);
    CHECK(strstr(nes_failure,"source=IIS_RATE rc=-2"));
    reset(14);worker(7);CHECK(!game_runs && !lcd_init && !key_start && !opens);
    CHECK(strstr(nes_failure,"source=ROM rc=-1"));
    reset(15);worker(7);CHECK(game_runs==1 && closes==1 && key_stop==NES_SCANNER_CALLS && lcd_stop==1);
    CHECK(strstr(nes_failure,"source=LCD_WRITE rc=-2"));
#ifdef FM1_NES_NO_KEYS
    /* Even a scanner fault injection cannot stop the isolation run: scanner
       start/read/stop are forbidden by the stubs in NES mode, not just counted. */
    reset(7);worker(7);CHECK(game_runs==1 && nes_frames==6 && !nes_failure[0]);
    CHECK(!key_start && !key_stop && !raw_calls && lcd_writes>0 && audio_irqs>0);
    while(fm1_peripheral_event(line,sizeof(line))){}
    CHECK(!fm1_peripheral_request(6,1));
    CHECK(fm1_peripheral_event(line,sizeof(line)) && strstr(line,"TEST STATUS"));
    CHECK(fm1_peripheral_event(line,sizeof(line)) && strstr(line,"INPUT=NEUTRAL SCANNER=DISABLED"));
    CHECK(!fm1_peripheral_event(line,sizeof(line)));
#else
    reset(7);worker(7);
#ifdef FM1_NES_INPUT_RECOVERY
    CHECK(!nes_failure[0] && !nes_fault && nes_frames==6 && lcd_writes>0 && audio_irqs>0);
    CHECK(key_start>=1 && key_start<=4 && key_polls==key_start && key_stop==key_start);
    CHECK(strstr(nes_input_failure,"source=KEY_SCAN rc=-7"));
    CHECK(strstr(nes_input_status,"state=BACKOFF"));
#else
    CHECK(strstr(nes_failure,"source=KEY_SCAN rc=-7"));
#endif
    CHECK(strstr(nes_key_failure[0],"reason=1 row=3 phase=1 tx=f7 elapsed_us=10000 polls=10"));
    CHECK(strstr(nes_key_failure[1],"CON=00000021 BAUD_WRITE=29"));
    CHECK(strstr(nes_key_failure[2],"start_us=4294967280 end_us=9984 sys=480000000 lsb=40000000"));
    CHECK(strstr(nes_key_failure[3],"INIT_CON=00000021 INIT_MUX=00020010 SCAN_CON=00000021 SCAN_MUX=00020010 scans=1"));
    CHECK(strstr(nes_key_failure[4],"change_scan=0"));
    CHECK(strstr(nes_key_failure[5],"PRE_CON=00000021 CLEARED_CON=00000021 FIRST_CON=00000021"));
    for(i=0;i<NES_KEY_LINES;i++)CHECK(strlen(nes_key_failure[i])<127 && strchr(nes_key_failure[i],'\n'));
    scanner.failure.row=9;nes_key_fail("KEY_SCAN",-9);
    CHECK(strstr(nes_key_failure[0],"row=3"));
#ifdef FM1_NES_INPUT_RECOVERY
    CHECK(strstr(nes_input_failure,"rc=-7") && !nes_failure[0]);
#else
    CHECK(strstr(nes_failure,"rc=-7"));
#endif
    /* Status republishes the first failure even after event queue overflow. */
    while(fm1_peripheral_event(line,sizeof(line))){}
    for(i=0;i<25;i++)event("overflow\n");
    while(fm1_peripheral_event(line,sizeof(line))){}
    CHECK(!fm1_peripheral_request(6,1));
    CHECK(fm1_peripheral_event(line,sizeof(line)) && strstr(line,"TEST STATUS"));
#ifdef FM1_NES_INPUT_RECOVERY
    CHECK(fm1_peripheral_event(line,sizeof(line)) && strstr(line,"state=BACKOFF"));
#endif
    CHECK(fm1_peripheral_event(line,sizeof(line)) && strstr(line,"source=KEY_SCAN rc=-7"));
    for(i=0;i<NES_KEY_LINES;i++)CHECK(fm1_peripheral_event(line,sizeof(line)) && !strcmp(line,nes_key_failure[i]));
    CHECK(!fm1_peripheral_event(line,sizeof(line)));
#ifdef FM1_NES_INPUT_RECOVERY
    /* Three TOTAL retries, at least 50ms apart; no busy retry or fatal core
       error. Failed restart must not publish previous/partial key values. */
    control.mode=7;scenario=16;
    for(i=nes_input_retries;i<3;i++) {
        unsigned starts=key_start;
        time_ms=nes_input_retry_at+49;
        CHECK(!nes_keys(0) && key_start==starts);
        time_ms++;CHECK(!nes_keys(0) && key_start==starts+1);
        CHECK(!scanner.running && nes_input_retries==i+1 && !nes_fault);
    }
    CHECK(nes_input_errors==4 && strstr(nes_input_status,"state=OFFLINE"));
    {unsigned polls=key_polls;time_ms+=100000;
     CHECK(!nes_keys(0) && key_start==4 && key_polls==polls);}
    CHECK(strstr(nes_input_failure,"source=KEY_SCAN rc=-7") && strstr(nes_key_failure[0],"row=3"));
    control.mode=0;
    /* A startup scanner failure also leaves the emulator/video/audio alive. */
    reset(16);worker(7);
    CHECK(game_runs==1 && nes_frames==6 && lcd_writes>0 && audio_irqs>0 && !nes_fault && !nes_failure[0]);
    CHECK(strstr(nes_input_failure,"source=KEY_START rc=-16"));
    CHECK(!scanner.running && key_start>=1 && key_start<=4);
    /* A continuously held button survives both ordinary frames and a short
       fault/recovery, including timestamp wrap. No release-to-rearm gate. */
    reset(0);worker(7);control.mode=7;
    CHECK(!nes_input_failure[0] && !nes_input_errors && !nes_input_retries);
    CHECK(!nes_scan_start());
    time_ms=UINT32_MAX-30;scenario=17;
    CHECK(!nes_board.platform.buttons(&nes_board));
    time_ms+=10;CHECK(nes_board.platform.buttons(&nes_board)==FM1_PAD_A);
    time_ms+=10;CHECK(nes_board.platform.buttons(&nes_board)==FM1_PAD_A);
    scenario=7;CHECK(nes_board.platform.buttons(&nes_board)==FM1_PAD_A);
    CHECK(nes_board.stable==FM1_PAD_A && !nes_fault);
    scenario=17;time_ms=nes_input_retry_at+49;
    CHECK(nes_board.platform.buttons(&nes_board)==FM1_PAD_A && !scanner.running);
    time_ms++;CHECK(nes_board.platform.buttons(&nes_board)==FM1_PAD_A && scanner.running && nes_input_retries==1);
    CHECK(strstr(nes_input_status,"state=RECOVERED"));
    time_ms+=1000;CHECK(nes_board.platform.buttons(&nes_board)==FM1_PAD_A);
    /* Actual release still follows normal 10ms board debounce. */
    scenario=0;CHECK(nes_board.platform.buttons(&nes_board)==FM1_PAD_A);
    time_ms+=10;CHECK(!nes_board.platform.buttons(&nes_board));
    scenario=17;CHECK(!nes_board.platform.buttons(&nes_board));
    time_ms+=10;CHECK(nes_board.platform.buttons(&nes_board)==FM1_PAD_A);
    scenario=7;CHECK(nes_board.platform.buttons(&nes_board)==FM1_PAD_A);
    CHECK(nes_input_retries==1 && !nes_fault); /* success did not reset budget */
    /* No complete good sample for 100ms releases stale buttons even if the
       scanner cannot restart. Never keep an unobservable note/direction forever. */
    scenario=16;time_ms=nes_input_good_at+99;
    CHECK(nes_board.platform.buttons(&nes_board)==FM1_PAD_A);
    time_ms++;CHECK(!nes_board.platform.buttons(&nes_board) && !nes_board.stable);
    /* STOP in backoff prevents a pending retry from touching hardware. */
    {unsigned starts=key_start;time_ms+=50;fm1_peripheral_cancel();
     CHECK(!nes_keys(0) && key_start==starts);}
    control.mode=0;
#endif
#endif
    reset(3);control.mode=7;nes_epoch=0;nes_session=1;nes_failure[0]=0;audio_fault=0;
    fm1_audio_queue_reset(&nes_queue);nes_queue.write_pos=FM1_AUDIO_CAPACITY;
#if defined(FM1_NES_LIVE_FX) && !defined(FM1_NES_CHANNEL_FX)
    fm1_fx_reset(&nes_fx); /* Retry loop must not advance DSP/LFO repeatedly. */
#endif
    {int16_t sample=1;CHECK(nes_pcm(0,&sample,1)==FM1_NES_IO_ERROR);}
#if defined(FM1_NES_LIVE_FX) && !defined(FM1_NES_CHANNEL_FX)
    CHECK(nes_fx.phase==nes_fx.phase_increment);
#endif
    CHECK(time_ms==100 && strstr(nes_failure,"source=PCM_TIMEOUT") && strstr(nes_failure,"q=4096"));
    nes_fail("CORE",-3);CHECK(strstr(nes_failure,"source=PCM_TIMEOUT"));
    control.mode=0;reset(8);worker(7);CHECK(!nes_failure[0]); /* New run clears it. */
    for(i=0;i<NES_KEY_LINES;i++)CHECK(!nes_key_failure[i][0]);
#endif
    puts("PASS peripheral idle/dispatch/STOP, audio geometry/timeout/teardown, disconnect, task failure, event overflow");return 0;
}
