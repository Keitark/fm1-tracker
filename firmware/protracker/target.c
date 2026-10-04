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
#include <stdio.h>
#include <string.h>

static fm1_mod_document song;
#ifdef FM1_MOD_EMBEDDED_DEFAULT
extern const uint8_t fm1_mod_default_song[];
extern const uint32_t fm1_mod_default_song_size;
#endif
static fm1_mod_transfer transfer;
static fm1_mod_player player;
static uint8_t mix_stereo; /* power-on default includes all voices on speaker */
static fm1_mod_editor editor;
static fm1_mod_library library;
static uint8_t page; /* 0 pattern, 1 PC files, 2 samples in loaded song */
static fm1_wl82_keyscan scanner;
static fm1_volume mod_volume;
static fm1_encoders encoders;
static fm1_audio_startup envelope;
static spinlock_t control_lock,audio_lock,input_lock;
static struct {unsigned request,running,available,executing,shutdown,quiescent,frames,underruns,ui_frames,ui_strips,save_hint;} control;
static int16_t ring[2048][2],block[FM1_MOD_BLOCK*2];
static uint32_t rd,wr,frames,underruns;
static unsigned primed,audio_enabled,scan_enabled,scan_ticks,input_irq_registered,scan_retries;
static uint32_t scan_retry_at,encoder_sequence;
#ifdef FM1_LCD_ASYNC
#define UI_STRIP_ROWS 3u
#else
#define UI_STRIP_ROWS 8u
#endif
static uint8_t row_pixels[UI_STRIP_ROWS*480u];
static volatile int lcd_error,key_error,audio_error;
static int scan_timer;

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
    rc=fm1_mod_library_line(&library,s,reply,ctx);
    if(!rc)rc=fm1_mod_usb_line(&song,&transfer,&io,s,now,reply,ctx);
    if(!was_loaded && song.loaded){fm1_mod_editor_init(&editor,&song);editor.octave=-1;editor.edit=0;control.save_hint=0;page=0;}
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
        if(primed && rd!=wr){out[2*i]=(int32_t)ring[rd&2047][0]*256;out[2*i+1]=(int32_t)ring[rd&2047][1]*256;rd++;}
        else {out[2*i]=out[2*i+1]=0;
            if(primed && (control.running || player.preview_active))underruns++;}
    }
    if(primed && wr==rd)primed=0;
    {unsigned f=take(&input_lock);envelope.target_q7=mod_volume.valid?mod_volume.target:0;release(&input_lock,f);}
    fm1_audio_startup_process24(&envelope,out);frames+=64;
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
static void flush_audio(void){unsigned f=take(&audio_lock);rd=wr=0;primed=0;release(&audio_lock,f);}
static void stop(void){unsigned f;flush_audio();
    if(player.playing){editor.order=player.current_order;editor.row=player.current_row;}
    fm1_mod_player_stop(&player);editor.edit=0;
    f=take(&control_lock);control.running=0;release(&control_lock,f);}
