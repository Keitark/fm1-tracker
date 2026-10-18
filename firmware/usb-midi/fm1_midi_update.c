/* SPDX-License-Identifier: Apache-2.0
 * Independent adapter to the exact pinned JieLi active-update API. It neither
 * implements a raw NOR writer nor enables the incompatible dual-bank API. */
#ifdef FM1_MIDI_UPDATE_HOST
#include "tests/update_fake.h"
#else
#include "app_config.h"
#include "system/includes.h"
#include "os/os_api.h"
#include "system/sys_time.h"
#include "update.h"
#include "update_loader_download.h"
#include "asm/sfc_norflash_api.h"
#endif
#include "fm1_midi_update.h"
#include "fm1_sha256.h"
#include <string.h>
#ifndef FM1_MIDI_LOADER_COMPATIBLE
#define FM1_MIDI_LOADER_COMPATIBLE 0
#endif
#ifndef FM1_MIDI_IDENTITY
#define FM1_MIDI_IDENTITY "FM1_FORGE_001"
#endif
_Static_assert(sizeof(FM1_MIDI_IDENTITY)<=28,"MIDI identity must fit 27 bytes");
const uint8_t fm1_midi_install_enabled=FM1_MIDI_LOADER_COMPATIBLE?1:0;
/* A true value is an offline loader/package compatibility assertion only;
 * separate exact-device bench acceptance remains mandatory. */
enum { IDLE,PREPARING,ARMED,UPDATING,FAULT };
enum { INVALID=1,BUSY,BLOCKED,TIMEOUT,SESSION,SDK_ERROR,RANGE,BASELINE,BASELINE_IO };
static struct {
    fm1_midi_send_fn send;fm1_midi_quiesce_fn stop;fm1_midi_idle_fn idle;void *ctx;
    OS_SEM sem;
    volatile unsigned state,error,epoch,armed_epoch,started,request,queued,received,token;
    uint32_t now,deadline,size,position,address;
    unsigned length;
    uint8_t target[32],read[512],header[8];
    uint8_t baseline[32],baseline_buffer[1024];
    fm1_sha256 baseline_hash;
    uint32_t baseline_at,baseline_started;
    unsigned hashing;
} u;
static unsigned take(void){unsigned f;local_irq_save(f);return f;}
static void release(unsigned f){local_irq_restore(f);}
static uint32_t get32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void put32(uint8_t *p,uint32_t n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);p[2]=(uint8_t)(n>>16);p[3]=(uint8_t)(n>>24);}
static int send(unsigned cmd,const uint8_t *p,unsigned n){return u.send && u.send(u.ctx,cmd,p,n);}
static void ack(unsigned status){uint8_t b=(uint8_t)status;(void)send(0x32,&b,1);}
static int elapsed(uint32_t since,unsigned ms){return (uint32_t)(u.now-since)>=ms;}
static void fault(unsigned error){unsigned f=take();u.error=error;u.state=FAULT;u.request=0;u.queued=0;release(f);os_sem_post(&u.sem);}
void fm1_midi_update_reset(void){
    unsigned f=take();++u.epoch;
    if(u.state==UPDATING || u.state==FAULT){u.error=u.error?u.error:SESSION;u.state=FAULT;}
    else {u.error=0;u.state=IDLE;}
    u.request=u.queued=u.received=u.hashing=0;u.baseline_at=0;release(f);os_sem_post(&u.sem);
}
void fm1_midi_update_init(fm1_midi_send_fn tx,fm1_midi_quiesce_fn stop,fm1_midi_idle_fn idle,void *ctx){
    memset(&u,0,sizeof(u));u.send=tx;u.stop=stop;u.idle=idle;u.ctx=ctx;os_sem_create(&u.sem,0);
}
unsigned fm1_midi_update_busy(void){return u.state!=IDLE;}
/* Reader executes on the SDK's registered update task. The USB task alone
 * sends requests and decodes incoming messages; neither path waits in IRQ. */
