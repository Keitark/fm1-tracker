/* Independent, opt-in peripheral worker. Never owns CDC or writes flash. */
#ifdef FM1_PERIPHERAL_HOST
#include "fake_peripherals.h"
#else
#include "app_config.h"
#include "system/includes.h"
#include "system/task.h"
#include "system/sys_time.h"
#include "system/timer.h"
#include "system/spinlock.h"
#include "os/os_api.h"
#include "asm/clock.h"
#include "asm/wdt.h"
#include "asm/iis.h"
#endif
#include "peripherals.h"
#include "peripheral_logic.h"
#include "display_test.h"
#include "clock_contract.h"
#include "fm1_wl82_keyscan.h"
#include <stdio.h>
#include <string.h>
#ifdef FM1_NES_PLAYER
#include "fm1_rom50.h"
#include "fm1_board.h"
#include "fm1_profile.h"
#ifdef FM1_NES_PROFILE
/* SDK function already in the pinned image;500us units. now_us is10ms here. */
extern unsigned long jiffies_half_msec(void);
static uint32_t nes_profile_clock(void){return (uint32_t)jiffies_half_msec();}
#endif
static fm1_board nes_board;
static fm1_audio_queue nes_queue;
static fm1_audio_startup nes_envelope;
#ifdef FM1_NES_VOLUME
#if !defined(FM1_KEYSCAN_PACED) || !defined(FM1_NES_AUDIO_PRIORITY)
#error Volume requires paced scanner and audio priority
#endif
#include "fm1_volume.h"
static fm1_volume nes_volume; /* input_lock owned, including2ms tick. */
#endif
static unsigned audio_nes,nes_epoch,nes_session;
static uint32_t nes_frames,nes_last_log;
static int nes_fault;
#ifdef FM1_NES_LIVE_FX
#if !defined(FM1_KEYSCAN_IRQ) || !defined(FM1_NES_AUDIO_PRIORITY)
#error Live effects require IRQ controls and audio-priority scheduling
#endif
#include "fm1_nes_fx.h"
static fm1_encoders nes_fx_encoders; /* Input-lock/scan-IRQ owned. */
static int32_t nes_fx_previous[7]; /* Task-owned edge snapshot. */
#ifdef FM1_NES_CHANNEL_FX
#include "fm1_channel_fx.h"
static fm1_channel_fx nes_channel_fx;
#else
static fm1_fx_controls nes_fx_controls;
static fm1_fx_state nes_fx;
#endif
#endif
/* First failure survives teardown and a disconnected/full serial event queue.
   Cleared only when a new NES run starts; TEST STATUS republishes it. */
static char nes_failure[128];
#define NES_KEY_LINES 6
static char nes_key_failure[NES_KEY_LINES][128];
#ifdef FM1_NES_INPUT_RECOVERY
#if defined(FM1_NES_NO_KEYS)
#error Input recovery requires physical keys
#endif
/* Worker-owned state; only the formatted snapshots cross to the CDC task. */
static unsigned nes_input_retries,nes_input_errors,nes_input_valid;
static uint32_t nes_input_retry_at,nes_input_good_at;
static uint64_t nes_input_last_keys;
static char nes_input_status[128],nes_input_failure[128];
#define NES_INPUT_RETRIES 3u
#define NES_INPUT_BACKOFF_MS 50u
#define NES_INPUT_STALE_MS 100u
#endif
#endif
extern volatile unsigned fm1_cdc_generation;
extern int fm1_cdc_ready(unsigned char);
static spinlock_t control_lock, audio_lock;
static struct {
    unsigned request,busy,mode,epoch,request_epoch,session,rd,wr,lost,available;
    char events[16][128];
} control;
static unsigned audio_enabled,audio_session;
static volatile uint32_t audio_frames,audio_irqs,audio_fault;
static fm1_wl82_keyscan scanner;
/* Timer IRQ only posts a coalesced wakeup. SPI polling/decoding stays in the
   worker; no SPI busy wait, printf, or control-lock access in the callback. */
