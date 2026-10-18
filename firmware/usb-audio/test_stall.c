/* SPDX-License-Identifier: GPL-3.0-only
 * Reused from Keitark/fm1-mdx at 4141057; see licenses/fm1-mdx-GPL-3.0.txt. */
/* Actual target.c + packet.c with a deterministic shared USB controller model.
 * This is not PI32 instruction, DMA bus, Windows driver or OBS emulation.
 * Legacy positive control models only the reviewed SDK busy/deadline loop.
 */
#include "packet_fake.h"
#include "target.c"
#include <stdlib.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"Stall check line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
struct fm1_packet_regs fm1_packet_regs;
static struct usb_device_t device={USB_CONFIGURED};
static uint32_t cdc_dma[64];
static u8 *dma[2];
static unsigned csr[4],irq_enabled=1,irq_depth,csr_reads,doorbells,syncs;
static unsigned audio_received,cdc_received,clipped,hash=2166136261u;
static unsigned inject_busy,rejected_race,cdc_attempts,stale_cdc,restarts;
static volatile unsigned cdc_epoch;
static void (*interrupts[2])(struct usb_device_t *,u32);
static u32 (*handlers[5])(struct usb_device_t *,struct usb_ctrlrequest *);
static void (*resets[5])(struct usb_device_t *,u32);
unsigned fm1_packet_irq_save(void){unsigned old=irq_enabled;irq_enabled=0;return old;}
void fm1_packet_irq_restore(unsigned f){CHECK(!irq_depth);irq_enabled=f;}
void __local_irq_disable(void){irq_enabled=0;irq_depth++;}
void __local_irq_enable(void){CHECK(irq_depth);if(!--irq_depth)irq_enabled=1;}
void __asm_csync(void){CHECK(!irq_enabled&&irq_depth);syncs++;}
void arch_spin_lock(spinlock_t *l){CHECK(!*l&&!irq_enabled);*l=1;}
void arch_spin_unlock(spinlock_t *l){CHECK(*l&&!irq_enabled);*l=0;}
usb_dev usb_device2id(const struct usb_device_t *d){CHECK(d==&device);return 0;}
struct usb_device_t *usb_id2device(usb_dev id){CHECK(!id);return &device;}
void *usb_get_dma_taddr(usb_dev id,unsigned ep){CHECK(!id&&!irq_enabled&&irq_depth);return ep==1?(void *)dma[1]:(void *)cdc_dma;}
u32 usb_read_txcsr(usb_dev id,u32 ep){unsigned v;CHECK(!id&&(ep==1||ep==3));
    /* Force ready->busy between target's early check and helper's commit. */
    if(ep==1&&irq_depth&&inject_busy){csr[1]|=1;inject_busy=0;rejected_race++;}
    __local_irq_disable();v=csr[ep];csr_reads++;__local_irq_enable();return v;
}
void usb_write_txcsr(usb_dev id,u32 ep,u32 v){CHECK(!id&&(ep==1||ep==3));
    if(v&TXCSRP_FlushFIFO){csr[ep]=0;return;}
    CHECK(!irq_enabled&&irq_depth&&!(csr[ep]&1)&&syncs>doorbells);
    CHECK((ep==1?fm1_packet_regs.EP1_CNT:fm1_packet_regs.EP3_CNT)==(ep==1?192:32));
    csr[ep]=v;doorbells++;
}
u32 usb_g_iso_read(usb_dev id,u32 ep,void *p,u32 n,u32 last){CHECK(!id&&ep==1&&!p&&n==192&&!last);return 0;}
void usb_write_rxcsr(usb_dev id,u32 ep,u32 v){CHECK(!id&&ep==1&&(v&RXCSRP_FlushFIFO));}
void usb_clr_intr_txe(usb_dev id,u32 ep){CHECK(!id&&ep==1);}
void usb_clr_intr_rxe(usb_dev id,u32 ep){CHECK(!id&&ep==1);}
void usb_enable_ep(usb_dev id,u32 ep){CHECK(!id&&ep==1);}
void usb_set_intr_txe(usb_dev id,u32 ep){CHECK(!id&&ep==1&&interrupts[1]);}
void usb_set_intr_rxe(usb_dev id,u32 ep){CHECK(!id&&ep==1&&interrupts[0]);}
u32 usb_g_ep_config(usb_dev id,u32 ep,u32 type,u32 ie,u8 *p,u32 n){CHECK(!id&&type==1&&!ie&&n==192&&!((uintptr_t)p%64));dma[ep>>7]=p;return 0;}
u32 usb_g_set_intr_hander(usb_dev id,u32 ep,void (*h)(struct usb_device_t *,u32)){CHECK(!id&&(ep==1||ep==0x81));interrupts[ep>>7]=h;return 0;}
u32 usb_set_interface_hander(usb_dev id,u32 i,u32 (*h)(struct usb_device_t *,struct usb_ctrlrequest *)){CHECK(!id&&i>=2&&i<5);handlers[i]=h;return i;}
u32 usb_set_reset_hander(usb_dev id,u32 i,void (*h)(struct usb_device_t *,u32)){CHECK(!id&&i>=2&&i<5);resets[i]=h;return i;}
void usb_set_setup_phase(struct usb_device_t *d,u8 p){CHECK(d==&device&&p==0);}
void *usb_get_setup_buffer(const struct usb_device_t *d){CHECK(d==&device);return device.setup;}
u8 *usb_set_data_payload(struct usb_device_t *d,struct usb_ctrlrequest *r,const void *p,u32 n){CHECK(d==&device&&p==device.setup&&n==r->wLength&&n<=2);return device.setup;}
static void set_stream(unsigned on){struct usb_ctrlrequest r={1,11,(uint16_t)on,4,0};handlers[4](&device,&r);}
static void consume(unsigned ep){unsigned i,n;u8 *p;
    if(!(csr[ep]&1))return;n=ep==1?fm1_packet_regs.EP1_CNT:fm1_packet_regs.EP3_CNT;
    CHECK(n==(ep==1?192:32));p=ep==1?dma[1]:(u8 *)cdc_dma;
    if(ep==1){for(i=0;i<n;i+=2){int v=(int16_t)(p[i]|((unsigned)p[i+1]<<8));CHECK(abs(v)<=12000);clipped+=abs(v)>=32767;}
        for(i=0;i<n;i++)hash=(hash^p[i])*16777619u;audio_received++;
    }else{CHECK(!memcmp(p,"# heartbeat",11));cdc_received++;}
    csr[ep]&=~1u;
}
/* Reconstructed control-flow condition, not execution of the SDK binary.
 * Its busy loop compares jiffies against deadline and has no independent
 * iteration bound. Watchdog makes a frozen-clock positive control terminate.
 */
