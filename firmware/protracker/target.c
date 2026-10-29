/* FM-1 ProTracker-style editor. The task owns playback and edits; IRQs only
 * scan keys or drain PCM. All song transfer bytes remain volatile RAM. */
#include "app_config.h"
#include "system/includes.h"
#include "system/task.h"
#include "system/sys_time.h"
#include "system/timer.h"
#include "system/spinlock.h"
#include "os/os_api.h"
#include "asm/iis.h"
#include "asm/wdt.h"
#include "fm1_wl82_keyscan.h"
#include "fm1_volume.h"
#include "peripheral_logic.h"
#include "peripherals.h"
#include "display_test.h"
#include "usb.h"
#include "library.h"
#include "view.h"
#include "controls.h"
#include "session.h"
#include "bank.h"
#include "audio_queue.h"
#include "routing.h"
#ifdef FM1_USB_AUDIO
#include "target.h"
#endif
#ifdef FM1_USB_VIDEO
#include "capture.h"
#include "fm1_uvc.h"
#endif
#include <stdio.h>
#include <string.h>

static fm1_mod_document song;
#ifdef FM1_MOD_EMBEDDED_DEFAULT
extern const uint8_t fm1_mod_default_song[];
extern const uint32_t fm1_mod_default_song_size;
#endif
static fm1_mod_transfer transfer;
static fm1_tracker_session tracker_session;
/* CPU-only edit history is never exposed to endpoint or display DMA. */
static fm1_mod_history tracker_history __attribute__((section(".memp_memory_x"),aligned(4)));
#define player tracker_session.player
#define editor tracker_session.editor
#define gestures tracker_session.gestures
#define view_context tracker_session.view_context
#define current_page tracker_session.page
#define mix_stereo tracker_session.stereo
static fm1_mod_library library;
static fm1_bank active_bank;
#ifdef FM1_MOD_EMBEDDED_BANK
extern const uint8_t fm1_mod_active_bank[];
extern const uint32_t fm1_mod_active_bank_size;
#endif
static fm1_wl82_keyscan scanner;
static fm1_volume mod_volume;
static fm1_encoders encoders;
static fm1_audio_startup envelope;
static spinlock_t control_lock,audio_lock,input_lock;
static struct {unsigned request,running,available,executing,shutdown,quiescent,frames,underruns,ui_frames,ui_strips,save_hint;} control;
#define PCM_RING_FRAMES FM1_PCM_RING_FRAMES
static int16_t ring[PCM_RING_FRAMES][2];
#ifdef FM1_USB_AUDIO
static int16_t block[FM1_MOD_BLOCK*2] __attribute__((section(".memp_memory_x"),aligned(4)));
static uint8_t local_stereo;
#else
static int16_t block[FM1_MOD_BLOCK*2];
#endif
#ifdef FM1_USB_VIDEO
static fm1_tracker_capture tracker_capture __attribute__((section(".memp_memory_x"),aligned(4)));
#endif
static uint32_t rd,wr,rd_slot,wr_slot,frames,underruns;
static unsigned primed,audio_enabled,scan_enabled,scan_ticks,input_irq_registered,scan_retries;
static uint32_t scan_retry_at,encoder_sequence;
#ifdef FM1_LCD_ASYNC
#define UI_STRIP_ROWS 3u
#else
#define UI_STRIP_ROWS 8u
#endif
#if defined(FM1_TRACKER_COMPOSITE) && defined(FM1_LCD_ASYNC)
/* Async display_rows copies/converts these CPU rows into its separately owned
 * ordinary-SRAM wire frame before any SPI DMA submission. */
static uint8_t row_pixels[UI_STRIP_ROWS*480u] __attribute__((section(".memp_memory_x"),aligned(4)));
#else
static uint8_t row_pixels[UI_STRIP_ROWS*480u];
#endif
static volatile int lcd_error,key_error,audio_error;
static int scan_timer;
static void flush_audio(void);
static void notice(const char *text);
static void session_flush(void *context){(void)context;flush_audio();}
/* Keep the capture -> local routing -> analog envelope boundary explicit in
 * the linked firmware so the emitted-order audit checks the actual callback. */
__attribute__((noinline,used)) void fm1_tracker_analog_gain(int32_t *out){
    fm1_audio_startup_process24(&envelope,out);
}