static spinlock_t input_lock;
static OS_SEM input_sem;
static unsigned input_enabled,input_pending,input_wakes,input_coalesced;
#define FM1_ENCODER_SCAN_US 250u
static unsigned take(spinlock_t *lock) {
    unsigned flags;local_irq_save(flags);arch_spin_lock(lock);return flags;
}
static void release(spinlock_t *lock,unsigned flags) {arch_spin_unlock(lock);local_irq_restore(flags);}
__attribute__((noinline,used))
static void fm1_encoder_wakeup(void *unused) {
    unsigned flags=take(&input_lock);(void)unused;
    if(input_enabled) {
        input_wakes++;
        if(input_pending)input_coalesced++;
        else {input_pending=1;os_sem_post(&input_sem);}
    }
    release(&input_lock,flags);
}
static void event(const char *line) {
    unsigned flags=take(&control_lock);
    if(control.wr-control.rd==16)control.lost++;
    else {snprintf(control.events[control.wr%16],128,"%s",line);control.wr++;}
    release(&control_lock,flags);
}
#ifdef FM1_LCD_STOCK_DMA
__attribute__((noinline,used))
void fm1_display_snapshot(unsigned phase,const uint32_t r[FM1_LCD_REGISTER_COUNT]) {
    char s[128];
    snprintf(s,sizeof(s),"LCDREG phase=%u OUT=%08lx IN=%08lx DIR=%08lx DIE=%08lx\n",phase,(unsigned long)r[0],(unsigned long)r[1],(unsigned long)r[2],(unsigned long)r[3]);event(s);
    snprintf(s,sizeof(s),"LCDREG phase=%u PU=%08lx PD=%08lx HD0=%08lx HD=%08lx\n",phase,(unsigned long)r[4],(unsigned long)r[5],(unsigned long)r[6],(unsigned long)r[7]);event(s);
    snprintf(s,sizeof(s),"LCDREG phase=%u DIEH=%08lx MUX0=%08lx MUX1=%08lx MUX2=%08lx\n",phase,(unsigned long)r[8],(unsigned long)r[9],(unsigned long)r[10],(unsigned long)r[11]);event(s);
    snprintf(s,sizeof(s),"LCDREG phase=%u MUX3=%08lx MUX5=%08lx SPI=%08lx BAUD=%08lx\n",phase,(unsigned long)r[12],(unsigned long)r[13],(unsigned long)r[14],(unsigned long)r[15]);event(s);
    snprintf(s,sizeof(s),"LCDREG phase=%u DMA_ADR=%08lx DMA_CNT=%08lx error=%d\n",phase,(unsigned long)r[16],(unsigned long)r[17],fm1_display_error);event(s);
#ifdef FM1_LCD_STOCK_SEQUENCE
    snprintf(s,sizeof(s),"LCDREG phase=%u PA_OUT=%08lx PA_DIR=%08lx\n",phase,(unsigned long)r[18],(unsigned long)r[19]);event(s);
#endif
}
#endif
int fm1_peripheral_event(char *out,unsigned size) {
    int result=0;unsigned flags=take(&control_lock);
    if(control.rd!=control.wr) {snprintf(out,size,"%s",control.events[control.rd++%16]);result=1;}
    release(&control_lock,flags);return result;
}
void fm1_peripheral_cancel(void) {
    unsigned flags=take(&control_lock);control.epoch++;release(&control_lock,flags);
}
void fm1_peripheral_session_cancel(void) {
    unsigned flags=take(&control_lock);
#ifdef FM1_NES_PLAYER
    /* Standalone game continues without a terminal. Explicit STOP/UBOOT still
       uses unconditional cancel; diagnostics retain disconnect cancellation. */
    if(control.mode!=7)
#endif
        control.epoch++;
    release(&control_lock,flags);
}
int fm1_peripheral_idle(void) {
    unsigned flags=take(&control_lock);int idle=!control.busy;release(&control_lock,flags);return idle;
}
int fm1_peripheral_request(unsigned command,unsigned generation) {
    unsigned flags=take(&control_lock);int result=0;
    if(command==1)control.epoch++;
    else if(command==6) {
        char text[128];
#ifdef FM1_NES_PLAYER
        char failure[128],key_failure[NES_KEY_LINES][128];unsigned i;
        memcpy(failure,nes_failure,sizeof(failure));
        memcpy(key_failure,nes_key_failure,sizeof(key_failure));
#ifdef FM1_NES_INPUT_RECOVERY
        char input_status[128],input_failure[128];
        memcpy(input_status,nes_input_status,sizeof(input_status));
        memcpy(input_failure,nes_input_failure,sizeof(input_failure));
#endif
#endif
        snprintf(text,sizeof(text),"TEST STATUS ready=%u busy=%u mode=%u dropped=%u frames=%lu irqs=%lu fault=%lu\n",
            control.available,control.busy,control.mode,control.lost,(unsigned long)audio_frames,(unsigned long)audio_irqs,(unsigned long)audio_fault);
        release(&control_lock,flags);event(text);
#ifdef FM1_NES_NO_KEYS
        event("NES INPUT=NEUTRAL SCANNER=DISABLED\n");
#endif
#ifdef FM1_NES_PLAYER
        if(failure[0])event(failure);
#ifdef FM1_NES_INPUT_RECOVERY
        if(input_status[0])event(input_status);
        if(input_failure[0])event(input_failure);
#endif
        for(i=0;i<NES_KEY_LINES;i++)if(key_failure[i][0])event(key_failure[i]);
#endif
        return 0;
    } else if(command<2 ||
#ifdef FM1_NES_PLAYER
              command>7 ||
#else
              command>5 ||
#endif
              control.busy || !control.available)result=-1;
    else {control.request=command;control.mode=command;control.busy=1;control.session=generation;control.request_epoch=control.epoch;}
    release(&control_lock,flags);return result;
}
static int cancelled(unsigned epoch,unsigned session) {
    unsigned flags=take(&control_lock);int stop=epoch!=control.epoch;
#ifdef FM1_NES_PLAYER
    if(control.mode==7){release(&control_lock,flags);return stop;}
#endif
    release(&control_lock,flags);
    return stop || session!=fm1_cdc_generation || !fm1_cdc_ready(FM1_USB_CONTROLLER);
}
static uint32_t now_us(void *unused) {(void)unused;return timer_get_ms()*1000u;}
static void audio_output(void *ctx,u8 *data,int len,u8 channel) {
    unsigned i;int32_t *pcm=(int32_t *)data;(void)ctx;
    if(!data || len<=0)return;
    if(channel!=3 || len!=512){memset(data,0,(unsigned)len);audio_fault=1;return;}
#ifdef FM1_NES_PLAYER
    if(audio_nes) {
#ifdef FM1_NES_VOLUME
        /* Lock order audio->input only. Timer never takes audio_lock. */
        unsigned flags=take(&input_lock);
        nes_envelope.target_q7=nes_volume.valid?nes_volume.target:0;
        release(&input_lock,flags);
#endif
#ifdef FM1_NES_AUDIO_PRIORITY
        fm1_audio_queue_play24(&nes_queue,&nes_envelope,pcm);
#else
        fm1_audio_queue_stereo24(&nes_queue,pcm,64);
        fm1_audio_startup_process24(&nes_envelope,pcm);
#endif
        audio_frames+=64;return;
    }
#endif
    if(audio_session!=fm1_cdc_generation || !fm1_cdc_ready(FM1_USB_CONTROLLER)){memset(data,0,(unsigned)len);return;}
    for(i=0;i<64;i++) {
        int32_t value=fm1_test_sample(audio_frames);
        if(audio_frames<FM1_TONE_FRAMES)audio_frames++;
        pcm[2*i]=pcm[2*i+1]=value;
    }
}
___interrupt
static void fm1_test_alink_isr(void) {
    unsigned flags=take(&audio_lock);
    if(audio_enabled){iis_irq_handler(0);audio_irqs++;}
    release(&audio_lock,flags);
}
static int audio_test(unsigned epoch,unsigned session) {
    struct iis_platform_data pd;int rc;unsigned flags;uint32_t start;
    memset(&pd,0,sizeof(pd));pd.port_sel=IIS_PORTC;
    pd.channel_out=pd.data_width=8;pd.mclk_output=1;pd.update_edge=0;pd.f32e=0;pd.sr_points=128;
    audio_frames=audio_irqs=audio_fault=0;audio_session=session;
    rc=iis_open(&pd,0);if(rc)return rc;
    iis_set_dec_data_handler(0,audio_output,0);
    rc=iis_set_sample_rate(44100,0);if(rc){iis_close(0);return rc;}
    flags=take(&audio_lock);audio_enabled=1;release(&audio_lock,flags);
    request_irq(IRQ_ALNK_IDX,3,fm1_test_alink_isr,0);
    iis_channel_on(8,0);start=timer_get_ms();
    while(!cancelled(epoch,session) && !audio_fault && audio_frames<FM1_TONE_FRAMES && (uint32_t)(timer_get_ms()-start)<4000) {
        wdt_clear();os_time_dly(1);
    }
    rc=audio_fault ? -21 : (!cancelled(epoch,session) && audio_frames<FM1_TONE_FRAMES ? -22 : 0);
    bit_clr_ie(IRQ_ALNK_IDX,0);
    flags=take(&audio_lock);audio_enabled=0;release(&audio_lock,flags);
    unrequest_irq(IRQ_ALNK_IDX,0);iis_channel_off(8,0);iis_close(0);
    return rc;
}
static int input_test(unsigned mode,unsigned epoch,unsigned session) {
    fm1_encoders encoders={0};uint8_t rows[11];uint64_t stable=0,candidate=0;
    uint32_t changed=timer_get_ms(),last=changed,previous_scan=changed,max_gap=0,scans=0;
    unsigned i,flags;int timer_id=0;char line[128];
    int rc=fm1_wl82_keyscan_start(&scanner,0,now_us);if(rc)return rc;
    if(mode==5) {
        if(os_sem_create(&input_sem,0)){fm1_wl82_keyscan_stop(&scanner);return -30;}
        flags=take(&input_lock);
        input_pending=input_wakes=input_coalesced=0;input_enabled=1;
        release(&input_lock,flags);
        timer_id=sys_usec_timer_add(0,fm1_encoder_wakeup,FM1_ENCODER_SCAN_US,1,0);
        if(timer_id<=0){rc=-31;goto input_done;}
        event("INPUT ENCODER=STABLE2 PERIOD_US=250 WORKER=SEM\n");
    }
    while(!cancelled(epoch,session)) {
        uint32_t now;uint64_t value;
        if(mode==5) {
            /* Bounded even if the timer stops: one RTOS tick, not forever. */
            if(os_sem_pend(&input_sem,1)) {
                rc=cancelled(epoch,session)?0:-32;break;
            }
            flags=take(&input_lock);input_pending=0;release(&input_lock,flags);
            if(cancelled(epoch,session))break;
        }
        now=timer_get_ms();
        rc=fm1_wl82_keyscan_raw(&scanner,rows);if(rc)break;
        if(scans && (uint32_t)(now-previous_scan)>max_gap)max_gap=now-previous_scan;
        previous_scan=now;scans++;
        value=fm1_stock_decode_keys(rows);
        if(value!=candidate){candidate=value;changed=now;}
        if(mode==4 && stable!=candidate && (uint32_t)(now-changed)>=20) {
            for(i=0;i<41;i++)if(((stable^candidate)>>i)&1) {
                if(i>=14)snprintf(line,sizeof(line),"KEY slot=%u midi=%u %s\n",i,53+i-14,((candidate>>i)&1)?"DOWN":"UP");
                else snprintf(line,sizeof(line),"BUTTON slot=%u %s\n",i,((candidate>>i)&1)?"DOWN":"UP");
                event(line);
            }
            stable=candidate;
        }
        fm1_encoders_sample(&encoders,rows);
        if(mode==5 && (uint32_t)(now-last)>=250) {
            last=now;
            for(i=0;i<7;i++) {
                snprintf(line,sizeof(line),"KNOB id=%u ab=%u edges=%ld invalid=%lu name=%s\n",i,encoders.previous[i],
                    (long)encoders.count[i],(unsigned long)encoders.invalid[i],fm1_encoder_names[i]);event(line);
            }
        }
        wdt_clear();if(mode!=5)os_time_dly(1);
    }
input_done:
    if(mode==5) {
        unsigned wakes,coalesced;
        flags=take(&input_lock);input_enabled=0;
        wakes=input_wakes;coalesced=input_coalesced;release(&input_lock,flags);
        if(timer_id>0)sys_usec_timer_del(timer_id);
        os_sem_del(&input_sem,0);
        snprintf(line,sizeof(line),"INPUT END scans=%lu max_gap_ms=%lu wakes=%u coalesced=%u\n",
            (unsigned long)scans,(unsigned long)max_gap,wakes,coalesced);event(line);
    }
    fm1_wl82_keyscan_stop(&scanner);return rc;
}
#ifdef FM1_NES_PLAYER
#ifdef FM1_KEYSCAN_IRQ
static unsigned nes_input_irq_enabled,nes_input_irq_registered;
#ifdef FM1_KEYSCAN_PACED
static int nes_scan_timer;
static uint32_t nes_scan_timer_ticks,nes_encoder_sequence;
static void fm1_nes_scan_tick(void *unused) {
    unsigned flags=take(&input_lock);(void)unused;
    if(nes_input_irq_enabled) {
        ++nes_scan_timer_ticks;
#ifdef FM1_NES_VOLUME
        if(!(nes_scan_timer_ticks&1u))fm1_volume_tick(&nes_volume);
#endif
#ifdef FM1_NES_LIVE_FX
        /* Decode the completed snapshot before allowing its replacement.
           Seven bounded updates, outside the SPI completion interrupt. */
        if(scanner.sequence!=nes_encoder_sequence) {
            fm1_encoders_sample(&nes_fx_encoders,scanner.ready_rows);
            nes_encoder_sequence=scanner.sequence;
        }
#endif
        fm1_wl82_keyscan_async_kick(&scanner);
    }
    release(&input_lock,flags);
}
#endif
___interrupt
static void fm1_nes_keyscan_isr(void) {
    unsigned flags=take(&input_lock);
    if(nes_input_irq_enabled) {
#if defined(FM1_NES_LIVE_FX) && !defined(FM1_KEYSCAN_PACED)
        uint32_t sequence=scanner.sequence;
#endif
        fm1_wl82_keyscan_async_step(&scanner);
#if defined(FM1_NES_LIVE_FX) && !defined(FM1_KEYSCAN_PACED)
        /* Each complete sweep, including while a game frame blocks on LCD.
           Seven bounded quadrature updates only; no DSP or nested audio lock. */
        if(scanner.sequence!=sequence)fm1_encoders_sample(&nes_fx_encoders,scanner.ready_rows);
#endif
    }
    release(&input_lock,flags);
}
static void nes_scan_stop(void) {
    unsigned flags;
    if(nes_input_irq_registered)bit_clr_ie(IRQ_SPI2_IDX,0);
    flags=take(&input_lock);nes_input_irq_enabled=0;
#ifdef FM1_NES_VOLUME
    fm1_volume_stop(&nes_volume);
#endif
    if(scanner.running)fm1_wl82_keyscan_stop(&scanner);
    release(&input_lock,flags);
#ifdef FM1_KEYSCAN_PACED
    if(nes_scan_timer>0)sys_usec_timer_del(nes_scan_timer);
    nes_scan_timer=0;
#endif
    if(nes_input_irq_registered){unrequest_irq(IRQ_SPI2_IDX,0);nes_input_irq_registered=0;}
}
static int nes_scan_start(void) {
    unsigned flags;int rc;
    /* Configure while the CPU vector is masked. Enabling the vector first
       could dispatch a stale peripheral PND before we own/reset the device. */
    bit_clr_ie(IRQ_SPI2_IDX,0);
    flags=take(&input_lock);
#ifdef FM1_NES_LIVE_FX
    nes_fx_encoders.valid=0; /* Re-prime contacts after recovery, keep counts. */
#endif
    rc=fm1_wl82_keyscan_async_start(&scanner,0,now_us);
#ifdef FM1_NES_VOLUME
    /* ADC failure leaves game/USB alive but audio muted; errors in telemetry. */
    if(!rc)fm1_volume_start(&nes_volume);
#endif
#ifdef FM1_KEYSCAN_PACED
    nes_scan_timer_ticks=nes_encoder_sequence=0;
#endif
    nes_input_irq_enabled=!rc;
    release(&input_lock,flags);
    if(rc)nes_scan_stop();
    else {nes_input_irq_registered=1;request_irq(IRQ_SPI2_IDX,5,fm1_nes_keyscan_isr,0);}
#ifdef FM1_KEYSCAN_PACED
    if(!rc) {
        nes_scan_timer=sys_usec_timer_add(0,fm1_nes_scan_tick,1000u,1,0);
        if(nes_scan_timer<=0){nes_scan_stop();rc=FM1_NES_IO_ERROR;}
    }
#endif
    return rc;
}
static int nes_scan_poll(uint64_t *keys) {
    uint8_t rows[11];int rc;unsigned flags=take(&input_lock);
    rc=fm1_wl82_keyscan_async_raw(&scanner,rows);
    release(&input_lock,flags);
    if(!rc)*keys=fm1_stock_decode_keys(rows); /* Decode outside IRQ/lock. */
    return rc;
}
#else
static int nes_scan_start(void){return fm1_wl82_keyscan_start(&scanner,0,now_us);}
static int nes_scan_poll(uint64_t *keys){return fm1_wl82_keyscan_poll(&scanner,keys);}
static void nes_scan_stop(void){if(scanner.running)fm1_wl82_keyscan_stop(&scanner);}
#endif
static int nes_fail(const char *source,int rc) {
    char line[128];unsigned flags;uint32_t queued,af;
    flags=take(&audio_lock);queued=nes_queue.write_pos-nes_queue.read_pos;af=audio_fault;release(&audio_lock,flags);
    snprintf(line,sizeof(line),"NES FAILURE source=%s rc=%d frame=%lu lcd=%d audio=%lu q=%lu\n",
        source,rc,(unsigned long)nes_frames,fm1_display_error,(unsigned long)af,(unsigned long)queued);
    flags=take(&control_lock);
    if(!nes_failure[0])memcpy(nes_failure,line,strlen(line)+1);
    release(&control_lock,flags);
    return rc;
}
static int nes_key_fail(const char *source,int rc) {
    const fm1_wl82_keyscan_failure *f=&scanner.failure;
    const fm1_wl82_keyscan_trace *t=&scanner.trace;
    char lines[NES_KEY_LINES][128];unsigned flags;
    if(f->valid) {
        memset(lines,0,sizeof(lines));
        snprintf(lines[0],sizeof(lines[0]),"KEYSPI reason=%lu row=%lu phase=%lu tx=%02lx elapsed_us=%lu polls=%lu\n",
            (unsigned long)f->reason,(unsigned long)f->row,(unsigned long)f->phase,
            (unsigned long)f->value,(unsigned long)(uint32_t)(f->end_us-f->start_us),(unsigned long)f->polls);
        snprintf(lines[1],sizeof(lines[1]),"KEYSPI CON=%08lx BAUD_WRITE=%lu MUX=%08lx PA_OUT=%08lx PA_DIR=%08lx\n",
            (unsigned long)f->con,(unsigned long)f->baud_written,(unsigned long)f->mux,
            (unsigned long)f->pa_out,(unsigned long)f->pa_dir);
        snprintf(lines[2],sizeof(lines[2]),"KEYSPI start_us=%lu end_us=%lu sys=%d lsb=%d\n",
            (unsigned long)f->start_us,(unsigned long)f->end_us,clk_get("sys"),clk_get("lsb"));
        snprintf(lines[3],sizeof(lines[3]),"KEYSPI INIT_CON=%08lx INIT_MUX=%08lx SCAN_CON=%08lx SCAN_MUX=%08lx scans=%lu\n",
            (unsigned long)t->init_con,(unsigned long)t->init_mux,(unsigned long)t->scan_con,
            (unsigned long)t->scan_mux,(unsigned long)t->scans);
        snprintf(lines[4],sizeof(lines[4]),"KEYSPI change_scan=%lu change_CON=%08lx change_MUX=%08lx\n",
            (unsigned long)t->change_scan,(unsigned long)t->change_con,(unsigned long)t->change_mux);
        snprintf(lines[5],sizeof(lines[5]),"KEYSPI PRE_CON=%08lx CLEARED_CON=%08lx FIRST_CON=%08lx CNT=%lu\n",
            (unsigned long)f->pre_con,(unsigned long)f->cleared_con,(unsigned long)f->first_con,(unsigned long)f->dma_count);
        flags=take(&control_lock);
        if(!nes_key_failure[0][0])memcpy(nes_key_failure,lines,sizeof(lines));
        release(&control_lock,flags);
    }
#ifdef FM1_NES_INPUT_RECOVERY
    snprintf(lines[0],sizeof(lines[0]),"NES INPUT FAULT source=%s rc=%d frame=%lu GAME=CONTINUES\n",
        source,rc,(unsigned long)nes_frames);
    flags=take(&control_lock);
    if(!nes_input_failure[0])memcpy(nes_input_failure,lines[0],strlen(lines[0])+1);
    release(&control_lock,flags);
    return rc;
#else
    return nes_fail(source,rc);
#endif
}
#ifdef FM1_NES_INPUT_RECOVERY
static void nes_input_report(const char *state) {
    char line[128];unsigned flags;
    snprintf(line,sizeof(line),"NES INPUT state=%s errors=%u retries=%u/%u backoff_ms=%u stale_ms=%u\n",
        state,nes_input_errors,nes_input_retries,NES_INPUT_RETRIES,NES_INPUT_BACKOFF_MS,NES_INPUT_STALE_MS);
    flags=take(&control_lock);memcpy(nes_input_status,line,strlen(line)+1);release(&control_lock,flags);
    event(line);
}
static uint64_t nes_input_release(void) {
    /* Clear the board debounce too: do not retain a held pad for another frame. */
    nes_board.candidate=nes_board.stable=0;return 0;
}
static uint64_t nes_input_cached(void) {
    /* A brief missed scan must not turn a held direction/jump/note into an
       artificial release. Never sustain an unobservable key indefinitely. */
    if(nes_input_valid && (uint32_t)(timer_get_ms()-nes_input_good_at)<NES_INPUT_STALE_MS)
        return nes_input_last_keys;
    return nes_input_release();
}
static void nes_input_failed(const char *source,int rc) {
    nes_key_fail(source,rc); /* Preserve first evidence before any restart. */
    nes_scan_stop();
    if(nes_input_errors!=UINT32_MAX)++nes_input_errors;
    nes_input_retry_at=timer_get_ms();
    nes_input_report(nes_input_retries>=NES_INPUT_RETRIES?"OFFLINE":"BACKOFF");
}
#endif
static int nes_stop(void *unused) {
    uint32_t now=timer_get_ms();char line[128];unsigned flags,underruns;
    (void)unused;nes_frames++;
#ifdef FM1_LCD_ASYNC
    if(fm1_display_async_service())nes_fail("LCD_ASYNC",FM1_NES_IO_ERROR);
#endif
    if((uint32_t)(now-nes_last_log)>=1000u) {
#ifdef FM1_NES_PROFILE
        fm1_profile_stats prof;fm1_profile_snapshot(&prof);
#endif
        nes_last_log=now;flags=take(&audio_lock);underruns=nes_queue.underrun_frames;release(&audio_lock,flags);
        snprintf(line,sizeof(line),"NES frames=%lu audio=%lu underrun=%u fault=%d shown=%lu skipped=%lu\n",
            (unsigned long)nes_frames,(unsigned long)audio_frames,underruns,nes_fault,
            (unsigned long)nes_board.presented_frames,(unsigned long)nes_board.skipped_frames);event(line);
        /* Timestamp the same frame report, not an asynchronous USB heartbeat. */
        snprintf(line,sizeof(line),"NES TIME ms=%lu\n",(unsigned long)now);event(line);
#ifdef FM1_NES_VOLUME
        {fm1_volume v;unsigned current;
         flags=take(&input_lock);v=nes_volume;release(&input_lock,flags);
         flags=take(&audio_lock);current=nes_envelope.gain_q7;release(&audio_lock,flags);
         snprintf(line,sizeof(line),"VOLUME adc=4 gpio=PB6 raw=%u target=%u gain=%u valid=%u run=%u samples=%lu errors=%lu\n",
             v.raw,v.target,current,v.valid,v.running,(unsigned long)v.samples,(unsigned long)v.errors);event(line);}
#endif
#ifdef FM1_LCD_ASYNC
        {fm1_lcd_async_stats lcd;fm1_display_async_snapshot(&lcd);
         snprintf(line,sizeof(line),"LCD ASYNC submit=%lu done=%lu busy=%lu irq=%lu bytes=%lu err=%lu a=%lu p=%lu cpu=%u b=%u bpp=%u\n",
             (unsigned long)lcd.submitted,(unsigned long)lcd.completed,(unsigned long)lcd.busy_skips,
             (unsigned long)lcd.irqs,(unsigned long)lcd.bytes,(unsigned long)lcd.errors,
             (unsigned long)lcd.active,(unsigned long)lcd.pending,(unsigned)current_cpu_id(),FM1_LCD_STREAM_BAUD,FM1_LCD_WIRE_BPP);event(line);}
#endif
#ifdef FM1_NES_PROFILE
        snprintf(line,sizeof(line),"PROF A f=%lu wall=%lu cpu=%lu apu=%lu ppu=%lu\n",
            (unsigned long)nes_frames,(unsigned long)prof.wall,(unsigned long)prof.ticks[FM1_PROF_CPU],
            (unsigned long)prof.ticks[FM1_PROF_APU],(unsigned long)prof.ticks[FM1_PROF_PPU]);event(line);
        snprintf(line,sizeof(line),"PROF B f=%lu video=%lu lcd=%lu fx=%lu pcm=%lu\n",
            (unsigned long)nes_frames,(unsigned long)prof.ticks[FM1_PROF_VIDEO],(unsigned long)prof.ticks[FM1_PROF_LCD],
            (unsigned long)prof.ticks[FM1_PROF_FX],(unsigned long)prof.ticks[FM1_PROF_PCM]);event(line);
        snprintf(line,sizeof(line),"PROF C f=%lu frame=%lu other=%lu calls=%lu unit_us=500\n",
            (unsigned long)nes_frames,(unsigned long)prof.ticks[FM1_PROF_FRAME],
            (unsigned long)prof.ticks[FM1_PROF_OTHER],(unsigned long)prof.transitions);event(line);
#endif
#ifdef FM1_NES_AUDIO_PRIORITY
        {uint32_t queued,priming,refills;
         flags=take(&audio_lock);queued=nes_queue.write_pos-nes_queue.read_pos;
         priming=nes_queue.priming_frames;refills=nes_queue.rebuffer_events;release(&audio_lock,flags);
         snprintf(line,sizeof(line),"NES AUDIO queued=%lu prime_silence=%lu rebuffers=%lu target=2205\n",
             (unsigned long)queued,(unsigned long)priming,(unsigned long)refills);event(line);}
#endif
#ifdef FM1_KEYSCAN_IRQ
        {uint32_t completions,scans;
         flags=take(&input_lock);completions=scanner.completions;scans=scanner.trace.scans;
#ifdef FM1_KEYSCAN_PACED
         {uint32_t ticks=nes_scan_timer_ticks;release(&input_lock,flags);
          snprintf(line,sizeof(line),"KEYPACE period_us=1000 ticks=%lu\n",(unsigned long)ticks);event(line);}
#else
         release(&input_lock,flags);
#endif
         snprintf(line,sizeof(line),"KEYIRQ completions=%lu scans=%lu\n",(unsigned long)completions,(unsigned long)scans);event(line);}
#endif
#ifdef FM1_NES_LIVE_FX
        {static const char *const modes[]={"BYPASS","LP","BP","HP"};
#ifdef FM1_NES_CHANNEL_FX
         const fm1_fx_controls *c=&nes_channel_fx.controls[nes_channel_fx.selected];
         snprintf(line,sizeof(line),"FX channel=%s mode=%s cutoff=%u res=%u lfo_mHz=%u depth=%u mix=%u\n",
             fm1_channel_fx_name(nes_channel_fx.selected),modes[c->mode],c->cutoff,c->resonance,
             (c->rate+1u)*100u,c->depth,c->mix);
#else
         snprintf(line,sizeof(line),"FX mode=%s cutoff=%u res=%u lfo_mHz=%u depth=%u mix=%u limited=%lu\n",
             modes[nes_fx_controls.mode],nes_fx_controls.cutoff,nes_fx_controls.resonance,
             (nes_fx_controls.rate+1u)*100u,nes_fx_controls.depth,nes_fx_controls.mix,(unsigned long)nes_fx.limited);
#endif
         event(line);}
#endif
    }
    /* User-requested single-variable A/B: yield without the per-frame 10ms
       sleep. Keep persistent failure reporting for any regression. */
    wdt_clear();os_time_dly(0);
    if(audio_fault)nes_fail("AUDIO_IRQ",FM1_NES_IO_ERROR);
    return nes_fault || audio_fault || cancelled(nes_epoch,nes_session);
}
static int nes_lcd(void *unused,int data,const uint8_t *p,size_t n) {
    int rc;FM1_PROFILE_BEGIN(FM1_PROF_LCD);
    (void)unused;rc=fm1_display_write(data,p,n);FM1_PROFILE_END();
    return rc ? nes_fail("LCD_WRITE",rc):0;
}
#ifdef FM1_LCD_ASYNC
static int nes_video_begin(void *unused,int wanted) {
    int rc;FM1_PROFILE_BEGIN(FM1_PROF_LCD);(void)unused;
    rc=fm1_display_async_begin(wanted);FM1_PROFILE_END();
    if(rc<0)nes_fail("LCD_BEGIN",rc);return rc;
}
#ifdef FM1_LCD_DIRECT
static int nes_video_native_rows(void *unused,unsigned y,unsigned rows,const uint16_t *p,unsigned crop) {
    int rc;FM1_PROFILE_BEGIN(FM1_PROF_LCD);(void)unused;
    rc=fm1_display_async_native_rows(y,rows,p,crop);FM1_PROFILE_END();
    return rc?nes_fail("LCD_NATIVE_ROWS",rc):0;
}
#else
static int nes_video_rows(void *unused,unsigned y,unsigned rows,const uint8_t *p) {
    int rc;FM1_PROFILE_BEGIN(FM1_PROF_LCD);(void)unused;
    rc=fm1_display_async_rows(y,rows,p);FM1_PROFILE_END();
    return rc?nes_fail("LCD_ROWS",rc):0;
}
#endif
#endif
static int nes_delay(void *unused,uint32_t ms) {
    (void)unused;os_time_dly((int)(ms/10u+1u));return 0;
}
static int nes_wait(void *unused,uint32_t us) {
    (void)unused;wdt_clear();os_time_dly((int)((us+9999u)/10000u));return 0;
}
static uint64_t nes_keys(void *unused) {
#ifdef FM1_NES_NO_KEYS
    (void)unused;return 0; /* No scanner access, including initialization. */
#else
    uint64_t keys=0;int rc;(void)unused;
#ifdef FM1_NES_INPUT_RECOVERY
    if(cancelled(nes_epoch,nes_session))return nes_input_release();
    if(!scanner.running) {
        if(nes_input_retries>=NES_INPUT_RETRIES ||
           (uint32_t)(timer_get_ms()-nes_input_retry_at)<NES_INPUT_BACKOFF_MS)
            return nes_input_cached();
        ++nes_input_retries; /* A total per-run budget, not reset by success. */
        rc=nes_scan_start();
        if(rc){nes_input_failed("KEY_RESTART",rc);return nes_input_cached();}
        nes_input_report("RECOVERED");
    }
#endif
    rc=nes_scan_poll(&keys);
#ifdef FM1_NES_INPUT_RECOVERY
    if(rc==FM1_NES_BUSY)return nes_input_cached(); /* No completed new sweep yet. */
    if(rc){nes_input_failed("KEY_SCAN",rc);return nes_input_cached();}
    nes_input_last_keys=keys;nes_input_good_at=timer_get_ms();nes_input_valid=1;
#else
    if(rc){nes_key_fail("KEY_SCAN",rc);nes_fault=FM1_NES_IO_ERROR;}
#endif
    return keys;
#endif
}
#ifdef FM1_NES_LIVE_FX
static void nes_fx_poll(void) {
    int32_t counts[7];unsigned i,flags;
    flags=take(&input_lock);memcpy(counts,nes_fx_encoders.count,sizeof(counts));release(&input_lock,flags);
    for(i=0;i<7;++i) {
        int64_t delta=(int64_t)counts[i]-nes_fx_previous[i];
        nes_fx_previous[i]=counts[i];
        if(delta) {
            int32_t edges=(int32_t)(delta>512?512:delta<-512?-512:delta);
#ifdef FM1_NES_CHANNEL_FX
            fm1_channel_fx_edges(&nes_channel_fx,i,edges);
#else
            fm1_fx_control_edges(&nes_fx_controls,i,edges);
#endif
        }
    }
}
#endif
static int nes_pcm_impl(void *unused,const int16_t *pcm,size_t frames) {
    uint32_t start=timer_get_ms();int rc;unsigned flags;(void)unused;
#if defined(FM1_NES_LIVE_FX) && !defined(FM1_NES_CHANNEL_FX)
    int16_t filtered[64];nes_fx_poll();
    /* Apply exactly once, OUTSIDE every IRQ/lock and before enqueue retries.
       LFO follows generated audio samples; queued audio already has effects. */
    rc=fm1_fx_process(&nes_fx,&nes_fx_controls,pcm,filtered,frames);
    if(rc)return nes_fail("FX",FM1_NES_IO_ERROR);
    pcm=filtered;
#endif
    do {
        flags=take(&audio_lock);rc=fm1_audio_queue_push(&nes_queue,pcm,frames);release(&audio_lock,flags);
        if(rc!=FM1_NES_BUSY)return rc ? nes_fail("PCM_PUSH",rc):0;
        if(audio_fault)return nes_fail("PCM_IRQ",FM1_NES_IO_ERROR);
        if(cancelled(nes_epoch,nes_session))return nes_fail("PCM_CANCEL",FM1_NES_IO_ERROR);
        if((uint32_t)(timer_get_ms()-start)>=100u)return nes_fail("PCM_TIMEOUT",FM1_NES_IO_ERROR);
        wdt_clear();os_time_dly(1);
    } while(1);
}
static int nes_pcm(void *unused,const int16_t *pcm,size_t frames) {
    int rc;FM1_PROFILE_BEGIN(FM1_PROF_PCM);
    rc=nes_pcm_impl(unused,pcm,frames);FM1_PROFILE_END();return rc;
}
#ifdef FM1_NES_CHANNEL_FX
static int nes_stems(void *unused,const uint8_t *mixed,const uint8_t *const voices[4],size_t frames) {
    size_t at,n;unsigned c;int rc;int16_t pcm[64];const uint8_t *part[4];(void)unused;
    if(!mixed || !voices)return nes_fail("STEMS",FM1_NES_INVALID);
    for(c=0;c<4;++c)if(!voices[c])return nes_fail("STEMS",FM1_NES_INVALID);
    for(at=0;at<frames;at+=n) {
        n=frames-at;if(n>64)n=64;
        for(c=0;c<4;++c)part[c]=voices[c]+at;
        nes_fx_poll();
        rc=fm1_channel_fx_process(&nes_channel_fx,mixed+at,part,nes_board.config.gain,pcm,n);
        if(rc)return nes_fail("STEM_FX",FM1_NES_IO_ERROR);
        rc=nes_pcm(0,pcm,n);if(rc)return rc;
    }
    return 0;
}
#endif
#ifdef FM1_NES_AUDIO_PRIORITY
static uint32_t nes_buffered(void *unused) {
    unsigned flags;uint32_t queued;(void)unused;
    flags=take(&audio_lock);queued=nes_queue.write_pos-nes_queue.read_pos;release(&audio_lock,flags);
    return queued;
}
#endif
__attribute__((noinline,used))
static int fm1_nes_player_run(unsigned epoch,unsigned session) {
    fm1_board_config config;fm1_nes_stats stats={0};struct iis_platform_data pd;
    fm1_board_io io={0,nes_lcd,nes_delay,now_us,nes_wait,nes_keys,nes_pcm,nes_stop};
    size_t size;const uint8_t *rom=fm1_rom50_data(&size);unsigned flags;int rc,opened=0;
    nes_epoch=epoch;nes_session=session;nes_frames=0;nes_fault=0;nes_last_log=timer_get_ms();
#ifdef FM1_NES_LIVE_FX
    flags=take(&input_lock);memset(&nes_fx_encoders,0,sizeof(nes_fx_encoders));release(&input_lock,flags);
    memset(nes_fx_previous,0,sizeof(nes_fx_previous));
#ifdef FM1_NES_CHANNEL_FX
    fm1_channel_fx_reset(&nes_channel_fx);
#else
    fm1_fx_controls_reset(&nes_fx_controls);fm1_fx_reset(&nes_fx);
#endif
#endif
    flags=take(&control_lock);nes_failure[0]=0;memset(nes_key_failure,0,sizeof(nes_key_failure));release(&control_lock,flags);
#ifdef FM1_NES_INPUT_RECOVERY
    nes_input_retries=nes_input_errors=nes_input_valid=0;nes_input_last_keys=0;
    flags=take(&control_lock);nes_input_status[0]=nes_input_failure[0]=0;release(&control_lock,flags);
#endif
    rc=fm1_nes_validate_rom(rom,size);if(rc)return nes_fail("ROM",rc);
    /* The working stock fill overrides the init-table row origin to zero.
       Keep all 240 NES rows in 0..239, not the old table window 40..279. */
    /* Expand 8-bit mix into signed16 after DC removal. Gain128 is +30dB
       over gain4; arbitrary 0..255 input remains <=32640, no wraparound.
       Four-voice NES mix reaches109: <=13952, retaining FX headroom. */
    fm1_board_default_config(&config);config.gain=128;config.y_offset=0;
#ifdef FM1_NES_AUTO_SKIP
    config.max_frame_skip=5;
#endif
#ifdef FM1_NES_AUDIO_PRIORITY
    config.max_frame_skip=5;io.audio_buffered=nes_buffered;
#endif
#ifdef FM1_NES_CHANNEL_FX
    io.audio_channels=nes_stems;
#endif
#ifdef FM1_LCD_ASYNC
    io.video_begin=nes_video_begin;
#ifdef FM1_LCD_DIRECT
    io.video_native_rows=nes_video_native_rows;
#else
    io.video_rows=nes_video_rows;
#endif
#endif
    rc=fm1_stock_assign_note_keys(&config);if(rc)return nes_fail("KEY_MAP",rc);
    rc=fm1_board_construct(&nes_board,&io,&config);if(rc)return nes_fail("BOARD",rc);
    event("NES DIAG FRAME_SLEEP=0 FAILURE=PERSISTENT KEYSPI=TRACE BAUD=WRITE-ONLY\n");
#ifdef FM1_NES_INPUT_RECOVERY
    event("NES INPUT RECOVERY=BOUNDED RETRIES=3 BACKOFF_MS=50 HOLD_MS=100\n");
#endif
#ifdef FM1_NES_NO_KEYS
    event("NES INPUT=NEUTRAL SCANNER=DISABLED\n");
#elif defined(FM1_KEYSCAN_IRQ)
    event("KEYSPI MODE=DMA2-IRQ BYTES=2 IRQ=37 PRIORITY=5\n");
#elif defined(FM1_KEYSCAN_DMA2)
    event("KEYSPI MODE=DMA2-POLLED BYTES=2 IRQ=OFF\n");
#endif
#if defined(FM1_NES_AUTO_SKIP) || defined(FM1_NES_AUDIO_PRIORITY)
    event("NES SMB1 START LCD=STOCK-SEQ GAIN=128 Y=0 AUTOSKIP=5\n");
#else
    event("NES SMB1 START LCD=STOCK-SEQ GAIN=128 Y=0 AUTOSKIP=0\n");
#endif
#ifdef FM1_NES_AUDIO_PRIORITY
    event("NES AUDIO CLOCK=DAC PRIME=1470 TARGET=2205 REBUFFER=ON\n");
#endif
#ifdef FM1_NES_LIVE_FX
#ifdef FM1_NES_CHANNEL_FX
    event("FX CHANNELS=PULSE1,PULSE2,TRIANGLE,NOISE,ALL SELECT=CHANNEL ALGORITHM=MODE PRESETS=PATCH\n");
    event("FX KNOB1=CUTOFF KNOB2=RES KNOB3=LFO-RATE KNOB4=LFO-DEPTH BANKS=INDEPENDENT\n");
#else
    event("FX KNOB1=CUTOFF KNOB2=RES KNOB3=LFO-RATE KNOB4=LFO-DEPTH SELECT=MIX ALGORITHM=MODE PRESETS=PATCH\n");
#endif
#endif
    rc=fm1_display_test_init();if(rc){nes_fail("LCD_INIT",rc);fm1_display_test_stop();return rc;}
    if(cancelled(epoch,session)){fm1_display_test_stop();return 0;}
#ifdef FM1_LCD_ASYNC
    rc=fm1_display_async_start();if(rc){nes_fail("LCD_ASYNC_START",rc);fm1_display_test_stop();return rc;}
#ifdef FM1_LCD_SPI30
    event("LCD MODE=ASYNC BUFFERS=2 IRQ=16 PRIORITY=2 BAUD=1 LSB=60000000 NOMINAL_HZ=30000000 " FM1_LCD_FORMAT_NAME "\n");
#elif defined(FM1_LCD_SPI15)
    event("LCD MODE=ASYNC BUFFERS=2 IRQ=16 PRIORITY=2 BAUD=3 LSB=60000000 NOMINAL_HZ=15000000 " FM1_LCD_FORMAT_NAME "\n");
#else
    event("LCD MODE=ASYNC BUFFERS=2 IRQ=16 PRIORITY=2 BAUD=4 QUEUE=1\n");
#endif
#endif
#ifndef FM1_NES_NO_KEYS
    rc=nes_scan_start();
#ifdef FM1_NES_INPUT_RECOVERY
    if(rc)nes_input_failed("KEY_START",rc);
    else nes_input_report("ACTIVE");
#else
    if(rc){nes_key_fail("KEY_START",rc);goto nes_done;}
#endif
#endif
    memset(&pd,0,sizeof(pd));pd.port_sel=IIS_PORTC;
    pd.channel_out=pd.data_width=8;pd.mclk_output=1;pd.sr_points=128;
    fm1_audio_queue_reset(&nes_queue);fm1_audio_startup_reset(&nes_envelope);
    audio_frames=audio_irqs=audio_fault=0;
    rc=iis_open(&pd,0);if(rc){nes_fail("IIS_OPEN",rc);goto nes_done;}opened=1;
    iis_set_dec_data_handler(0,audio_output,0);
    rc=iis_set_sample_rate(44100,0);if(rc){nes_fail("IIS_RATE",rc);goto nes_done;}
    flags=take(&audio_lock);audio_nes=audio_enabled=1;release(&audio_lock,flags);
    request_irq(IRQ_ALNK_IDX,3,fm1_test_alink_isr,0);iis_channel_on(8,0);
#ifdef FM1_NES_PROFILE
    event("PROF CLOCK=SDK_HALF_MSEC UNIT_US=500 EXCLUSIVE=WALL IRQ_TIME=INCLUDED\n");
    fm1_profile_start(nes_profile_clock);
#endif
    rc=fm1_board_run(&nes_board,rom,size,0,&stats);
    if(nes_fault || audio_fault)rc=FM1_NES_IO_ERROR;
nes_done:
    if(rc)nes_fail("CORE",rc); /* Does not replace the originating failure. */
    if(nes_failure[0])event(nes_failure);
    {unsigned i;for(i=0;i<NES_KEY_LINES;i++)if(nes_key_failure[i][0])event(nes_key_failure[i]);}
    if(audio_enabled) {
        bit_clr_ie(IRQ_ALNK_IDX,0);
        flags=take(&audio_lock);audio_enabled=audio_nes=0;release(&audio_lock,flags);
        unrequest_irq(IRQ_ALNK_IDX,0);iis_channel_off(8,0);
    }
    if(opened)iis_close(0);
#ifndef FM1_NES_NO_KEYS
    nes_scan_stop();
#endif
    fm1_display_test_stop();return rc;
}
#endif
__attribute__((noinline,used))
static void fm1_peripheral_task(void *unused) {
    (void)unused;
    for(;;) {
        unsigned flags,mode,epoch,session;int rc=0;uint32_t start,last;char line[128];
        flags=take(&control_lock);mode=control.request;control.request=0;epoch=control.request_epoch;session=control.session;
        release(&control_lock,flags);
        if(!mode){os_time_dly(1);continue;}
        snprintf(line,sizeof(line),"TEST START mode=%u sys=%d lsb=%d\n",mode,clk_get("sys"),clk_get("lsb"));event(line);
        start=last=timer_get_ms();
        if(cancelled(epoch,session))rc=-10;
        else if(!fm1_clock_report_valid(clk_get("sys"),clk_get("lsb")))rc=-11;
        else if(mode==2) {
#ifdef FM1_LCD_STOCK_SEQUENCE
            event("LCD STOCK-SEQ PA2=LOW DMA=2880x40 FIRST=BLACK THEN=WHITE\n");
#elif defined(FM1_LCD_STOCK_FILL)
            event("LCD STOCK-FILL x=000000f0 y=000000f0 bytes=115200 value=ff continuous-CS\n");
#endif
            event("LCD INIT begin\n");rc=fm1_display_test_init();
            snprintf(line,sizeof(line),"LCD INIT result=%d stage=%lu\n",rc,(unsigned long)fm1_display_stage);event(line);
            while(!rc && !cancelled(epoch,session)) {
                uint32_t now=timer_get_ms();
                if((uint32_t)(now-last)>=1000) {
                    last=now;rc=fm1_display_test_frame(now-start);
                    snprintf(line,sizeof(line),"LCD FRAME result=%d stage=%lu elapsed=%lu\n",rc,(unsigned long)fm1_display_stage,(unsigned long)(now-start));event(line);
                }
                wdt_clear();os_time_dly(1);
            }
            fm1_display_test_stop();
        } else if(mode==3)rc=audio_test(epoch,session);
#ifdef FM1_NES_PLAYER
        else if(mode==7)rc=fm1_nes_player_run(epoch,session);
#endif
        else rc=input_test(mode,epoch,session);
        snprintf(line,sizeof(line),"TEST END mode=%u result=%d frames=%lu irqs=%lu\n",mode,rc,(unsigned long)audio_frames,(unsigned long)audio_irqs);event(line);
        flags=take(&control_lock);control.busy=control.mode=0;release(&control_lock,flags);
    }
}
int fm1_peripheral_start_task(void) {
    int rc=task_create(fm1_peripheral_task,0,"peripheral");
    unsigned flags=take(&control_lock);control.available=!rc;release(&control_lock,flags);
#ifdef FM1_NES_PLAYER
    if(!rc)fm1_peripheral_request(7,0); /* Standalone SMB1 at normal power-on. */
#endif
    return rc;
}
