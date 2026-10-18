/* SPDX-License-Identifier: Apache-2.0 */
#include "update_fake.h"
#include "fm1_midi_update.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
const uint8_t fm1_midi_build_id[32]={1};
static update_mode_info_t captured;
static unsigned last_cmd,last_n,tx_count,sdk_starts,param_writes,resets,clears,stops,ready,started,reply_mode,clock_ms;
static unsigned baseline_reads,baseline_mode;
static uint8_t last[520];
static int transmit(void *ctx,unsigned cmd,const uint8_t *p,unsigned n){(void)ctx;last_cmd=cmd;last_n=n;memcpy(last,p,n);tx_count++;return 1;}
static void stop(void *ctx){(void)ctx;stops++;}
static int idle(void *ctx){(void)ctx;return ready;}
int os_sem_create(OS_SEM *s,unsigned n){s->count=n;return 0;}
int os_sem_post(OS_SEM *s){s->count++;return 0;}
int os_sem_pend(OS_SEM *s,unsigned ticks){
    uint8_t response[520];unsigned n,i;uint32_t address;(void)ticks;
    if(s->count)s->count--;
    if(reply_mode==4){fm1_midi_update_reset();return 0;}
    fm1_midi_update_poll(++clock_ms);
    if(last_cmd!=0x30 || last_n!=8 || reply_mode==1)return 0;
    memcpy(response,last,8);n=(unsigned)last[5]|((unsigned)last[6]<<8);address=(uint32_t)last[1]|((uint32_t)last[2]<<8)|((uint32_t)last[3]<<16)|((uint32_t)last[4]<<24);
    if(address==0xe0000000u)memcpy(response+8,"success",8);
    else for(i=0;i<n;i++)response[8+i]=(uint8_t)(address+i);
    if(reply_mode==2)response[0]^=1;
    if(reply_mode==3)response[8]^=1;
    fm1_midi_update_message(0,0x30,response,n+8);return 0;
}
int app_active_update_task_init(update_mode_info_t *p){captured=*p;sdk_starts++;if(started)p->state_cbk(p->type,UPDATE_CH_INIT,0);return 0;}
void update_mode_api_v2(UPDATA_TYPE type,void (*fill)(UPDATA_PARM *),void (*jump)(int)){
    UPDATA_PARM p;memset(&p,0xa5,sizeof(p));param_writes++;fill(&p);CHECK(p.file_path[0]==0);jump(type);
}
void cpu_reset(void){resets++;}
void clr_update_ram_info(void){clears++;}
int norflash_origin_read(u8 *p,u32 at,u32 n){
    unsigned i;CHECK(ready && at==baseline_reads*1024u && n==1024 && at+n<=1048576u);baseline_reads++;
    for(i=0;i<n;i++)p[i]=(uint8_t)(at+i);
    if(baseline_mode==1)p[0]^=1;
    if(baseline_mode==2)return 0;
    if(baseline_mode==3)fm1_midi_update_reset();
    return (int)n;
}
static void init(void){
    memset(&captured,0,sizeof(captured));last_cmd=last_n=tx_count=sdk_starts=param_writes=resets=clears=stops=reply_mode=baseline_reads=baseline_mode=0;ready=started=1;clock_ms=100;
    fm1_midi_update_init(transmit,stop,idle,0);fm1_midi_update_poll(clock_ms);
}
static void request_arm(void){
    /* Independent hashlib oracle for bytes(0..255) repeated to one MiB. */
    static const uint8_t digest[]={0xfb,0xba,0xb2,0x89,0xf7,0xf9,0x4b,0x25,0x73,0x6c,0x58,0xbe,0x46,0xa9,0x94,0xc4,0x41,0xfd,0x02,0x55,0x2c,0xc6,0x02,0x23,0x52,0xe3,0xd8,0x6d,0x2f,0xab,0x7c,0x83};
    uint8_t a[70]={1,8,0,16,0,0};memset(a+6,0x42,32);memcpy(a+38,digest,32);fm1_midi_update_message(0,0x32,a,sizeof(a));
}
static void arm(void){unsigned i;request_arm();for(i=0;i<(ready?1024u:1u);i++){clock_ms+=10;fm1_midi_update_poll(clock_ms);}}
static void begin(void){arm();CHECK(last_cmd==0x32 && last[0]==0 && stops==1);fm1_midi_update_message(0,0x100,0,0);CHECK(sdk_starts==1 && captured.type==USB_HID_UPDATA && captured.task_en==1);}
int main(void){
    uint8_t bytes[512];update_ret_code_t ok={0,0};const update_op_api_t *ops;
    init();fm1_midi_update_reset();fm1_midi_update_message(0,0x11,0,0);CHECK(last_n==27 && !memcmp(last,"FM1_FORGE_001",13));
    fm1_midi_update_message(0,0x31,0,0);CHECK(last_n==47 && !memcmp(last,"FMC1",4) && last[6]==0);
#ifdef FM1_MIDI_LOADER_COMPATIBLE
    CHECK(last[7]==1 && last[46]==0);
    begin();ops=captured.p_op_api;CHECK(ops && ops->f_open()==1 && ops->notify_update_content_size(0,4096)==0 && ops->notify_update_content_size(0,4097)!=0);
    CHECK(baseline_reads==1024);
    CHECK(ops->f_read(0,bytes,512)==512);CHECK(bytes[0]==0 && bytes[255]==255 && bytes[511]==255);
    CHECK(ops->f_seek(0,0,4095)==0 && ops->f_read(0,bytes,2)==0 && ops->f_seek(0,0,4097)!=0 && ops->f_seek(0,2,0)!=0);
    CHECK(ops->f_seek(0,0,256)==0 && ops->f_read(0,bytes,4)==4 && bytes[0]==0 && bytes[3]==3);
    captured.state_cbk(captured.type,UPDATE_CH_EXIT,&ok);CHECK(param_writes==1 && resets==1 && !clears);
    init();begin();reply_mode=2;CHECK(!captured.p_op_api->f_read(0,bytes,1));CHECK(!param_writes && !resets);
    fm1_midi_update_reset();fm1_midi_update_message(0,0x31,0,0);CHECK(last[6]==4 && last[46]!=0);
    init();begin();reply_mode=4;CHECK(!captured.p_op_api->f_read(0,bytes,1));CHECK(!param_writes && !resets);
    init();begin();reply_mode=3;captured.state_cbk(captured.type,UPDATE_CH_EXIT,&ok);CHECK(!param_writes && !resets);
    init();ready=0;arm();CHECK(stops==1 && last_cmd!=0x32);fm1_midi_update_poll(clock_ms+2001);CHECK(last_cmd==0x32 && last[0]!=0 && !sdk_starts);
    CHECK(!baseline_reads);
    init();baseline_mode=1;arm();CHECK(last_cmd==0x32 && last[0]==8 && baseline_reads==1024 && !sdk_starts);fm1_midi_update_message(0,0x100,0,0);CHECK(!sdk_starts);
    init();baseline_mode=2;arm();CHECK(last_cmd==0x32 && last[0]==9 && baseline_reads==1 && !sdk_starts);
    init();baseline_mode=3;request_arm();fm1_midi_update_poll(++clock_ms);fm1_midi_update_message(0,0x100,0,0);CHECK(!sdk_starts && baseline_reads==1);
    init();request_arm();fm1_midi_update_poll(++clock_ms);fm1_midi_update_poll(clock_ms+15001);CHECK(last_cmd==0x32 && last[0]!=0 && baseline_reads==1 && !sdk_starts);
    init();started=0;begin();fm1_midi_update_poll(clock_ms+1001);fm1_midi_update_message(0,0x31,0,0);CHECK(last[6]==4);
    init();arm();fm1_midi_update_poll(clock_ms+5001);fm1_midi_update_message(0,0x100,0,0);CHECK(!sdk_starts);
    init();arm();fm1_midi_update_reset();fm1_midi_update_message(0,0x100,0,0);CHECK(!sdk_starts);
    puts("PASS exact live synthetic baseline SHA before ARM, bounded reads, SDK file ops, bounds, exact replies, epochs, quiescence/deadlines and receipt before retained params");
#else
    (void)bytes;(void)ok;(void)ops;CHECK(last[7]==0 && last[46]!=0);arm();CHECK(last_cmd==0x32 && last[0]!=0 && !stops);fm1_midi_update_message(0,0x100,0,0);CHECK(!sdk_starts && !param_writes && !resets);
    puts("PASS default install guard: capabilities honest, ARM denied, trigger cannot enter SDK or reset");
#endif
    return 0;
}
