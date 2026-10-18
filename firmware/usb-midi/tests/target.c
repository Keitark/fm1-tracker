/* SPDX-License-Identifier: Apache-2.0 */
#include "target_fake.h"
#include "fm1_midi.h"
#include "fm1_midi_target.h"
#include "fm1_midi_update.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
const uint8_t fm1_midi_build_id[32]={1};
struct midi_usb_regs midi_usb_regs;
static struct usb_device_t device={USB_CONFIGURED};
static u32 (*setups[8])(struct usb_device_t *,struct usb_ctrlrequest *);
static void (*resets[8])(struct usb_device_t *,u32),(*rx_handler)(struct usb_device_t *,u32);
static unsigned out_n,configured_in,configured_out,busy,packets,stalls,nesting,sdk_starts,reply_calls,reply_cmd;
static u8 out[64],setup_buffer[64],*in_dma;static fm1_midi_parser host_parser;
unsigned usb_device2id(struct usb_device_t *p){CHECK(p==&device);return 0;}
struct usb_device_t *usb_id2device(usb_dev id){CHECK(!id);return &device;}
u32 usb_g_bulk_read(usb_dev id,unsigned ep,void *p,u32 n,unsigned blocking){CHECK(!id && ep==3 && n==64 && !blocking);memcpy(p,out,out_n);return out_n;}
void usb_clr_intr_rxe(usb_dev id,unsigned ep){CHECK(!id && ep==3);}
void usb_g_set_intr_hander(usb_dev id,unsigned ep,void (*fn)(struct usb_device_t *,u32)){CHECK(!id && ep==3);rx_handler=fn;}
void usb_g_ep_config(usb_dev id,unsigned ep,unsigned type,unsigned intr,void *dma,unsigned n){CHECK(!id && type==2 && n==64 && !((uintptr_t)dma&63));if(ep==0x84){configured_in++;in_dma=dma;CHECK(!intr);}else{CHECK(ep==3 && intr==1);configured_out++;}}
void usb_enable_ep(usb_dev id,unsigned ep){CHECK(!id && ep==4);}
u8 *usb_get_setup_buffer(struct usb_device_t *p){CHECK(p==&device);return setup_buffer;}
void usb_set_setup_phase(struct usb_device_t *p,unsigned phase){CHECK(p==&device);if(phase==USB_EP0_SET_STALL)stalls++;else CHECK(!phase);}
void usb_set_data_payload(struct usb_device_t *p,struct usb_ctrlrequest *r,u8 *b,unsigned n){CHECK(p==&device && r->bRequest==10 && b==setup_buffer && n==1 && b[0]==0);}
u32 usb_set_interface_hander(usb_dev id,unsigned itf,u32 (*fn)(struct usb_device_t *,struct usb_ctrlrequest *)){CHECK(!id && itf<8);setups[itf]=fn;return itf;}
u32 usb_set_reset_hander(usb_dev id,unsigned itf,void (*fn)(struct usb_device_t *,u32)){CHECK(!id && itf<8);resets[itf]=fn;return itf;}
unsigned usb_read_txcsr(usb_dev id,unsigned ep){CHECK(!id && ep==4);return busy;}
void *usb_get_dma_taddr(usb_dev id,unsigned ep){CHECK(!id && ep==4);return in_dma;}
void usb_write_txcsr(usb_dev id,unsigned ep,unsigned csr){CHECK(!id && ep==4 && csr&1 && nesting==1);CHECK(midi_usb_regs.EP4_CNT && midi_usb_regs.EP4_CNT<=64 && !(midi_usb_regs.EP4_CNT&3));packets++;fm1_midi_events(&host_parser,in_dma,midi_usb_regs.EP4_CNT);}
void __local_irq_disable(void){nesting++;}void __local_irq_enable(void){CHECK(nesting==1);nesting--;}void __asm_csync(void){}
int os_sem_create(OS_SEM *s,unsigned n){s->count=n;return 0;}int os_sem_post(OS_SEM *s){s->count++;return 0;}int os_sem_pend(OS_SEM *s,unsigned n){(void)s;(void)n;return 0;}
int app_active_update_task_init(update_mode_info_t *p){(void)p;sdk_starts++;return -1;}
void update_mode_api_v2(UPDATA_TYPE t,void (*f)(UPDATA_PARM *),void (*j)(int)){(void)t;(void)f;(void)j;CHECK(0);}
void cpu_reset(void){CHECK(0);}void clr_update_ram_info(void){CHECK(0);}
int norflash_origin_read(u8 *p,u32 at,u32 n){(void)p;(void)at;(void)n;CHECK(0);return 0;}
static void reply(void *ctx,unsigned cmd,const uint8_t *p,unsigned n){(void)ctx;reply_calls++;reply_cmd=cmd;if(cmd==0x31)CHECK(n==47 && !memcmp(p,"FMC1",4) && !p[7] && p[46]);}
static void request(unsigned cmd){uint8_t frame[80],events[108];unsigned bytes,i,k;bytes=fm1_midi_frame(cmd,0,0,frame,sizeof(frame));bytes=fm1_midi_event_encode(frame,bytes,events,sizeof(events));for(i=0;i<bytes;i+=k){k=bytes-i;if(k>64)k=64;memcpy(out,events+i,k);out_n=k;rx_handler(&device,3);}}
int main(void){
    uint8_t desc[100];u32 itf=2;struct usb_ctrlrequest r={0x81,10,0,2,1};unsigned before,i;
    fm1_usb_midi_init();fm1_midi_parser_init(&host_parser,reply,0);CHECK(fm1_midi_desc_config(0,desc,&itf)==100 && itf==4 && setups[2] && setups[3]);resets[2](&device,2);CHECK(rx_handler && configured_in==1 && configured_out==1);
    fm1_usb_midi_poll(0);setups[2](&device,&r);CHECK(!stalls);r.bRequestType=0xa1;setups[2](&device,&r);CHECK(stalls==1);r.bRequestType=0x81;r.wIndex=4;setups[2](&device,&r);CHECK(stalls==2);
    request(0x31);fm1_usb_midi_poll(10);fm1_usb_midi_poll(20);CHECK(reply_calls==1 && reply_cmd==0x31 && !sdk_starts && !nesting);
    busy=1;before=packets;request(0x11);fm1_usb_midi_poll(30);CHECK(packets==before);busy=0;fm1_usb_midi_poll(40);CHECK(reply_calls==2 && reply_cmd==0x11);
    /* Queue overflow invalidates the entire old session before parsing. */
    for(i=0;i<20;i++){out_n=64;memset(out,0,sizeof(out));rx_handler(&device,3);}fm1_usb_midi_poll(50);CHECK(!sdk_starts);
    request(0x31);device.bDeviceStates=0;before=packets;fm1_usb_midi_poll(60);CHECK(packets==before);device.bDeviceStates=USB_CONFIGURED;fm1_usb_midi_poll(70);CHECK(packets==before);
    request(0x31);fm1_usb_midi_poll(80);fm1_usb_midi_poll(90);CHECK(reply_calls==3 && reply_cmd==0x31 && !sdk_starts);
    puts("PASS actual MIDI descriptor registration, EP0 validation, bounded IRQ ring, nonblocking EP4 DMA, reset/overflow cancellation");return 0;
}