static unsigned take(spinlock_t *l){unsigned f;local_irq_save(f);arch_spin_lock(l);return f;}
static void release(spinlock_t *l,unsigned f){arch_spin_unlock(l);local_irq_restore(f);}
static uint32_t now_us(void *u){(void)u;return timer_get_ms()*1000u;}
static int idle(void *u){(void)u;return control.available && !control.running && !control.request && !control.executing;}
static int request(void *u,unsigned op){
    (void)u;if(!control.available || control.request || control.executing || (op!=FM1_MOD_REQUEST_STOP && control.shutdown))return -1;
    control.request=op;return 0;
}
static void status(void *u,char *out,size_t n){
    unsigned order=control.running?player.current_order:editor.order;
    unsigned row=control.running?player.current_row:editor.row;
    (void)u;snprintf(out,n,"MOD status ready=%u running=%u dirty=%u export=%u order=%u row=%u ch=%u sample=%u mix=%u edit=%u lcd=%d keys=%d audio=%d frames=%u underruns=%u ui=%u strips=%u\n",
        song.loaded,control.running,song.dirty,transfer.exporting,order,row,editor.channel,
        editor.sample,mix_stereo,editor.edit,lcd_error,key_error,audio_error,
        control.frames,control.underruns,control.ui_frames,control.ui_strips);
}
int fm1_mod_usb_command(const char *s,uint32_t now,fm1_mod_reply reply,void *ctx){
    fm1_mod_usb_io io={0,idle,request,status};unsigned f,was_loaded;int rc;
    if(!strcmp(s,"MOD KEY")){
        char line[192];fm1_wl82_keyscan_failure failure;
        fm1_wl82_keyscan_trace trace;unsigned completions,paused,running,ticks,retries;
        f=take(&input_lock);failure=scanner.failure;trace=scanner.trace;
        completions=scanner.completions;paused=scanner.paused;running=scanner.running;
        ticks=scan_ticks;retries=scan_retries;release(&input_lock,f);
        snprintf(line,sizeof(line),"MOD key valid=%lu reason=%lu row=%lu dt=%lu con=%08lx cnt=%lu mux=%08lx scans=%lu done=%u paused=%u running=%u ticks=%u retries=%u\n",
            (unsigned long)failure.valid,(unsigned long)failure.reason,
            (unsigned long)failure.row,(unsigned long)(failure.end_us-failure.start_us),
            (unsigned long)failure.con,(unsigned long)failure.dma_count,
            (unsigned long)failure.mux,(unsigned long)trace.scans,
            completions,paused,running,ticks,retries);
        reply(ctx,line);return 1;
    }
    if(!strcmp(s,"MOD VOL")){
        char line[128];fm1_volume volume;
        f=take(&input_lock);volume=mod_volume;release(&input_lock,f);
        snprintf(line,sizeof(line),"MOD vol valid=%u target=%u raw=%u samples=%lu errors=%lu running=%u audio=%d\n",
            volume.valid,volume.target,volume.raw,(unsigned long)volume.samples,
            (unsigned long)volume.errors,volume.running,audio_error);
        reply(ctx,line);return 1;
    }
    f=take(&control_lock);was_loaded=song.loaded;
    if(!strcmp(s,"MOD INFO") && song.loaded && !transfer.active && idle(0)){
        /* Freeze edits, then hash with interrupts enabled. The DAC ISR must
         * not be held off while CRC32 walks up to 256 KiB. */
        size_t length=song.length;uint32_t crc;
        transfer.exporting=1;transfer.last_ms=now;transfer.export_length=0;
        release(&control_lock,f);crc=fm1_mod_crc32(song.bytes,length);
        f=take(&control_lock);transfer.export_crc=crc;transfer.export_length=length;
    }
    if(control.executing){release(&control_lock,f);reply(ctx,"ERR MOD BUSY\n");return 1;}
    /* Large MOD transfers can take longer than the library's offline timeout.
     * Their protocol traffic proves the PC is still servicing this request. */
    if(!strcmp(s,"LIB POLL") || (library.pending && !strncmp(s,"MOD ",4)))library.last_ms=now;
    rc=fm1_mod_library_line(&library,s,reply,ctx);
    if(!rc)rc=fm1_mod_usb_line(&song,&transfer,&io,s,now,reply,ctx);
    if(was_loaded && !song.loaded && transfer.active){fm1_mod_preview_stop(&player);flush_audio();}
    if(!was_loaded && song.loaded){
        fm1_tracker_session_reset_after_load(&tracker_session,now);control.save_hint=0;
        notice("MOD LOADED");
    }
    if(!strncmp(s,"MOD SAVED ",10) && !transfer.exporting && !song.dirty)
        fm1_mod_history_mark_saved(&tracker_history);
    release(&control_lock,f);return rc;
}
void fm1_mod_usb_reset(void){unsigned f=take(&control_lock);fm1_mod_usb_abort(&transfer);release(&control_lock,f);}
void fm1_mod_usb_tick(uint32_t now){unsigned f=take(&control_lock);fm1_mod_usb_timeout(&transfer,now);release(&control_lock,f);}
void fm1_peripheral_cancel(void){unsigned f=take(&control_lock);if(!control.quiescent){control.request=FM1_MOD_REQUEST_STOP;control.shutdown=1;}release(&control_lock,f);}
void fm1_peripheral_session_cancel(void){fm1_mod_usb_reset();}
int fm1_peripheral_idle(void){unsigned f=take(&control_lock);int result=control.quiescent;release(&control_lock,f);return result;}
int fm1_peripheral_request(unsigned command,unsigned generation){unsigned f;int rc;(void)generation;if(command!=1)return -1;
    f=take(&control_lock);rc=request(0,FM1_MOD_REQUEST_STOP);release(&control_lock,f);return rc;}
