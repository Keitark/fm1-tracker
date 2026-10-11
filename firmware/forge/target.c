/* Fixed board/system HAL. User modules never acquire USB, GPIO or flash. */
#ifdef FM1_FORGE_HOST
#include "tests/fake_hal.h"
#else
#include "app_config.h"
#include "system/includes.h"
#include "system/task.h"
#include "system/sys_time.h"
#include "system/timer.h"
#include "system/spinlock.h"
#include "os/os_api.h"
#include "asm/clock.h"
#include "asm/iis.h"
#include "asm/wdt.h"
#endif
#include "forge_target.h"
#include "peripherals.h"
#include "peripheral_logic.h"
#include "fm1_wl82_keyscan.h"
#include "fm1_volume.h"
#include "fm1_board.h"
#include "display_test.h"
#ifdef FM1_USB_AUDIO
#include "target.h"
#endif
#include <stdio.h>
#include <string.h>
#ifndef FM1_FORGE_BOOT_CLOCK_HZ
#define FM1_FORGE_BOOT_CLOCK_HZ 0
#endif
#if FM1_FORGE_BOOT_CLOCK_HZ != 0 && FM1_FORGE_BOOT_CLOCK_HZ != 320000000
#error Unreviewed FM1 Forge CPU preset
#endif
#if !defined(FM1_KEYSCAN_IRQ) || !defined(FM1_KEYSCAN_PACED) || !defined(FM1_LCD_ASYNC)
#error Forge requires paced IRQ controls and maintained async LCD ownership
#endif
static spinlock_t input_lock,audio_lock,control_lock;
static fm1_wl82_keyscan scanner;
static fm1_volume volume;
static fm1_encoders encoders;
static fm1_forge_controls controls;
static fm1_forge_voice voice;
static fm1_audio_startup envelope;
static fm1_forge_frame published;
static unsigned available,running,pending,quiescent,scan_enabled,audio_enabled;
static int scan_timer;
static unsigned scan_ticks,encoder_sequence,scan_irq_registered,audio_opened;
static uint32_t audio_frames,audio_irqs,sys_hz,lsb_hz;
static unsigned clock_prepared;
static int key_error,lcd_error,audio_error,clock_error;
static uint8_t strip[8u*FM1_LCD_WIRE_ROW_BYTES];
static unsigned take(spinlock_t *l){unsigned f;local_irq_save(f);arch_spin_lock(l);return f;}
static void release(spinlock_t *l,unsigned f){arch_spin_unlock(l);local_irq_restore(f);}
static uint32_t now_us(void *u){(void)u;return timer_get_ms()*1000u;}
/* Opt-in preset is applied ONCE before either USB or DMA starts. SDK source
 * declares320M, HSB<=160M, SFC/LSB<=80M. No396M overclock table is enabled.
 * This is source-guarded experimental selection, not bench qualification. */