static unsigned read_at(uint32_t address,uint8_t *out,unsigned n){
    unsigned f,epoch,ok;
    if(!out || !n || n>512 ||
       (address!=0xe0000000u && (address>u.size || n>u.size-address)))return 0;
    f=take();
    if(u.state!=UPDATING || u.request){release(f);return 0;}
    epoch=u.epoch;u.address=address;u.length=n;u.received=0;
    u.token=(u.token%255u)+1u;u.header[0]=(uint8_t)u.token;put32(u.header+1,address);
    u.header[5]=(uint8_t)n;u.header[6]=(uint8_t)(n>>8);u.header[7]=0;
    u.request=1;u.queued=1;release(f);
    /* A stale semaphore count is harmless: recheck the exact epoch and reply.
     * Each request has a bounded 1000 ms wait, without automatic retries. */
    for(ok=0;ok<100;ok++){
        os_sem_pend(&u.sem,1);
        f=take();
        if(u.epoch!=epoch || u.state!=UPDATING || !u.request){release(f);return 0;}
        if(u.received){memcpy(out,u.read,n);u.request=0;u.queued=0;release(f);return n;}
        release(f);
    }
    fault(TIMEOUT);return 0;
}
static u16 file_open(void){u.position=0;return 1;}
static u16 file_read(void *fp,u8 *out,u16 n){
    unsigned done=0,k;(void)fp;
    if(!out || !n || u.position>u.size || n>u.size-u.position)return 0;
    while(done<n){k=n-done;if(k>512)k=512;if(read_at(u.position+done,out+done,k)!=k)return 0;done+=k;}
    u.position+=done;return (u16)done;
}
static int file_seek(void *fp,u8 type,u32 offset){
    uint32_t pos;(void)fp;
    if(type==0)pos=offset;
    else if(type==1){if(offset>u.size-u.position)return -1;pos=u.position+offset;}
    else return -1;
    if(pos>u.size)return -1;u.position=pos;return 0;
}
static u16 file_stop(u8 error){if(error)fault(SDK_ERROR);return 0;}
static int file_size(void *priv,u32 size){(void)priv;return size<=u.size?0:-1;}
static void private_fill(UPDATA_PARM *p){
    /* The caller never supplies boot/config addresses or executable code.
     * Final handoff identity is filled after the exact loader audit. */
    memset(p->file_path,0,sizeof(p->file_path));
}
static void before_reset(int type){
    extern void clr_update_ram_info(void);
    (void)type;
    /* The SDK has now written retained parameters. A concurrent USB reset
     * cancels them rather than arming an unrelated later reset. */
    if(u.state!=UPDATING || u.epoch!=u.armed_epoch){clr_update_ram_info();fault(SESSION);return;}
    cpu_reset();
}
static void state_changed(int type,u32 state,void *priv){
    static const uint8_t expected[8]={'s','u','c','c','e','s','s',0};
    uint8_t reply[8];
    update_ret_code_t *result=(update_ret_code_t *)priv;
    if(state==UPDATE_CH_INIT){u.started=1;return;}
    if(state!=UPDATE_CH_EXIT)return;
    if(u.state!=UPDATING || !result || result->stu || result->err_code){fault(SDK_ERROR);return;}
    /* A missing/incorrect final host receipt must never leave valid retained
     * boot parameters behind. Complete the handshake before asking the SDK
     * to arm its loader and invoke the immediate guarded reset callback. */
    if(read_at(0xe0000000u,reply,sizeof(reply))!=sizeof(reply)){fault(TIMEOUT);return;}
    if(memcmp(reply,expected,sizeof(reply))){fault(INVALID);return;}
    update_mode_api_v2((UPDATA_TYPE)type,private_fill,before_reset);
}
static const update_op_api_t ops={0,file_open,file_read,file_seek,file_stop,file_size,0};
void fm1_midi_update_message(void *unused,unsigned cmd,const uint8_t *p,unsigned n){
    unsigned i,f;uint8_t reply[47];(void)unused;
    if(!p && n)return;
    if(cmd==0x11){
        if(n)return;memset(reply,0,27);memcpy(reply,FM1_MIDI_IDENTITY,sizeof(FM1_MIDI_IDENTITY)-1);(void)send(cmd,reply,27);return;
    }
    if(cmd==0x31){
        if(n)return;memcpy(reply,"FMC1",4);reply[4]=1;reply[5]=8;reply[6]=(uint8_t)u.state;
        reply[7]=FM1_MIDI_LOADER_COMPATIBLE?1:0;reply[8]=0;reply[9]=2;put32(reply+10,1048576);
        memcpy(reply+14,fm1_midi_build_id,32);reply[46]=(uint8_t)(u.error?u.error:(FM1_MIDI_LOADER_COMPATIBLE?0:BLOCKED));
        (void)send(cmd,reply,sizeof(reply));return;
    }
    if(cmd==0x32){
        if(n!=70 || p[0]!=1 || p[1]!=8){ack(INVALID);return;}
        if(!FM1_MIDI_LOADER_COMPATIBLE){ack(BLOCKED);return;}
        if(u.state!=IDLE){ack(BUSY);return;}
        if(!get32(p+2) || get32(p+2)>0x110000u){ack(RANGE);return;}
        for(i=0;i<32 && p[6+i]==0;i++){}if(i==32 || !memcmp(p+6,fm1_midi_build_id,32)){ack(INVALID);return;}
        u.size=get32(p+2);memcpy(u.target,p+6,32);memcpy(u.baseline,p+38,32);u.error=0;u.started=0;u.hashing=0;u.baseline_at=0;
        u.armed_epoch=u.epoch;u.deadline=u.now;u.state=PREPARING;
        if(u.stop)u.stop(u.ctx);return;
    }
    if(cmd==0x100){
        update_mode_info_t info;
        if(n || u.state!=ARMED || u.epoch!=u.armed_epoch || elapsed(u.deadline,5000) || !u.idle || !u.idle(u.ctx))return;
        info.type=USB_HID_UPDATA;info.state_cbk=state_changed;info.p_op_api=&ops;info.task_en=1;
        u.state=UPDATING;u.started=0;u.deadline=u.now;
        if(app_active_update_task_init(&info))fault(SDK_ERROR);return;
    }
    if(cmd==0x30){
        f=take();
        if(u.state==UPDATING && u.request && n==u.length+8u && !memcmp(p,u.header,8)){
            memcpy(u.read,p+8,u.length);u.received=1;release(f);os_sem_post(&u.sem);return;
        }release(f);
    }
}
void fm1_midi_update_poll(uint32_t now){
    u.now=now;
    if(u.state==PREPARING){
        if(u.epoch!=u.armed_epoch){fault(SESSION);return;}
        if(!u.idle || !u.idle(u.ctx)){
            if(u.hashing){u.state=IDLE;u.error=BUSY;ack(BUSY);}
            else if(elapsed(u.deadline,2000)){u.state=IDLE;u.error=BUSY;ack(BUSY);}
            return;
        }
        if(!u.hashing){fm1_sha256_init(&u.baseline_hash);u.baseline_started=now;u.hashing=1;}
        if(elapsed(u.baseline_started,15000)){u.state=IDLE;u.error=TIMEOUT;ack(TIMEOUT);return;}
        /* The exact pinned implementation returns the requested length and
         * ignores its inner driver's status. Only the complete digest match
         * proves these reads match the reviewed unit baseline. Read at most
         * 1024 bytes per foreground turn with every peripheral quiesced. */
        if(norflash_origin_read(u.baseline_buffer,u.baseline_at,sizeof(u.baseline_buffer))!=(int)sizeof(u.baseline_buffer)){
            u.state=IDLE;u.error=BASELINE_IO;ack(BASELINE_IO);return;
        }
        if(u.state!=PREPARING || u.epoch!=u.armed_epoch)return;
        fm1_sha256_update(&u.baseline_hash,u.baseline_buffer,sizeof(u.baseline_buffer));u.baseline_at+=sizeof(u.baseline_buffer);
        if(u.baseline_at==1048576u){
            uint8_t digest[32];fm1_sha256_final(&u.baseline_hash,digest);
            if(memcmp(digest,u.baseline,32)){u.state=IDLE;u.error=BASELINE;ack(BASELINE);return;}
            u.hashing=0;u.state=ARMED;u.deadline=now;ack(0);
        }
    }else if(u.state==ARMED && elapsed(u.deadline,5000)){u.state=IDLE;u.error=TIMEOUT;}
    else if(u.state==UPDATING && !u.started && elapsed(u.deadline,1000))fault(SDK_ERROR);
    if(u.state==UPDATING && u.request && u.queued){uint8_t header[8];unsigned f=take();memcpy(header,u.header,8);release(f);
        if(send(0x30,header,8)){f=take();u.queued=0;release(f);}}
}
