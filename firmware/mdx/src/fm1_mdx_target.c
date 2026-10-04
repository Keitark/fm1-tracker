/* One synth/transport owner task. IRQs only scan inputs and drain PCM. */
#ifdef FM1_MDX_TARGET_HOST
#include "fake_peripherals.h"
#else
#include "app_config.h"
#include "system/includes.h"
#include "system/task.h"
#include "system/sys_time.h"
#include "system/timer.h"
#include "system/spinlock.h"
#include "os/os_api.h"
#include "asm/iis.h"
#include "asm/wdt.h"
#endif
#include "fm1_mdx.h"
#include "fm1_mdx_usb.h"
#include "fm1_wl82_keyscan.h"
#include "fm1_volume.h"
#include "peripheral_logic.h"
#include "peripherals.h"
#include "display_test.h"
#include <stdio.h>
#include <string.h>
extern const unsigned char fm1_demo_mdx[],fm1_demo_pdx[];
extern const size_t fm1_demo_mdx_size,fm1_demo_pdx_size;
static fm1_mdx_player player;
static fm1_mdx_upload upload;
static fm1_wl82_keyscan scanner;
static fm1_volume mdx_volume;
static fm1_encoders encoders;
static fm1_audio_startup envelope;
static spinlock_t control_lock,audio_lock,input_lock;
static struct {unsigned request,a,b,running,available,selected,mutes,frames,error,underruns,shutdown,quiescent;} control;
static int16_t ring[2048][2],block[FM1_MDX_BLOCK*2];
static uint32_t rd,wr,underruns,frames;
static unsigned primed,audio_enabled,scan_enabled,scan_ticks;
static uint8_t row_pixels[480];
static volatile int lcd_error,key_error,audio_error;
static int scan_timer,uploaded_song;
static unsigned take(spinlock_t *l){unsigned f;local_irq_save(f);arch_spin_lock(l);return f;}
static void release(spinlock_t *l,unsigned f){arch_spin_unlock(l);local_irq_restore(f);}
static uint32_t now_us(void *u){(void)u;return timer_get_ms()*1000u;}
static int idle(void *u){(void)u;return control.available && !control.running && !control.request;}
static int request(void *u,unsigned op,unsigned a,unsigned b) {
    (void)u;
    if(!control.available || upload.active || (control.request && op!=FM1_MDX_STOP))return -1;
    if(op>=FM1_MDX_SELECT && !control.running)return -1;
    control.request=op;control.a=a;control.b=b;return 0;
}
static void status(void *u,char *out,size_t n) {
    (void)u;
    snprintf(out,n,"MDX running=%u pending=%u selected=%u mute=%04x frames=%u underruns=%u error=%u upload=%u ready=%u lcd=%d keys=%d audio=%d\n",
             control.running,control.request,control.selected,control.mutes,control.frames,control.underruns,control.error,upload.active,upload.ready,lcd_error,key_error,audio_error);
}
int fm1_mdx_usb_command(const char *s,uint32_t now,fm1_mdx_reply reply,void *ctx) {
    fm1_mdx_usb_io io={0,idle,request,status};int result;unsigned f=take(&control_lock);
    result=fm1_mdx_usb_line(&upload,&io,s,now,reply,ctx);release(&control_lock,f);return result;
}
void fm1_mdx_usb_reset(void){unsigned f=take(&control_lock);fm1_mdx_usb_abort(&upload);release(&control_lock,f);}
void fm1_mdx_usb_tick(uint32_t now){unsigned f=take(&control_lock);fm1_mdx_usb_timeout(&upload,now);release(&control_lock,f);}
void fm1_peripheral_cancel(void){unsigned f=take(&control_lock);if(!control.quiescent){control.request=FM1_MDX_STOP;control.shutdown=1;}release(&control_lock,f);}
void fm1_peripheral_session_cancel(void){fm1_mdx_usb_reset();} /* Standalone playback survives disconnect. */
int fm1_peripheral_idle(void){unsigned f=take(&control_lock);int v=control.quiescent;release(&control_lock,f);return v;}
int fm1_peripheral_request(unsigned command,unsigned generation){unsigned f;int rc;(void)generation;if(command!=1)return -1;f=take(&control_lock);rc=request(0,FM1_MDX_STOP,0,0);release(&control_lock,f);return rc;}
int fm1_peripheral_event(char *out,unsigned n){(void)out;(void)n;return 0;}
void fm1_display_snapshot(unsigned phase,const uint32_t *r){(void)phase;(void)r;}
static void audio_output(void *ctx,u8 *data,int len,u8 ch) {
    unsigned i;int32_t *out=(int32_t *)data;uint32_t available=wr-rd;
    (void)ctx;
    if(!data || len<=0)return;
    if(ch!=3 || len!=512){memset(data,0,(unsigned)len);audio_error=-1;return;}
    if(!primed && available>=1470)primed=1;
    for(i=0;i<64;i++) {
        if(primed && rd!=wr){out[2*i]=(int32_t)ring[rd&2047][0]*256;out[2*i+1]=(int32_t)ring[rd&2047][1]*256;rd++;}
        else {out[2*i]=out[2*i+1]=0;if(primed)underruns++;}
    }
    if(primed && wr==rd)primed=0;
    {unsigned f=take(&input_lock);envelope.target_q7=mdx_volume.valid?mdx_volume.target:0;release(&input_lock,f);}
    fm1_audio_startup_process24(&envelope,out);frames+=64;
}
___interrupt
static void fm1_test_alink_isr(void){unsigned f=take(&audio_lock);if(audio_enabled)iis_irq_handler(0);release(&audio_lock,f);}
___interrupt
static void fm1_mdx_keyscan_isr(void){unsigned f=take(&input_lock);if(scan_enabled)fm1_wl82_keyscan_async_step(&scanner);release(&input_lock,f);}
static void fm1_mdx_scan_tick(void *u) {
    unsigned f=take(&input_lock);(void)u;
    if(scan_enabled){if(!(++scan_ticks&1))fm1_volume_tick(&mdx_volume);fm1_wl82_keyscan_async_kick(&scanner);}
    release(&input_lock,f);
}
static void publish(void) {
    unsigned f,fr,ur;f=take(&audio_lock);fr=frames;ur=underruns;release(&audio_lock,f);
    f=take(&control_lock);control.selected=player.selected;control.mutes=player.mute_mask;control.frames=fr;control.underruns=ur;control.error=(unsigned)player.error;release(&control_lock,f);
}
static void silence(void) {
    unsigned f=take(&audio_lock);rd=wr=0;primed=0;release(&audio_lock,f);
    fm1_mdx_stop(&player);
    f=take(&control_lock);control.running=0;release(&control_lock,f);
}
static void shutdown(void) {
    unsigned f;
    silence();
    if(scan_timer>0)sys_usec_timer_del(scan_timer);
    bit_clr_ie(IRQ_SPI2_IDX,0);f=take(&input_lock);scan_enabled=0;
    if(scanner.running)fm1_wl82_keyscan_stop(&scanner);
    fm1_volume_stop(&mdx_volume);release(&input_lock,f);unrequest_irq(IRQ_SPI2_IDX,0);
    bit_clr_ie(IRQ_ALNK_IDX,0);f=take(&audio_lock);audio_enabled=0;release(&audio_lock,f);
    unrequest_irq(IRQ_ALNK_IDX,0);iis_channel_off(8,0);iis_close(0);
    fm1_display_test_stop();
    f=take(&control_lock);control.available=0;control.quiescent=1;release(&control_lock,f);
    for(;;)os_time_dly(100);
}
static void action(unsigned op,unsigned a,unsigned b) {
    int rc=0;
    if(op==FM1_MDX_STOP){silence();return;}
    if(op==FM1_MDX_DEMO || op==FM1_MDX_PLAY) {
        unsigned f=take(&audio_lock);rd=wr=0;primed=0;release(&audio_lock,f);
        if(op==FM1_MDX_PLAY) {
            uploaded_song=1;
            rc=fm1_mdx_load(&player,upload.bytes+12,upload.mdx_size,upload.bytes+12+upload.mdx_size,upload.pdx_size);
        } else {uploaded_song=0;rc=fm1_mdx_load(&player,fm1_demo_mdx,fm1_demo_mdx_size,fm1_demo_pdx,fm1_demo_pdx_size);}
    } else if(op==FM1_MDX_SELECT)rc=fm1_mdx_select(&player,a);
    else if(op==FM1_MDX_MUTE)rc=fm1_mdx_mute(&player,a,(int)b);
    else if(op==FM1_MDX_NOTE)rc=fm1_mdx_note(&player,a,(int)b);
    if(rc){unsigned f=take(&control_lock);control.error=(unsigned)rc;release(&control_lock,f);}
}
/* Compact original 3x5 font. UI renders one scanline per refill opportunity. */
static uint16_t glyph(char c) {
    static const uint16_t digits[10]={0x7b6f,0x2492,0x73e7,0x73cf,0x5bc9,0x79cf,0x79ef,0x7249,0x7bef,0x7bcf};
    if(c>='0'&&c<='9')return digits[c-'0'];
    switch(c){case 'A':return 0x2bed;case 'D':return 0x6b6e;case 'E':return 0x79e7;case 'F':return 0x79e4;case 'I':return 0x7497;case 'K':return 0x5bad;case 'L':return 0x4927;case 'M':return 0x7fed;case 'N':return 0x7b6d;case 'O':return 0x7b6f;case 'P':return 0x7be4;case 'R':return 0x7bad;case 'S':return 0x79cf;case 'T':return 0x7492;case 'U':return 0x5b6f;case 'X':return 0x5aad;case '-':return 0x01c0;default:return 0;}
}
static char ui[6][40];
static void ui_text(void) {
    unsigned i;snprintf(ui[0],40,"FM1 MDX %s",control.running?"PLAY":"STOP");
    snprintf(ui[1],40,"%s",uploaded_song?"USB RAM":"FLASH DEMO");
    snprintf(ui[2],40,"TRACK %u %s",player.selected+1,(player.mute_mask&(1u<<player.selected))?"KARAOKE":"MDX");
    for(i=0;i<8;i++)ui[3][i]=(player.mute_mask&(1u<<i))?'-':(char)('1'+i);ui[3][8]=0;
    snprintf(ui[4],40,"KNOB 1 TRACK  KEY 2 MUTE");
    snprintf(ui[5],40,"KEY 3 PLAY STOP");
}
static int ui_row(unsigned y) {
    unsigned x,line=y/24,gy=(y%24)/3;
    uint8_t col[4]={0,0,0,239},rows[4]={0,(uint8_t)(40+y),0,(uint8_t)(40+y)};
    /* y+40 can exceed255; set the high bytes explicitly. */
    rows[0]=rows[2]=(uint8_t)((40+y)>>8);
    for(x=0;x<240;x++) {
        unsigned gx=(x%12)/3,character=x/12;uint16_t c=0x0841;
        if(line<6 && gy<5 && gx<3 && character<strlen(ui[line]) && (glyph(ui[line][character])&(1u<<(14-gy*3-gx))))c=line==2?0xffe0:0x07ff;
        row_pixels[2*x]=(uint8_t)(c>>8);row_pixels[2*x+1]=(uint8_t)c;
    }
    {uint8_t cmd=0x2a;if(fm1_display_write(0,&cmd,1)||fm1_display_write(1,col,4))return -1;cmd=0x2b;if(fm1_display_write(0,&cmd,1)||fm1_display_write(1,rows,4))return -1;cmd=0x2c;if(fm1_display_write(0,&cmd,1)||fm1_display_write(1,row_pixels,480))return -1;}
    return 0;
}
__attribute__((noinline,used))
static void fm1_peripheral_task(void *u) {
    struct iis_platform_data pd;unsigned f;uint32_t last_ui=0,last_status=0,changed=0;unsigned row=240;
    uint64_t candidate=0,stable=0;int32_t encoder=0;
    (void)u;memset(&pd,0,sizeof(pd));
    lcd_error=fm1_display_test_init();
    bit_clr_ie(IRQ_SPI2_IDX,0);key_error=fm1_wl82_keyscan_async_start(&scanner,0,now_us);
    if(!key_error) {
        scan_enabled=1;request_irq(IRQ_SPI2_IDX,5,fm1_mdx_keyscan_isr,0);
        fm1_volume_start(&mdx_volume);scan_timer=sys_usec_timer_add(0,fm1_mdx_scan_tick,1000,1,0);
        if(scan_timer<=0){key_error=-1;scan_enabled=0;fm1_volume_stop(&mdx_volume);}
    }
    fm1_audio_startup_reset(&envelope);
    pd.port_sel=IIS_PORTC;pd.channel_out=pd.data_width=8;pd.mclk_output=1;pd.sr_points=128;
    audio_error=iis_open(&pd,0);
    if(!audio_error){iis_set_dec_data_handler(0,audio_output,0);audio_error=iis_set_sample_rate(FM1_MDX_RATE,0);}
    if(!audio_error){audio_enabled=1;request_irq(IRQ_ALNK_IDX,3,fm1_test_alink_isr,0);iis_channel_on(8,0);}
    f=take(&control_lock);control.available=1;control.running=1;release(&control_lock,f);
    action(FM1_MDX_DEMO,0,0);
    for(;;) {
        unsigned op,a,b,running,queued,closing;uint32_t now=timer_get_ms();uint8_t rows[11];int scan_rc;
        f=take(&control_lock);op=control.request;a=control.a;b=control.b;control.request=0;
        if(op==FM1_MDX_PLAY || op==FM1_MDX_DEMO)control.running=1;
        running=control.running;closing=control.shutdown;release(&control_lock,f);
        if(closing)shutdown();
        if(op){action(op,a,b);publish();if(op==FM1_MDX_STOP)running=0;}
        f=take(&audio_lock);queued=wr-rd;release(&audio_lock,f);
        if(running && player.loaded && !audio_error && queued<=2048-FM1_MDX_BLOCK) {
            if(fm1_mdx_render(&player,block,FM1_MDX_BLOCK) || !player.playing){silence();running=0;}
            else {
                unsigned i;f=take(&audio_lock);
                for(i=0;i<FM1_MDX_BLOCK;i++){ring[wr&2047][0]=block[2*i];ring[wr&2047][1]=block[2*i+1];wr++;}
                release(&audio_lock,f);
            }
        }
        f=take(&input_lock);scan_rc=scan_enabled?fm1_wl82_keyscan_async_raw(&scanner,rows):FM1_NES_BUSY;release(&input_lock,f);
        if(!scan_rc) {
            uint64_t keys=fm1_stock_decode_keys(rows),down,up;unsigned i;
            fm1_encoders_sample(&encoders,rows);
            if(keys!=candidate){candidate=keys;changed=now;}
            if((uint32_t)(now-changed)>=10 && candidate!=stable) {
                down=candidate&~stable;up=stable&~candidate;stable=candidate;
                f=take(&control_lock);running=control.running;
                if(!upload.active && !control.request) {
                    if(down&1)request(0,FM1_MDX_SELECT,(player.selected+7)%8,0);
                    else if(down&2)request(0,FM1_MDX_SELECT,(player.selected+1)%8,0);
                    else if(down&4)request(0,FM1_MDX_MUTE,player.selected,!(player.mute_mask&(1u<<player.selected)));
                    else if(down&8)request(0,running?FM1_MDX_STOP:(uploaded_song&&upload.ready?FM1_MDX_PLAY:FM1_MDX_DEMO),0,0);
                }
                release(&control_lock,f);
                if(running){for(i=14;i<41;i++)if((up>>i)&1)fm1_mdx_note(&player,53+i-14,0);for(i=14;i<41;i++)if((down>>i)&1)fm1_mdx_note(&player,53+i-14,1);}
            }
            if(encoders.count[0]!=encoder) {
                int step=encoders.count[0]>encoder?1:-1;encoder=encoders.count[0];
                f=take(&control_lock);if(!upload.active && !control.request)request(0,FM1_MDX_SELECT,(player.selected+8+step)%8,0);release(&control_lock,f);
            }
        } else if(scan_rc!=FM1_NES_BUSY && scan_enabled) {
            key_error=scan_rc;f=take(&input_lock);scan_enabled=0;fm1_wl82_keyscan_stop(&scanner);fm1_volume_stop(&mdx_volume);release(&input_lock,f);
            /* Lost key releases must never leave a manual voice held. */
            for(a=0;a<8;a++)if(player.live_note[a]>=0){fm1_mdx_song_write(&player,8,(uint8_t)a);ym2151_write_reg(&player.opm,8,(int)a);player.live_note[a]=-1;}
        }
        if((uint32_t)(now-last_status)>=250){last_status=now;publish();}
        if(!lcd_error && (queued>=1470 || !running)) {
            if(row==240 && (uint32_t)(now-last_ui)>=500){last_ui=now;ui_text();row=0;}
            if(row<240){lcd_error=ui_row(row++);}
        }
        wdt_clear();if(queued>=1470 || !running || audio_error)os_time_dly(1);
    }
}
int fm1_peripheral_start_task(void){return task_create(fm1_peripheral_task,0,"peripheral");}