__attribute__((noinline,used)) int fm1_forge_clock_prepare(void){
    int hsb,sfc;
    if(available || running || scan_timer || audio_opened || audio_enabled)return -83;
    if(clock_prepared)return clock_error;
    clock_prepared=1;sys_hz=(uint32_t)clk_get("sys");lsb_hz=(uint32_t)clk_get("lsb");
#if FM1_FORGE_BOOT_CLOCK_HZ
    if(sys_hz!=FM1_FORGE_BOOT_CLOCK_HZ && system_clock_set(FM1_FORGE_BOOT_CLOCK_HZ)<0)clock_error=-80;
    sys_hz=(uint32_t)clk_get("sys");lsb_hz=(uint32_t)clk_get("lsb");
    if(sys_hz!=FM1_FORGE_BOOT_CLOCK_HZ)clock_error=-81;
#endif
    hsb=clk_get("hsb");sfc=clk_get("sfc");
    if(sys_hz<24000000u || sys_hz>480000000u || !lsb_hz || lsb_hz>80000000u || hsb<=0 || hsb>160000000 || sfc<=0 || sfc>80000000)clock_error=-82;
    return clock_error;
}
static void scan_tick(void *u){unsigned f=take(&input_lock);(void)u;
    if(!(++scan_ticks&1))fm1_volume_tick(&volume);
    if(scan_enabled){fm1_encoders_capture(&encoders,&encoder_sequence,scanner.sequence,scanner.ready_rows);fm1_wl82_keyscan_async_kick(&scanner);}
    release(&input_lock,f);
}
___interrupt __attribute__((used)) static void fm1_forge_keyscan_isr(void){unsigned f=take(&input_lock);if(scan_enabled)fm1_wl82_keyscan_async_step(&scanner);release(&input_lock,f);}
__attribute__((noinline,used)) static void fm1_forge_audio_output(void *ctx,u8 *data,int len,u8 channel){
    int32_t *pcm=(int32_t *)data;unsigned f;(void)ctx;
    if(!data || len<=0)return;
    if(channel!=3 || len!=512){memset(data,0,(unsigned)len);audio_error=-20;return;}
    /* ALINK holds audio_lock. Only audio->input nesting is permitted; scanner
     * and timer never take audio_lock. Module audio cannot access device APIs. */
    f=take(&input_lock);envelope.target_q7=volume.valid?volume.target:0;release(&input_lock,f);
#ifdef FM1_FORGE_USER_MODULE
    memset(pcm,0,512);if(fm1_forge_user_module.audio)fm1_forge_user_module.audio(pcm,64);fm1_forge_audio_limit(pcm,64);
#else
    fm1_forge_voice_render(&voice,pcm,64);
#endif
#ifdef FM1_USB_AUDIO
    fm1_usb_audio_dac(pcm,64); /* Capture diagnostics then merge PC playback. */
#endif
    fm1_audio_startup_process24(&envelope,pcm);audio_frames+=64;
}
___interrupt __attribute__((used)) static void fm1_forge_alink_isr(void){unsigned f=take(&audio_lock);if(audio_enabled){iis_irq_handler(0);audio_irqs++;}release(&audio_lock,f);}
void fm1_forge_hal_snapshot(fm1_forge_input *out){unsigned f;if(!out)return;f=take(&control_lock);*out=published.input;release(&control_lock,f);}
/* IRQ-safe final guard; normal FORGE STOP performs full task teardown first.
 * No waits, logging, allocation or synchronous LCD transfers here. */
