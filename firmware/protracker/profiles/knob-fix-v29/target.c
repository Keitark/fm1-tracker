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
    unsigned order=control.running?player.order:editor.order;
    unsigned row=control.running?player.row:editor.row;
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
    if(!primed && available>=1470)primed=1;
    for(i=0;i<64;i++){
        if(primed && rd!=wr){out[2*i]=(int32_t)ring[rd&2047][0]*256;out[2*i+1]=(int32_t)ring[rd&2047][1]*256;rd++;}
        else {out[2*i]=out[2*i+1]=0;if(primed)underruns++;}
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
        /* Decode before the next sweep replaces ready_rows; display work must
         * not discard knob phases. Keep the stock stable-two filter. */
        fm1_encoders_capture(&encoders,&encoder_sequence,scanner.sequence,scanner.ready_rows);
        fm1_wl82_keyscan_async_kick(&scanner);
    }
    release(&input_lock,f);}
static void publish(void){unsigned f,fr,ur;f=take(&audio_lock);fr=frames;ur=underruns;release(&audio_lock,f);
    f=take(&control_lock);control.frames=fr;control.underruns=ur;release(&control_lock,f);}
static void stop(void){unsigned f=take(&audio_lock);rd=wr=0;primed=0;release(&audio_lock,f);
    if(player.playing){editor.order=player.order;editor.row=player.row;}
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
    if(op==FM1_MOD_REQUEST_PLAY && song.loaded){
        unsigned f=take(&audio_lock);rd=wr=0;primed=0;release(&audio_lock,f);
        fm1_mod_player_start(&player,&song);player.stereo=mix_stereo;editor.edit=0;
        f=take(&control_lock);control.running=1;release(&control_lock,f);
    }
}
/* Original compact 3x5 font. No imported artwork or Amiga ROM data. */
static uint16_t glyph(char c){
    static const uint16_t digits[10]={0x7b6f,0x2492,0x73e7,0x73cf,0x5bc9,0x79cf,0x79ef,0x7249,0x7bef,0x7bcf};
    if(c>='0'&&c<='9')return digits[c-'0'];
    switch(c){
    case 'A':return 0x2bed;case 'B':return 0x6bae;case 'C':return 0x78e7;case 'D':return 0x6b6e;
    case 'E':return 0x79e7;case 'F':return 0x79e4;case 'G':return 0x78ef;case 'H':return 0x5bed;
    case 'I':return 0x7497;case 'J':return 0x124f;case 'K':return 0x5bad;case 'L':return 0x4927;
    case 'M':return 0x7fed;case 'N':return 0x7b6d;case 'O':return 0x7b6f;case 'P':return 0x7be4;
    case 'R':return 0x7bad;case 'S':return 0x79cf;case 'T':return 0x7492;case 'U':return 0x5b6f;
    case 'V':return 0x5b6a;case 'W':return 0x5b7f;case 'X':return 0x5aad;case 'Y':return 0x5a92;
    case 'Q':return 0x7b7f;case 'Z':return 0x73e7;case '-':return 0x01c0;
    case '#':return 0x2f7a;case ':':return 0x0090;case '.':return 0x0002;case '/':return 0x1250;
    case '>':return 0x4454;
    default:return 0;}
}
/* Compact 5x7 glyphs for four full note/sample/effect cells across 240 pixels. */
static uint64_t grid_glyph(char c){
    switch(c){
    case '0':return 0x3a33ae62eULL;case '1':return 0x11842108eULL;
    case '2':return 0x3a211111fULL;case '3':return 0x78217043eULL;
    case '4':return 0x8ca97c42ULL; case '5':return 0x7e10f043eULL;
    case '6':return 0x3a10f462eULL;case '7':return 0x7c2222108ULL;
    case '8':return 0x3a317462eULL;case '9':return 0x3a317842eULL;
    case 'A':return 0x3a31fc631ULL;case 'B':return 0x7a31f463eULL;
    case 'C':return 0x3e108420fULL;case 'D':return 0x7a318c63eULL;
    case 'E':return 0x7e10f421fULL;case 'F':return 0x7e10f4210ULL;
    case 'G':return 0x3e10bc62fULL;case 'H':return 0x4631fc631ULL;
    case 'O':return 0x3a318c62eULL;case 'R':return 0x7a31f5251ULL;
    case 'W':return 0x4631ad6aaULL;case '#':return 0x295f57d4aULL;
    case '-':return 0x0000f8000ULL;case '>':return 0x410411110ULL;
    default:return 0;}
}
static char ui[10][40];
static char pattern_grid[12][40];
static uint8_t pattern_grid_selected_line,pattern_grid_selected_channel;
static uint8_t rendered_page,rendered_loaded;
static int8_t display_scope[4][48];
static char previous_ui[10][40],previous_grid[12][40];
static int8_t previous_scope[4][48];
static uint8_t previous_page=255,previous_loaded,previous_selected_line,previous_selected_channel;
static uint32_t dirty_strips;
static void display_name(char *out,const uint8_t *raw,unsigned n,unsigned limit){
    unsigned i;
    for(i=0;i<n && i<limit;i++){
        unsigned c=raw[i];if(!c)break;
        if(c>='a' && c<='z')c-=32;
        out[i]=(c>='A' && c<='Z') || (c>='0' && c<='9') || c==' ' || c=='-' || c=='.'?(char)c:' ';
    }
    while(i && out[i-1]==' ')i--;
    out[i]=0;
}
static void ui_text(void){
    unsigned i,view_order=control.running?player.order:editor.order;
    unsigned view_row=control.running?player.row:editor.row;
    unsigned pattern=song.loaded?song.bytes[952+view_order]:0;
    rendered_page=page;rendered_loaded=song.loaded;
    for(i=0;i<4;i++){
        unsigned x;
        for(x=0;x<48;x++)display_scope[i][x]=(control.running || player.preview_active)?
            player.scope[i][(player.scope_head+x)%48]:0;
    }
    if(page==1){
        unsigned first=library.selected>3?library.selected-3:0;
        snprintf(ui[0],40,"FILES FROM USB PC");
        snprintf(ui[1],40,"%02u FILES %s",library.count,library.pending?"LOADING":library.failed?"ERROR":library.ready?"READY":"CONNECT");
        for(i=0;i<7;i++){
            unsigned index=first+i;
            if(index<library.count)snprintf(ui[i+2],40,"%c%02u %.16s",index==library.selected?'>':' ',index+1,library.entries[index].name);
            else ui[i+2][0]=0;
        }
        snprintf(ui[9],40,"PLAY LOAD HOME BACK");return;
    }
    if(page==2){
        unsigned first=editor.sample>3?editor.sample-3:1;
        if(first>25)first=25;
        snprintf(ui[0],40,"SAMPLES IN MOD");
        snprintf(ui[1],40,"SELECT S%02u",editor.sample);
        for(i=0;i<7;i++){
            unsigned index=first+i,h=20u+(index-1u)*30u;char name[17];
            if(index<=31 && song.loaded){
                display_name(name,song.bytes+h,22,16);
                snprintf(ui[i+2],40,"%c%02u %.16s",index==editor.sample?'>':' ',index,name);
            }else ui[i+2][0]=0;
        }
        snprintf(ui[9],40,"HOME BACK");return;
    }
    snprintf(ui[0],40,"%c%02X/%02X:%02X %s S%02X%c",control.running?'>':'-',
        view_order,pattern,view_row,mix_stereo?"ST":"SP",editor.sample,
        editor.edit?'W':'E');
    ui[1][0]=0; /* Four live scopes own this band in the pattern view. */
    if(!song.loaded){snprintf(ui[2],40,"USB LOAD OR MOD NEW");for(i=3;i<10;i++)ui[i][0]=0;return;}
    memcpy(pattern_grid[0],"ROW CH1      CH2      CH3      CH4     ",40);
    pattern_grid_selected_line=0;
    pattern_grid_selected_channel=editor.channel;
    for(i=0;i<11;i++){
        unsigned row=((view_row>3?view_row-3:0)+i)&63,ch,k,pos;
        pos=(unsigned)snprintf(pattern_grid[i+1],40,"%c%02X ",row==view_row?'>':' ',row);
        if(row==view_row)pattern_grid_selected_line=(uint8_t)(i+1);
        for(ch=0;ch<4;ch++){
            fm1_mod_cell cell={0,0,0,0};char formatted[12];
            static const uint8_t compact_index[8]={0,1,2,4,5,7,8,9};
            fm1_mod_cell_get(&song,pattern,row,ch,&cell);
            fm1_mod_format_cell(cell,formatted);
            for(k=0;k<8;k++)pattern_grid[i+1][pos++]=formatted[compact_index[k]];
            if(ch<3)pattern_grid[i+1][pos++]=' ';
        }
        pattern_grid[i+1][pos]=0;
    }
    if(control.save_hint)snprintf(ui[1],40,"USB SAVE TO COMPUTER");
}
static void ui_refresh(void){
    unsigned i;
    uint8_t old_page=rendered_page,old_loaded=rendered_loaded;
    ui_text();
    if(previous_page==255 || old_page!=rendered_page || old_loaded!=rendered_loaded)
        dirty_strips=0x3fffffffu;
    else if(rendered_page==0 && rendered_loaded){
        for(i=0;i<2;i++)if(strcmp(ui[i],previous_ui[i]))dirty_strips|=7u<<(3*i);
        for(i=0;i<12;i++)if(strcmp(pattern_grid[i],previous_grid[i]) ||
            (i && (previous_selected_line!=pattern_grid_selected_line ||
                   previous_selected_channel!=pattern_grid_selected_channel) &&
             ((i==previous_selected_line && previous_selected_channel<4) ||
              (i==pattern_grid_selected_line && pattern_grid_selected_channel<4))))
            dirty_strips|=3u<<(6+2*i);
    }else for(i=0;i<10;i++)if(strcmp(ui[i],previous_ui[i]))dirty_strips|=7u<<(3*i);
    for(i=0;i<10;i++)strcpy(previous_ui[i],ui[i]);
    if(rendered_page==0 && rendered_loaded)
        for(i=0;i<12;i++)strcpy(previous_grid[i],pattern_grid[i]);
    if(rendered_page==0 && rendered_loaded && memcmp(display_scope,previous_scope,sizeof(display_scope)))
        dirty_strips|=7u<<3;
    memcpy(previous_scope,display_scope,sizeof(display_scope));
    previous_page=rendered_page;previous_loaded=rendered_loaded;
    previous_selected_line=pattern_grid_selected_line;
    previous_selected_channel=pattern_grid_selected_channel;
}
static void render_strip(unsigned y){
    unsigned dy,x,count=240-y<UI_STRIP_ROWS?240-y:UI_STRIP_ROWS;
    for(dy=0;dy<count;dy++){
        unsigned physical=y+dy,line=physical/24,gy=(physical%24)/3;
        unsigned grid_line=physical>=48?(physical-48)/16:0;
        unsigned grid_gy=physical>=48?((physical-48)%16)/2:0;
        uint8_t row_glyphs[40]={0};unsigned character;
        size_t text_length=rendered_page==0 && rendered_loaded && physical>=48?
            (grid_line<12?strlen(pattern_grid[grid_line]):0):(line<10?strlen(ui[line]):0);
        if(rendered_page==0 && rendered_loaded && physical>=48 && grid_line<12 && grid_gy<7){
            for(character=0;character<text_length && character<40;character++)
                row_glyphs[character]=(uint8_t)((grid_glyph(pattern_grid[grid_line][character])>>
                    (30-grid_gy*5))&31u);
        }else if(physical<24 || rendered_page!=0 || !rendered_loaded){
            if(line<10 && gy<5)for(character=0;character<text_length && character<20;character++)
                row_glyphs[character]=(uint8_t)((glyph(ui[line][character])>>(12-gy*3))&7u);
        }
        for(x=0;x<240;x++){
            unsigned gx;uint16_t color=0x0841;
            if(rendered_page==0 && rendered_loaded && physical>=24 && physical<48){
                unsigned ch=x/60,local=x%60;
                color=(local==0 || local==59 || physical==24 || physical==47)?0x6b4d:0x0841;
                if(local>=6 && local<54){
                    int sample=display_scope[ch][local-6];
                    int trace=36-(sample*9)/64;
                    if(physical>=27 && physical<=45 &&
                       (int)physical>=trace-1 && (int)physical<=trace+1)
                        color=ch&1?0xffe0:0x07ff;
                }else if(local>=1 && local<7 && physical>=31 && physical<41){
                    unsigned gx=(local-1)/2,gy=(physical-31)/2;
                    if(gx<3 && gy<5 && (glyph((char)('1'+ch))&(1u<<(14-gy*3-gx))))color=0xffff;
                }
            }else if(rendered_page==0 && rendered_loaded && physical>=48){
                unsigned selected,active;
                gx=x%6;character=x/6;
                active=grid_line==pattern_grid_selected_line && grid_line>0;
                selected=active &&
                         character>=4+pattern_grid_selected_channel*9 &&
                         character<12+pattern_grid_selected_channel*9;
                if(active)color=selected?0x632c:0x2945;
                if(grid_line<12 && grid_gy<7 && gx<5 && character<text_length &&
                   (row_glyphs[character]&(1u<<(4-gx))))
                    color=active || grid_line==0?0xffe0:0x07ff;
            }else{
                gx=(x%12)/3;character=x/12;
                if(line<10 && gy<5 && gx<3 && character<text_length &&
                   (row_glyphs[character]&(1u<<(2-gx))))
                    color=line==1?0xffe0:0x07ff;
            }
            row_pixels[2*(dy*240+x)]=(uint8_t)(color>>8);
            row_pixels[2*(dy*240+x)+1]=(uint8_t)color;
        }
    }
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
    if(page==1){
        if(slot==8)page=0;
        else if(slot==12)fm1_mod_library_pick(&library);
        else if(slot==4 && song.loaded)page=2;
        release(&control_lock,f);return;
    }
    if(page==2){
        if(slot==8 || slot==4 || slot==12)page=0;
        else if(slot==6){page=0;editor.edit=0;}
        release(&control_lock,f);return;
    }
    if(slot!=9)control.save_hint=0;
    if(slot==12){if(!control.request && !transfer.exporting && !transfer.active)
        request(0,control.running?FM1_MOD_REQUEST_STOP:FM1_MOD_REQUEST_PLAY);}
    else if(slot==6){editor.edit=0;}
    else if(slot==4){if(editor.edit)editor.sample_mode^=1;else if(song.loaded)page=2;}
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
        if(!editor.edit && control.running){editor.order=player.order;editor.row=player.row;}
        editor.edit^=1;
    }
    else if(slot>=14 && slot<41 && !transfer.exporting && !transfer.active){
        int midi=53+(int)slot-14+(int)editor.octave*12;
        if(midi>=36 && midi<=71 && song.loaded){
            if(!fm1_mod_preview_start(&player,&song,editor.sample,(unsigned)midi,editor.channel))
                player.preview_key=(uint8_t)slot;
            if(editor.edit){
                if(control.running){editor.order=player.order;editor.row=player.row;}
                fm1_mod_editor_note(&editor,(unsigned)midi);
            }
        }
    }
    release(&control_lock,f);
}
static void input_key_release(unsigned slot){
    unsigned f=take(&control_lock);
    if(slot>=14 && slot<41 && player.preview_active && player.preview_key==slot)
        fm1_mod_preview_stop(&player);
    release(&control_lock,f);
}
static void select_sample(int step){
    int n=(int)editor.sample+step;
    if(n<1)n=31;if(n>31)n=1;
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
        editor.order=player.order;editor.row=player.row;
    }
    pattern=song.bytes[952+editor.order];
    if(id==0)fm1_mod_editor_move(&editor,0,step,0); /* SELECT: track */
    else if(id==1)fm1_mod_editor_move(&editor,0,0,step); /* ALGORITHM: order */
    else if(id==2)fm1_mod_editor_move(&editor,step,0,0); /* KNOB1: row */
    else if(id==3){int n=(int)editor.step+step;if(n<1)n=16;if(n>16)n=1;editor.step=(uint8_t)n;}
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
                uint64_t up=stable&~candidate;
                down=candidate&~stable;stable=candidate;
                for(i=0;i<41;i++)if((up>>i)&1u)input_key_release(i);
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