static unsigned legacy_wait(unsigned frozen,unsigned *polls){
    uint32_t jiffies=7500,deadline=jiffies+202;unsigned i;
    for(i=0;i<100000;i++){
        if((int32_t)(deadline-jiffies)<0){*polls=i;return 1;}
        /* TxPktRdy remains1, no SENTSTALL/UNDERRUN escape flags. */
        if(!frozen&&i%100==99)jiffies++;
    }
    *polls=i;return 0; /* watchdog: would remain in the SDK polling loop */
}
int main(int argc,char **argv){
    unsigned stress=argc==2&&!strcmp(argv[1],"stress"),elapsed,phase=0,dac_units=0;
    unsigned checks=0,heartbeats=0,clock_ticks=0,reads_before,accepted_before;
    unsigned audio_pause_ms=0,bus_pause_ms=0,recovery_audio=0,recovery_cdc=0;
    unsigned polls_live,polls_frozen,cdc_at_audio_pause=0;u8 desc[173],message[32]={0},held[192];u32 itf=2;
    CHECK(argc==1||stress);CHECK(legacy_wait(0,&polls_live));CHECK(!legacy_wait(1,&polls_frozen));
    printf("{\"legacy_model\":true,\"live_clock_timeout_polls\":%u,\"frozen_clock_watchdog_polls\":%u,\"conditional_stall\":true}\n",polls_live,polls_frozen);
    memcpy(message,"# heartbeat",11);fm1_usb_audio_init();CHECK(fm1_uac_desc_config(0,desc,&itf)==173);
    capture_epoch=0xfffffffeu;cdc_epoch=0xfffffffeu;set_stream(1);
    for(elapsed=1;elapsed<=600000;elapsed++){
        unsigned audio_pause=stress&&((elapsed>=75000&&elapsed<80000)||(elapsed%13000<350&&elapsed>13000));
        unsigned bus_pause=stress&&elapsed>=160000&&elapsed<165000;
        if(!(stress&&elapsed>=75000&&elapsed<80000)&&elapsed%10==0)clock_ticks++;
        if(stress&&elapsed%61000==0){set_stream(0);set_stream(1);restarts++;}
        if(stress&&elapsed%131000==0){device.bDeviceStates=0;resets[2](&device,2);++cdc_epoch;csr[3]=0;
            device.bDeviceStates=USB_CONFIGURED;set_stream(1);restarts++;}
        if(!audio_pause&&!bus_pause)consume(1);else audio_pause_ms++;
        if(!bus_pause)consume(3);else bus_pause_ms++;
        if(elapsed==75000)cdc_at_audio_pause=cdc_received;
        if(elapsed==80000)CHECK(cdc_received>cdc_at_audio_pause+400);
        dac_units+=44100;
        while(dac_units>=64000){int32_t pcm[128];unsigned i;dac_units-=64000;
            for(i=0;i<64;i++,phase++){pcm[2*i]=((int)(phase%241)-120)*100*256;pcm[2*i+1]=((int)(phase%151)-75)*80*256;}
            fm1_usb_audio_dac(pcm,64);
        }
        if(stress&&elapsed%47000==0&&!(csr[1]&1))inject_busy=1;
        memcpy(held,dma[1],192);accepted_before=doorbells;reads_before=csr_reads;
        if(interrupts[1])interrupts[1](&device,1);
        CHECK(csr_reads-reads_before<=2);if(doorbells==accepted_before)CHECK(!memcmp(held,dma[1],192));
        if(elapsed%10==0){unsigned expected=cdc_epoch;checks++;cdc_attempts++;
            if(stress&&elapsed%37000==0){++cdc_epoch;stale_cdc++;}
            if(fm1_usb_packet_write(0,3,message,32,&cdc_epoch,expected))heartbeats++;
        }
        CHECK(irq_enabled&&!irq_depth&&!lock);
        if(elapsed==165010){recovery_audio=audio_received;recovery_cdc=cdc_received;}
        if(elapsed==165020){CHECK(audio_received>recovery_audio&&cdc_received>recovery_cdc);}
    }
    consume(1);consume(3);CHECK(checks==60000&&heartbeats&&audio_received&&cdc_received&&!clipped);
    if(!stress)CHECK(!bridge.capture.underruns&&!bridge.capture.overruns&&audio_received==600001&&cdc_received==60000);
    else CHECK(restarts&&rejected_race&&stale_cdc&&bridge.capture.overruns&&capture_epoch<100&&cdc_epoch<100);
    printf("{\"actual_target_and_packet\":true,\"virtual_seconds\":600,\"stress\":%u,\"audio_received\":%u,\"cdc_received\":%u,\"service_checks\":%u,\"clock_ticks\":%u,\"stream_transitions\":%u,\"ready_busy_races\":%u,\"stale_cdc\":%u,\"audio_pause_ms\":%u,\"bus_pause_ms\":%u,\"capture_under\":%u,\"capture_over\":%u,\"clipped\":%u,\"hash\":%u,\"persistent_stall\":false}\n",
        stress,audio_received,cdc_received,checks,clock_ticks,restarts,rejected_race,stale_cdc,audio_pause_ms,bus_pause_ms,bridge.capture.underruns,bridge.capture.overruns,clipped,hash);
    return 0;
}