static void shutdown(void){unsigned f;stop();if(scan_timer>0)sys_usec_timer_del(scan_timer);
    bit_clr_ie(IRQ_SPI2_IDX,0);f=take(&input_lock);scan_enabled=0;
    if(scanner.running)fm1_wl82_keyscan_stop(&scanner);fm1_volume_stop(&mod_volume);release(&input_lock,f);
    if(input_irq_registered)unrequest_irq(IRQ_SPI2_IDX,0);
    bit_clr_ie(IRQ_ALNK_IDX,0);f=take(&audio_lock);audio_enabled=0;release(&audio_lock,f);
    unrequest_irq(IRQ_ALNK_IDX,0);iis_channel_off(8,0);iis_close(0);fm1_display_test_stop();
    f=take(&control_lock);control.available=0;control.quiescent=1;release(&control_lock,f);
    for(;;)os_time_dly(100);
}
static void action(unsigned op){
    if(op==FM1_MOD_REQUEST_STOP){stop();return;}
    if(op==FM1_MOD_REQUEST_NEW){stop();fm1_mod_new(&song);song.dirty=1;fm1_mod_editor_init(&editor,&song);editor.octave=-1;editor.edit=0;return;}
    if((op==FM1_MOD_REQUEST_PLAY || op==FM1_MOD_REQUEST_PATTERN) && song.loaded){
        unsigned f;int rc;flush_audio();
        rc=op==FM1_MOD_REQUEST_PATTERN?
            fm1_mod_player_start_pattern_at(&player,&song,editor.order,0):
            fm1_mod_player_start_at(&player,&song,editor.order,0);
        if(rc)return;
        player.stereo=mix_stereo;editor.edit=0;
        f=take(&control_lock);control.running=1;release(&control_lock,f);
    }
}
static fm1_tracker_view view;
#ifndef FM1_LCD_ASYNC
static fm1_tracker_view previous_view;
static uint32_t dirty_strips;
static uint8_t view_valid;
#endif
static void ui_text(void){
    unsigned f=take(&control_lock),busy=transfer.active,loaded=song.loaded;
    release(&control_lock,f);
    if(!busy)
    fm1_tracker_view_update(&view,&song,&player,&editor,&library,page,
        control.running,mix_stereo,control.save_hint);
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
    if(y>=80 && local>=2 && local<=14 && !(local&1)){
        /* The first row of this strip repeats the last row of the previous
         * strip. The frame snapshot stays fixed until all rows are submitted. */
        memcpy(row_pixels,row_pixels+(UI_STRIP_ROWS-1u)*480u,480u);
        fm1_tracker_view_render(&view,y+1,UI_STRIP_ROWS-1u,row_pixels+480u);
        return;
    }
#endif
    fm1_tracker_view_render(&view,y,UI_STRIP_ROWS,row_pixels);
}
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
static void input_key(unsigned slot){
    unsigned f=take(&control_lock);
    if(slot==6){
        page=0;editor.edit=0;fm1_mod_preview_stop(&player);
        if(!control.running)flush_audio();
        release(&control_lock,f);return;
    }
    if(page==1){
        if(slot==8)page=0;
        else if(slot==12)fm1_mod_library_pick(&library);
        else if(slot==4 && song.loaded)page=2;
        release(&control_lock,f);return;
    }
    if(page==2){
        if(slot==8 || slot==4 || slot==12)page=0;
        release(&control_lock,f);return;
    }
    if(slot!=9)control.save_hint=0;
    if(slot==12){if(!control.request && !transfer.exporting && !transfer.active)
        request(0,control.running || player.preview_active?FM1_MOD_REQUEST_STOP:FM1_MOD_REQUEST_PLAY);}
    else if(slot==7){if(!control.request && !transfer.exporting && !transfer.active)
        request(0,control.running || player.preview_active?FM1_MOD_REQUEST_STOP:FM1_MOD_REQUEST_PATTERN);}
    else if(slot==2 && editor.edit)editor.sample_mode=0; /* FX: cell parameters */
    else if((slot==3 || slot==5) && editor.edit && song.loaded &&
            !transfer.exporting && !transfer.active){
        if(control.running){editor.order=player.current_order;editor.row=player.current_row;}
        if(slot==3)fm1_mod_editor_clear_note(&editor); /* SCL: leave effects */
        else fm1_mod_editor_clear_effect(&editor);  /* LFO: leave note */
    }
    else if(slot==4){if(editor.edit)editor.sample_mode=1;else if(song.loaded)page=2;}
    else if(slot==0 && editor.octave>-2)editor.octave--;
    else if(slot==1 && editor.octave<1)editor.octave++;
    else if(slot==8){if(editor.edit){editor.order=0;editor.row=0;}else page=1;}
    else if(slot==9)control.save_hint=1;
    else if((slot==10 || slot==11) && song.loaded && !control.running &&
            !transfer.exporting && !transfer.active && song.orders<128){
        unsigned old=song.bytes[952+editor.order],new_pattern;
        int added=fm1_mod_add_pattern(&song);
        if(added>=0){new_pattern=(unsigned)added;
            if(slot==11)memcpy(song.bytes+1084u+new_pattern*1024u,song.bytes+1084u+old*1024u,1024u);
            fm1_mod_set_order(&song,song.orders,new_pattern);editor.order=(uint8_t)(song.orders-1);editor.row=0;}
    }
    else if(slot==13 && !transfer.exporting && !transfer.active){
        if(!editor.edit && control.running){editor.order=player.current_order;editor.row=player.current_row;}
        editor.edit^=1;
    }
    else if(slot>=14 && slot<41 && !transfer.exporting && !transfer.active){
        int midi=53+(int)slot-14+(int)editor.octave*12;
        if(midi>=36 && midi<=71 && song.loaded){
            if(!control.running)flush_audio();
            fm1_mod_preview_start(&player,&song,editor.sample,(unsigned)midi,editor.channel);
            if(editor.edit){
                if(control.running){editor.order=player.current_order;editor.row=player.current_row;}
                fm1_mod_editor_note(&editor,(unsigned)midi);
                if(control.running)editor.row=player.current_row;
            }
        }
    }
    release(&control_lock,f);
}
static void select_sample(int step){
    int n=(int)editor.sample+step;
    if(n<1)n=1;if(n>31)n=31;
    editor.sample=(uint8_t)n;
}
static void input_encoder(unsigned id,int step){
    unsigned f=take(&control_lock);fm1_mod_cell cell;unsigned pattern;
    control.save_hint=0;
    if(page==1){if(id==0 || id==2)fm1_mod_library_move(&library,step);release(&control_lock,f);return;}
    if(page==2){if(song.loaded && (id==0 || id==2 || id==6))select_sample(step);
        release(&control_lock,f);return;}
    if(!song.loaded || transfer.active || transfer.exporting){release(&control_lock,f);return;}
    if(control.running && editor.edit && (id==4 || id==5)){
        editor.order=player.current_order;editor.row=player.current_row;
    }
    pattern=song.bytes[952+editor.order];
    if(id==0)fm1_mod_editor_move(&editor,0,step,0); /* SELECT: track */
    else if(id==1){if(!control.running)fm1_mod_editor_move(&editor,0,0,step);} /* ALGORITHM: order */
    else if(id==2){if(!control.running)fm1_mod_editor_move(&editor,step,0,0);} /* KNOB1: row */
    else if(id==3){int n=(int)editor.step+step;if(n<0)n=0;if(n>16)n=16;editor.step=(uint8_t)n;}
    else if(id==4 && !editor.edit){mix_stereo=(uint8_t)(step>0);player.stereo=mix_stereo;}
    else if(id==6)select_sample(step); /* PRESETS: sample */
    else if(editor.edit && editor.sample_mode && (id==4 || id==5)){
        unsigned h=20u+(editor.sample-1u)*30u;
        if(id==4){int volume=(int)song.bytes[h+25]+step;
            if(volume<0)volume=0;if(volume>64)volume=64;
            fm1_mod_set_sample_volume(&song,editor.sample,(unsigned)volume);
        }else fm1_mod_set_sample_finetune(&song,editor.sample,(song.bytes[h+24]+step+16)&15);
    }
    else if(editor.edit && !fm1_mod_cell_get(&song,pattern,editor.row,editor.channel,&cell)){
        if(id==4)cell.effect=(uint8_t)((cell.effect+step+16)&15);
        else if(id==5)cell.param=(uint8_t)(cell.param+step);
        fm1_mod_cell_set(&song,pattern,editor.row,editor.channel,cell);
    }
    release(&control_lock,f);
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
    fm1_mod_editor_init(&editor,&song);editor.octave=-1;editor.edit=0;
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
        if((running || player.preview_active) && song.loaded && !audio_error && queued<=2048-FM1_MOD_BLOCK){
            unsigned i;fm1_mod_render(&player,block,FM1_MOD_BLOCK);
            f=take(&audio_lock);for(i=0;i<FM1_MOD_BLOCK;i++){
                ring[wr&2047][0]=block[2*i];ring[wr&2047][1]=block[2*i+1];wr++;}release(&audio_lock,f);
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
            uint64_t keys=fm1_stock_decode_keys(rows),down;
            if(keys!=candidate){candidate=keys;changed=now;}
            if((uint32_t)(now-changed)>=10 && candidate!=stable){
                down=candidate&~stable;stable=candidate;
                for(i=0;i<41;i++)if((down>>i)&1u)input_key(i);
            }
        } else if(scan_rc!=FM1_NES_BUSY && scan_enabled){
            key_error=scan_rc;scan_retry_at=now;bit_clr_ie(IRQ_SPI2_IDX,0);
            f=take(&input_lock);scan_enabled=0;fm1_wl82_keyscan_stop(&scanner);release(&input_lock,f);
            if(input_irq_registered){unrequest_irq(IRQ_SPI2_IDX,0);input_irq_registered=0;}
        }
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
                lcd_error=fm1_display_async_rows(screen_row,UI_STRIP_ROWS,row_pixels);
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