int fm1_peripheral_event(char *out,unsigned n){(void)out;(void)n;return 0;}
__attribute__((noinline,used)) void fm1_display_snapshot(unsigned phase,const uint32_t *r){(void)phase;(void)r;}

static void audio_output(void *ctx,u8 *data,int len,u8 ch){
    unsigned i;int32_t *out=(int32_t *)data;uint32_t available=wr-rd;(void)ctx;
    if(!data || len<=0)return;
    if(ch!=3 || len!=512){memset(data,0,(unsigned)len);audio_error=-1;return;}
    /* Song playback keeps its reservoir. A stopped one-shot may be shorter
     * than that reservoir and must be audible as soon as it is rendered. */
    if(!primed && available>=(control.running?1470u:1u))primed=1;
    for(i=0;i<64;i++){
        if(primed && rd!=wr){out[2*i]=(int32_t)ring[rd_slot][0]*256;out[2*i+1]=(int32_t)ring[rd_slot][1]*256;
            rd++;rd_slot=fm1_pcm_next_slot(rd_slot);}
        else {out[2*i]=out[2*i+1]=0;
            if(primed && (control.running || player.preview_active))underruns++;}
    }
    if(primed && wr==rd)primed=0;
#ifdef FM1_USB_AUDIO
    /* Capture original tracker stereo before speaker fold and analog gain.
     * The shared bounded bridge also permits optional PC audio monitoring. */
    fm1_usb_audio_dac(out,64);
    fm1_tracker_speaker_fold(out,64,local_stereo);
#endif
    {unsigned f=take(&input_lock);envelope.target_q7=mod_volume.valid?mod_volume.target:0;release(&input_lock,f);}
    fm1_tracker_analog_gain(out);frames+=64;
}
___interrupt static void fm1_test_alink_isr(void){unsigned f=take(&audio_lock);if(audio_enabled)iis_irq_handler(0);release(&audio_lock,f);}
___interrupt static void fm1_mod_keyscan_isr(void){unsigned f=take(&input_lock);if(scan_enabled)fm1_wl82_keyscan_async_step(&scanner);release(&input_lock,f);}
static void fm1_mod_scan_tick(void *u){unsigned f=take(&input_lock);(void)u;
    if(!(++scan_ticks&1))fm1_volume_tick(&mod_volume);
    if(scan_enabled){
        /* Decode before the next sweep can replace ready_rows. LCD work may
         * delay the task, but must not drop the intervening knob phases. */
        fm1_encoders_capture(&encoders,&encoder_sequence,scanner.sequence,scanner.ready_rows);
        fm1_wl82_keyscan_async_kick(&scanner);
    }
    release(&input_lock,f);}
static void publish(void){unsigned f,fr,ur;f=take(&audio_lock);fr=frames;ur=underruns;release(&audio_lock,f);
    f=take(&control_lock);control.frames=fr;control.underruns=ur;release(&control_lock,f);}