__attribute__((noinline,used)) void fm1_peripheral_usb_audio_quiesce(void){unsigned f;
    bit_clr_ie(IRQ_ALNK_IDX,0);bit_clr_ie(IRQ_SPI2_IDX,0);
    f=take(&audio_lock);audio_enabled=0;fm1_forge_voice_init(&voice);release(&audio_lock,f);
    f=take(&input_lock);scan_enabled=0;fm1_volume_stop(&volume);release(&input_lock,f);
}
void fm1_peripheral_usb_audio_stop(void){fm1_peripheral_usb_audio_quiesce();}
static void shutdown(void){unsigned f;
    if(scan_timer){sys_usec_timer_del(scan_timer);scan_timer=0;}
    fm1_peripheral_usb_audio_quiesce();
    f=take(&input_lock);if(scanner.running)fm1_wl82_keyscan_stop(&scanner);release(&input_lock,f);
    if(scan_irq_registered){unrequest_irq(IRQ_SPI2_IDX,0);scan_irq_registered=0;}
    if(audio_opened){unrequest_irq(IRQ_ALNK_IDX,0);iis_channel_off(8,0);iis_close(0);audio_opened=0;}
    fm1_display_test_stop();
#ifdef FM1_USB_AUDIO
    fm1_usb_audio_stop();
#endif
#ifdef FM1_FORGE_USER_MODULE
    if(fm1_forge_user_module.shutdown)fm1_forge_user_module.shutdown();
#endif
    f=take(&control_lock);running=pending=0;quiescent=1;published.input.keys=0;release(&control_lock,f);
}
void fm1_peripheral_cancel(void){unsigned f=take(&control_lock);if(!quiescent)pending=1;release(&control_lock,f);}
void fm1_peripheral_session_cancel(void){} /* DTR changes do not stop hardware. */
int fm1_peripheral_idle(void){unsigned f=take(&control_lock);int q=quiescent;release(&control_lock,f);return q;}
int fm1_peripheral_request(unsigned command,unsigned generation){(void)generation;if(command==1){fm1_peripheral_cancel();return 0;}return command==6?0:-1;}
int fm1_peripheral_event(char *out,unsigned size){(void)out;(void)size;return 0;}
__attribute__((noinline,used)) void fm1_display_snapshot(unsigned phase,const uint32_t *r){(void)phase;(void)r;}
static void snapshot(fm1_forge_frame *out){unsigned f=take(&control_lock);*out=published;release(&control_lock,f);}
int fm1_forge_usb_command(const char *s,uint32_t now,fm1_forge_reply reply,void *ctx){
    fm1_forge_frame p;unsigned a,r,q,f;char line[224];(void)now;
    if(strcmp(s,"FORGE STATUS") && strcmp(s,"FORGE INPUT") && strcmp(s,"FORGE AUDIO") && strcmp(s,"FORGE LCD") && strcmp(s,"FORGE CLOCK") && strcmp(s,"FORGE MODULE") && strcmp(s,"FORGE STOP") && strcmp(s,"HAL GET") && strcmp(s,"FORGE HELP"))return 0;
    if(!strcmp(s,"FORGE STOP")){fm1_peripheral_cancel();reply(ctx,"OK FORGE STOP REQUESTED\n");return 1;}
    if(!strcmp(s,"FORGE HELP")){reply(ctx,"FORGE STATUS INPUT AUDIO LCD CLOCK MODULE STOP; HAL GET\n");return 1;}
    snapshot(&p);f=take(&control_lock);a=available;r=running;q=pending;release(&control_lock,f);
    if(!strcmp(s,"FORGE INPUT") || !strcmp(s,"HAL GET")){
        unsigned i;snprintf(line,sizeof(line),"FORGE INPUT keys=%08lx%08lx seen=%08lx%08lx scans=%lu errors=%lu volume=%u gain=%u valid=%u min=%u max=%u adc_samples=%lu adc_errors=%lu\n",
            (unsigned long)(p.input.keys>>32),(unsigned long)p.input.keys,(unsigned long)(p.input.seen>>32),(unsigned long)p.input.seen,
            (unsigned long)p.input.scans,(unsigned long)p.input.input_errors,p.input.volume_raw,p.input.volume_gain,p.input.volume_valid,p.input.volume_min,p.input.volume_max,(unsigned long)p.input.volume_samples,(unsigned long)p.input.volume_errors);reply(ctx,line);
        for(i=0;i<7;i++){snprintf(line,sizeof(line),"FORGE KNOB id=%u name=%s edges=%ld invalid=%lu seen=%u\n",i,fm1_encoder_names[i],(long)p.input.encoder[i],(unsigned long)p.input.encoder_invalid[i],(p.input.encoder_seen>>i)&1u);reply(ctx,line);}
    }else if(!strcmp(s,"FORGE AUDIO")){
        snprintf(line,sizeof(line),"FORGE AUDIO frames=%lu irqs=%lu note=%u rate=44100 peak=131072 cap=88200 fault=%d\n",(unsigned long)p.audio_frames,(unsigned long)p.audio_irqs,p.note,audio_error);reply(ctx,line);
#ifdef FM1_USB_AUDIO
        fm1_usb_audio_status(line,sizeof(line));reply(ctx,line);fm1_usb_audio_transport_status(line,sizeof(line));reply(ctx,line);
#endif
    }else if(!strcmp(s,"FORGE LCD")){snprintf(line,sizeof(line),"FORGE LCD completed=%lu submitted=%lu busy=%lu errors=%lu fps10=%u format=%s divider=%u fault=%d\n",
        (unsigned long)p.lcd_completed,(unsigned long)p.ui_frames,(unsigned long)p.lcd_busy,(unsigned long)p.lcd_errors,p.fps_tenths,FM1_LCD_FORMAT_NAME,FM1_LCD_STREAM_BAUD,lcd_error);reply(ctx,line);
    }else if(!strcmp(s,"FORGE CLOCK")){snprintf(line,sizeof(line),"FORGE CLOCK preset=%s sys=%lu lsb=%lu fault=%d dynamic=blocked qualified=0\n",FM1_FORGE_BOOT_CLOCK_HZ?"sdk-320":"sdk-default",(unsigned long)sys_hz,(unsigned long)lsb_hz,clock_error);reply(ctx,line);
    }else if(!strcmp(s,"FORGE MODULE")){snprintf(line,sizeof(line),"FORGE MODULE abi=1 user=%u name=%s system=usb,uboot,input,lcd,audio\n",p.module_user,p.module_user?fm1_forge_user_module.name:"diagnostics");reply(ctx,line);
    }else{snprintf(line,sizeof(line),"FORGE STATUS ready=%u running=%u pending=%u frames=%lu audio_frames=%lu scans=%lu keys=%08lx%08lx lcd=%d fault=%d input_errors=%lu adc_errors=%lu\n",a,r,q,
        (unsigned long)p.lcd_completed,(unsigned long)p.audio_frames,(unsigned long)p.input.scans,(unsigned long)(p.input.keys>>32),(unsigned long)p.input.keys,lcd_error,p.fault,(unsigned long)p.input.input_errors,(unsigned long)p.input.volume_errors);reply(ctx,line);}
    return 1;
}
__attribute__((noinline,used)) static void fm1_forge_task(void *unused){
    struct iis_platform_data pd;fm1_forge_frame frame={0},view={0};uint32_t last_ui=0,last_fps=0,fps_count=0;unsigned y=240;
    int32_t previous_note=255;unsigned f,retries=0;uint32_t retry_at=0;(void)unused;
    fm1_forge_controls_init(&controls);fm1_forge_voice_init(&voice);fm1_audio_startup_reset(&envelope);
#ifdef FM1_FORGE_USER_MODULE
    if(fm1_forge_user_module.abi!=FM1_FORGE_ABI)audio_error=-90;
    else if(fm1_forge_user_module.init)fm1_forge_user_module.init();
    frame.module_user=1;
#endif
#ifdef FM1_USB_AUDIO
    frame.usb_audio=1;
#endif
    frame.sys_hz=sys_hz;frame.lsb_hz=lsb_hz;frame.note=255;
    memset(&pd,0,sizeof(pd));pd.port_sel=IIS_PORTC;pd.channel_out=pd.data_width=8;pd.mclk_output=1;pd.sr_points=128;
    if(!clock_error && !audio_error){audio_error=iis_open(&pd,0);if(!audio_error){audio_opened=1;iis_set_dec_data_handler(0,fm1_forge_audio_output,0);audio_error=iis_set_sample_rate(44100,0);}}
    if(!audio_error && audio_opened){f=take(&input_lock);audio_error=fm1_volume_start(&volume);release(&input_lock,f);
        scan_timer=sys_usec_timer_add(0,scan_tick,1000,1,0);if(scan_timer<=0){scan_timer=0;key_error=-30;}
        f=take(&audio_lock);audio_enabled=!audio_error;release(&audio_lock,f);
        if(audio_enabled){request_irq(IRQ_ALNK_IDX,3,fm1_forge_alink_isr,0);iis_channel_on(8,0);}}
    lcd_error=fm1_display_test_init();if(!lcd_error)lcd_error=fm1_display_async_start();
    if(!key_error){bit_clr_ie(IRQ_SPI2_IDX,0);f=take(&input_lock);key_error=fm1_wl82_keyscan_async_start(&scanner,0,now_us);scan_enabled=!key_error;release(&input_lock,f);
        if(scan_enabled){request_irq(IRQ_SPI2_IDX,5,fm1_forge_keyscan_isr,0);scan_irq_registered=1;}}
    f=take(&control_lock);running=1;release(&control_lock,f);
    for(;;){
        uint32_t now=timer_get_ms();uint8_t rows[11];int rc;fm1_volume v;int32_t counts[7];uint32_t invalid[7];unsigned stop,note;
        f=take(&control_lock);stop=pending;release(&control_lock,f);if(stop){shutdown();for(;;)os_time_dly(100);}
        if(!scan_enabled && key_error && scan_timer>0 && retries<3 && (uint32_t)(now-retry_at)>=50){
            retries++;bit_clr_ie(IRQ_SPI2_IDX,0);f=take(&input_lock);
            encoders.valid=0;rc=fm1_wl82_keyscan_async_start(&scanner,0,now_us);encoder_sequence=scanner.sequence;
            if(!rc)scan_enabled=1;release(&input_lock,f);
            if(!rc){request_irq(IRQ_SPI2_IDX,5,fm1_forge_keyscan_isr,0);scan_irq_registered=1;key_error=0;}
            else {key_error=rc;retry_at=now;}
        }
        f=take(&input_lock);rc=scan_enabled?fm1_wl82_keyscan_async_raw(&scanner,rows):FM1_NES_BUSY;
        v=volume;memcpy(counts,encoders.count,sizeof(counts));memcpy(invalid,encoders.invalid,sizeof(invalid));release(&input_lock,f);
        if(!rc)fm1_forge_controls_scan(&controls,fm1_stock_decode_keys(rows),now);
        else if(rc!=FM1_NES_BUSY){controls.input.input_errors++;key_error=rc;retry_at=now;bit_clr_ie(IRQ_SPI2_IDX,0);
            f=take(&input_lock);scan_enabled=0;fm1_wl82_keyscan_stop(&scanner);release(&input_lock,f);
            if(scan_irq_registered){unrequest_irq(IRQ_SPI2_IDX,0);scan_irq_registered=0;}
            controls.input.keys=controls.candidate=0;controls.initialized=0; /* Faulted input is never held during recovery. */}
        fm1_forge_controls_stale(&controls,now);fm1_forge_controls_encoders(&controls,counts,invalid);
        fm1_forge_controls_volume(&controls,v.raw,v.target,v.valid);controls.input.volume_samples=v.samples;controls.input.volume_errors=v.errors;if(v.errors && !audio_error)audio_error=-31;
        note=fm1_forge_note_for_keys(controls.input.keys);
        if(note!=(unsigned)previous_note){f=take(&audio_lock);if(note==255)fm1_forge_voice_release(&voice);else fm1_forge_voice_note(&voice,note);release(&audio_lock,f);previous_note=(int32_t)note;}
#ifdef FM1_FORGE_USER_MODULE
        if(fm1_forge_user_module.update)fm1_forge_user_module.update(&controls.input,now);
#endif
        frame.input=controls.input;frame.uptime_ms=now;
        f=take(&audio_lock);frame.audio_frames=audio_frames;frame.audio_irqs=audio_irqs;frame.note=voice.note;release(&audio_lock,f);
        if(!lcd_error){fm1_lcd_async_stats lcd;rc=fm1_display_async_service();if(rc)lcd_error=rc;
            fm1_display_async_snapshot(&lcd);frame.lcd_completed=lcd.completed;frame.lcd_busy=lcd.busy_skips;frame.lcd_errors=lcd.errors;
            if((uint32_t)(now-last_fps)>=1000){uint32_t elapsed=now-last_fps;frame.fps_tenths=(uint16_t)((lcd.completed-fps_count)*10000u/elapsed);last_fps=now;fps_count=lcd.completed;}
            if(y==240 && (uint32_t)(now-last_ui)>=33){rc=fm1_display_async_begin(1);if(rc<0)lcd_error=rc;else if(rc){view=frame;y=0;last_ui=now;frame.ui_frames++;}}
            /* Build at most128 rows per task turn, then yield. The scanner,
             * volume timer and DAC run independently; no whole-frame wait. */
            {unsigned built=0;while(y<240 && !lcd_error && built<16){
#ifdef FM1_FORGE_USER_MODULE
                if(fm1_forge_user_module.render)fm1_forge_user_module.render(&view,y,8,strip,FM1_LCD_WIRE_BPP);else (void)fm1_forge_render(&view,y,8,strip,FM1_LCD_WIRE_BPP);
#else
                (void)fm1_forge_render(&view,y,8,strip,FM1_LCD_WIRE_BPP);
#endif
                rc=fm1_display_async_wire_rows(y,8,strip,sizeof(strip));if(rc<0)lcd_error=rc;else y+=8;built++;}}}
        frame.fault=(int16_t)(clock_error?clock_error:audio_error?audio_error:key_error?key_error:lcd_error);
        f=take(&control_lock);published=frame;release(&control_lock,f);wdt_clear();os_time_dly(1);
    }
}
int fm1_peripheral_start_task(void){int rc;available=running=pending=quiescent=0;rc=task_create(fm1_forge_task,0,"#C0peripheral");
    if(rc){quiescent=1;return rc;}available=1;return 0;}