static void flush_audio(void){unsigned f=take(&audio_lock);rd=wr=rd_slot=wr_slot=0;primed=0;release(&audio_lock,f);}
static void stop(void){unsigned f;
    fm1_tracker_session_stop(&tracker_session);
    f=take(&control_lock);control.running=tracker_session.running;release(&control_lock,f);
}
static void shutdown(void){unsigned f;stop();if(scan_timer>0)sys_usec_timer_del(scan_timer);
    bit_clr_ie(IRQ_SPI2_IDX,0);f=take(&input_lock);scan_enabled=0;
    if(scanner.running)fm1_wl82_keyscan_stop(&scanner);fm1_volume_stop(&mod_volume);release(&input_lock,f);
    if(input_irq_registered)unrequest_irq(IRQ_SPI2_IDX,0);
    bit_clr_ie(IRQ_ALNK_IDX,0);f=take(&audio_lock);audio_enabled=0;release(&audio_lock,f);
    unrequest_irq(IRQ_ALNK_IDX,0);iis_channel_off(8,0);iis_close(0);
#ifdef FM1_USB_AUDIO
    fm1_usb_audio_stop();
#endif
#ifdef FM1_USB_VIDEO
    fm1_uvc_capture_stop();
#endif
    fm1_display_test_stop();
    f=take(&control_lock);control.available=0;control.quiescent=1;release(&control_lock,f);
    for(;;)os_time_dly(100);
}
static void action(unsigned op){unsigned f;
    fm1_tracker_session_action(&tracker_session,op,timer_get_ms());
    f=take(&control_lock);control.running=tracker_session.running;release(&control_lock,f);
}
#ifdef FM1_TRACKER_COMPOSITE
static fm1_tracker_view view __attribute__((section(".memp_memory_x"),aligned(4)));
#else
static fm1_tracker_view view;
#endif
#ifdef FM1_USB_VIDEO
/* Only task code renders/converts; short IRQ guards authenticate packet leases. */
__attribute__((noinline,used)) void fm1_tracker_capture_step(uint32_t now){
    fm1_uvc_write write;fm1_uvc_stream *stream;unsigned flags;int rc;
    if(transfer.active || transfer.exporting || control.shutdown)return;
    local_irq_save(flags);stream=fm1_uvc_stream_get();
    rc=fm1_uvc_reserve_bytes(stream,now,&write);local_irq_restore(flags);
    if(rc!=FM1_UVC_OK)return;
    if(!write.first_byte)fm1_tracker_capture_accept(&tracker_capture,&view);
    rc=fm1_tracker_capture_packet(&tracker_capture,&write);
    local_irq_save(flags);
    if(rc==FM1_UVC_OK)fm1_uvc_publish_bytes(stream,now,&write);
    else fm1_uvc_cancel_bytes(stream,now,&write);
    local_irq_restore(flags);
}
#endif
#if defined(FM1_USB_AUDIO) || defined(FM1_USB_VIDEO)
/* Called only after the foreground task has stopped IIS/scanning for UBOOT. */
__attribute__((noinline,used)) void fm1_peripheral_usb_audio_quiesce(void){
#ifdef FM1_USB_VIDEO
    fm1_uvc_capture_stop();
#endif
}
#endif
#ifndef FM1_LCD_ASYNC
static fm1_tracker_view previous_view;
static uint32_t dirty_strips;
static uint8_t view_valid;
#endif
static void ui_text(void){
    unsigned f=take(&control_lock),busy=transfer.active,loaded=song.loaded;
    release(&control_lock,f);
    if(!busy)
    fm1_tracker_view_update(&view,&song,&player,&editor,&library,current_page,
        control.running,mix_stereo,control.save_hint);
    view_context.quick_modifier=(uint8_t)fm1_tracker_controls_held(&gestures,6);
    view_context.block_modifier=(uint8_t)fm1_tracker_controls_held(&gestures,11);
    view_context.new_hold=(uint8_t)(gestures.new_pending && !gestures.new_fired);
    if(!busy)fm1_tracker_view_context_apply(&view,&song,&editor,&view_context);
    f=take(&control_lock);busy|=transfer.active || loaded!=song.loaded;
    release(&control_lock,f);
    if(busy){memset(&view,0,sizeof(view));strcpy(view.ui[0],"LOADING FROM USB PC");}
}
#ifndef FM1_LCD_ASYNC
static void ui_refresh(void){
    ui_text();
    if(!view_valid || memcmp(&view,&previous_view,sizeof(view))){
        previous_view=view;view_valid=1;dirty_strips=0x3fffffffu;
    }
}
#endif
static void render_strip(unsigned y){
#ifdef FM1_LCD_ASYNC
    unsigned local=y>=80?(y-80)%16:0;
    if(!view.page && y>=80 && local>=2 && local<=14 && !(local&1)){
        /* The first row of this strip repeats the last row of the previous
         * strip. The frame snapshot stays fixed until all rows are submitted. */
        memcpy(row_pixels,row_pixels+(UI_STRIP_ROWS-1u)*480u,480u);
        fm1_tracker_view_render(&view,y+1,UI_STRIP_ROWS-1u,row_pixels+480u);
        return;
    }
#endif
    fm1_tracker_view_render(&view,y,UI_STRIP_ROWS,row_pixels);
}
#ifdef FM1_LCD_ASYNC
/* Preserve an emitted ownership boundary: rows are converted/copied into the
 * display's normal-SRAM wire frame before the screen-capture producer runs. */
__attribute__((noinline,used)) int fm1_tracker_lcd_submit(unsigned y){
    return fm1_display_async_rows(y,UI_STRIP_ROWS,row_pixels);
}
#endif
#ifndef FM1_LCD_ASYNC
static int ui_strip(unsigned y){
    uint8_t col[4]={0,0,0,239},rows[4],cmd;unsigned end=y+7;
    rows[0]=(uint8_t)(y>>8);rows[1]=(uint8_t)y;
    rows[2]=(uint8_t)(end>>8);rows[3]=(uint8_t)end;
    render_strip(y);
    cmd=0x2a;if(fm1_display_write(0,&cmd,1)||fm1_display_write(1,col,4))return -1;
    cmd=0x2b;if(fm1_display_write(0,&cmd,1)||fm1_display_write(1,rows,4))return -1;
    cmd=0x2c;if(fm1_display_write(0,&cmd,1)||fm1_display_write(1,row_pixels,8*480))return -1;
    return 0;
}
#endif
static void notice(const char *text){
    fm1_tracker_session_notice(&tracker_session,text,timer_get_ms());
}
/* The controller is task-owned. This gate prevents the USB task from replacing
 * its document during an edit while audio and scanner IRQs keep running. */
static int session_begin(void){unsigned f=take(&control_lock);
    if(transfer.active || transfer.exporting || control.request || control.shutdown){
        release(&control_lock,f);return 0;
    }
    control.executing=1;tracker_session.busy=0;release(&control_lock,f);return 1;
}
static void session_end(void){unsigned f=take(&control_lock);
    control.running=tracker_session.running;control.executing=0;
    release(&control_lock,f);
#ifdef FM1_USB_AUDIO
    f=take(&audio_lock);local_stereo=mix_stereo;release(&audio_lock,f);
#endif
}
static void session_key(unsigned slot,unsigned down,uint32_t now){
    unsigned f=take(&control_lock);
    if(transfer.active || transfer.exporting || control.request || control.shutdown){
        /* Consume releases even when editing is frozen. Otherwise a modifier
         * released during a USB transfer would remain held after thawing. */
        tracker_session.busy=1;
        fm1_tracker_session_key(&tracker_session,slot,down,now);
        release(&control_lock,f);return;
    }
    release(&control_lock,f);
    if(!session_begin())return;
    fm1_tracker_session_key(&tracker_session,slot,down,now);session_end();
}
static void input_encoder(unsigned id,int step){
    if(!session_begin())return;
    fm1_tracker_session_encoder(&tracker_session,id,step,timer_get_ms());session_end();
}
__attribute__((noinline,used)) static void fm1_peripheral_task(void *u){
    struct iis_platform_data pd;unsigned f;uint32_t last_ui=0,last_status=0,changed=0;
#ifdef FM1_LCD_ASYNC
    unsigned screen_row=240;
#endif
    uint64_t candidate=0,stable=0;int32_t prior[7]={0},accum[7]={0};
    (void)u;memset(&pd,0,sizeof(pd));fm1_mod_new(&song);
#ifdef FM1_MOD_EMBEDDED_DEFAULT
    if(fm1_mod_default_song_size<=FM1_MOD_CAPACITY){
        memcpy(song.bytes,fm1_mod_default_song,fm1_mod_default_song_size);
        if(fm1_mod_open(&song,fm1_mod_default_song_size))fm1_mod_new(&song);
    }
#endif
#ifdef FM1_MOD_EMBEDDED_BANK
    fm1_bank_load(&active_bank,fm1_mod_active_bank,fm1_mod_active_bank_size);
#endif
    fm1_tracker_session_init(&tracker_session,&song,&library,&active_bank,
        &tracker_history,session_flush,0);
    lcd_error=fm1_display_test_init();
#ifdef FM1_LCD_ASYNC
    if(!lcd_error)lcd_error=fm1_display_async_start();
#endif
    bit_clr_ie(IRQ_SPI2_IDX,0);key_error=fm1_wl82_keyscan_async_start(&scanner,0,now_us);
    if(!key_error){scan_enabled=1;request_irq(IRQ_SPI2_IDX,5,fm1_mod_keyscan_isr,0);input_irq_registered=1;}
    fm1_volume_start(&mod_volume);
    scan_timer=sys_usec_timer_add(0,fm1_mod_scan_tick,1000,1,0);
    if(scan_timer<=0){key_error=-1;scan_enabled=0;fm1_volume_stop(&mod_volume);}
    fm1_audio_startup_reset(&envelope);pd.port_sel=IIS_PORTC;pd.channel_out=pd.data_width=8;pd.mclk_output=1;pd.sr_points=128;
    audio_error=iis_open(&pd,0);
    if(!audio_error){iis_set_dec_data_handler(0,audio_output,0);audio_error=iis_set_sample_rate(FM1_MOD_RATE,0);}
    if(!audio_error){audio_enabled=1;request_irq(IRQ_ALNK_IDX,3,fm1_test_alink_isr,0);iis_channel_on(8,0);}
    f=take(&control_lock);control.available=1;release(&control_lock,f);
    for(;;){
        unsigned op,running,queued,closing,i;uint32_t now=timer_get_ms();
        uint8_t rows[11];int scan_rc;int32_t counts[7];
        f=take(&control_lock);op=control.request;control.request=0;control.executing=op!=0;
        running=control.running;closing=control.shutdown;release(&control_lock,f);
        if(closing)shutdown();
        if(op){action(op);f=take(&control_lock);control.executing=0;running=control.running;release(&control_lock,f);}
        f=take(&audio_lock);queued=wr-rd;release(&audio_lock,f);
        if((running || player.preview_active) && song.loaded && !audio_error && queued<=PCM_RING_FRAMES-FM1_MOD_BLOCK){
            unsigned i;
#ifdef FM1_USB_AUDIO
            /* Session/playback are task-owned. The queued PCM keeps its
             * original panning; the DAC callback applies the local SPK mode. */
            unsigned stereo=player.stereo;player.stereo=1;
            fm1_mod_render(&player,block,FM1_MOD_BLOCK);player.stereo=(uint8_t)stereo;
#else
            fm1_mod_render(&player,block,FM1_MOD_BLOCK);
#endif
            f=take(&audio_lock);for(i=0;i<FM1_MOD_BLOCK;i++){
                ring[wr_slot][0]=block[2*i];ring[wr_slot][1]=block[2*i+1];wr++;
                wr_slot=fm1_pcm_next_slot(wr_slot);}release(&audio_lock,f);
        }
        if(!scan_enabled && key_error && scan_retries<3 &&
           (uint32_t)(now-scan_retry_at)>=250u){
            ++scan_retries;bit_clr_ie(IRQ_SPI2_IDX,0);
            f=take(&input_lock);scan_rc=fm1_wl82_keyscan_async_start(&scanner,0,now_us);
            if(!scan_rc){
                memset(&encoders,0,sizeof(encoders));encoder_sequence=0;scan_enabled=1;
            }
            release(&input_lock,f);
            if(!scan_rc){
                request_irq(IRQ_SPI2_IDX,5,fm1_mod_keyscan_isr,0);input_irq_registered=1;
                key_error=0;candidate=stable=0;
                memset(prior,0,sizeof(prior));memset(accum,0,sizeof(accum));
            }else {key_error=scan_rc;scan_retry_at=now;}
        }
        f=take(&input_lock);
        scan_rc=scan_enabled?fm1_wl82_keyscan_async_raw(&scanner,rows):FM1_NES_BUSY;
        memcpy(counts,encoders.count,sizeof(counts));release(&input_lock,f);
        if(!scan_rc){
            uint64_t keys=fm1_stock_decode_keys(rows),down,up;
            if(keys!=candidate){candidate=keys;changed=now;}
            if((uint32_t)(now-changed)>=10 && candidate!=stable){
                down=candidate&~stable;up=stable&~candidate;stable=candidate;
                /* Modifier presses precede shortcut presses in one scan;
                 * releases preserve the pending chord even if EDIT rises first. */
                if((down>>6)&1u)session_key(6,1,now);
                if((down>>9)&1u)session_key(9,1,now);
                if((down>>11)&1u)session_key(11,1,now);
                for(i=0;i<41;i++)if(i!=6 && i!=9 && i!=11 && ((down>>i)&1u))
                    session_key(i,1,now);
                for(i=0;i<41;i++)if((up>>i)&1u)
                    session_key(i,0,now);
            }
        } else if(scan_rc!=FM1_NES_BUSY && scan_enabled){
            key_error=scan_rc;scan_retry_at=now;bit_clr_ie(IRQ_SPI2_IDX,0);
            f=take(&input_lock);scan_enabled=0;fm1_wl82_keyscan_stop(&scanner);release(&input_lock,f);
            if(input_irq_registered){unrequest_irq(IRQ_SPI2_IDX,0);input_irq_registered=0;}
        }
        if(session_begin()){
            fm1_tracker_session_tick(&tracker_session,now);session_end();
        }
        running=control.running;
        if(scan_enabled)for(i=0;i<7;i++){
            int steps=fm1_encoder_detents(counts[i],&prior[i],&accum[i]);
            while(steps>0){input_encoder(i,1);steps--;}
            while(steps<0){input_encoder(i,-1);steps++;}
        }
        if((uint32_t)(now-last_status)>=250){last_status=now;publish();}
        if(!lcd_error && (queued>=1470 || !running)){
#ifdef FM1_LCD_ASYNC
            lcd_error=fm1_display_async_service();
            if(!lcd_error && screen_row==240 && (uint32_t)(now-last_ui)>=33){
                int begin=fm1_display_async_begin(1);
                if(begin<0)lcd_error=begin;
                else if(begin==1){
                    /* Text formatting walks 48 MOD cells and may take longer
                     * than the scanner's 10 ms no-progress watchdog. It runs
                     * in this task, outside the IRQ-masking control lock;
                     * the USB loader publishes song.loaded only after the
                     * fixed RAM document has passed validation. */
                    last_ui=now;ui_text();
                    screen_row=0;
                }
            }
            if(!lcd_error && screen_row<240){
                render_strip(screen_row);
                lcd_error=fm1_tracker_lcd_submit(screen_row);
                screen_row+=UI_STRIP_ROWS;
                f=take(&control_lock);control.ui_strips++;
                if(screen_row==240)control.ui_frames++;
                release(&control_lock,f);
            }
#else
            if((uint32_t)(now-last_ui)>=8){last_ui=now;
                f=take(&control_lock);ui_refresh();release(&control_lock,f);}
            if(dirty_strips){
                unsigned strip=(unsigned)__builtin_ctz(dirty_strips);
                dirty_strips&=~(1u<<strip);
                lcd_error=ui_strip(strip*8);
                f=take(&control_lock);control.ui_strips++;
                if(!dirty_strips)control.ui_frames++;
                release(&control_lock,f);
            }
#endif
        }
#ifdef FM1_USB_VIDEO
        /* Complete foreground LCD work before bounded USB screen strips. */
        if(!running || queued>=1470u)fm1_tracker_capture_step(now);
#endif
        wdt_clear();
#ifdef FM1_LCD_ASYNC
        /* The NES 30 fps path yields without a 10 ms sleep. That also keeps
         * the paced SPI2 scanner task watchdog regularly observed. */
        os_time_dly(0);
#else
        if(!running || audio_error || !dirty_strips)os_time_dly(1);
#endif
    }
}
int fm1_peripheral_start_task(void){return task_create(fm1_peripheral_task,0,"peripheral");}
