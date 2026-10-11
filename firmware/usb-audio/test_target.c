/* SPDX-License-Identifier: GPL-3.0-only
 * Reused from Keitark/fm1-mdx at 4141057; see licenses/fm1-mdx-GPL-3.0.txt. */
#define FM1_UAC_TARGET_HOST 1
#include "target.c"
#include <stdlib.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static struct usb_device_t device={USB_CONFIGURED};
static unsigned phase,rx_length,tx_length,enabled;
static unsigned tx_csr,tx_result=192,tx_calls,tx_flushes,rx_flushes;
static u8 *dma[2];
static void (*interrupts[2])(struct usb_device_t *,u32);
static u32 (*handlers[5])(struct usb_device_t *,struct usb_ctrlrequest *);
static void (*resets[5])(struct usb_device_t *,u32);
void arch_spin_lock(spinlock_t *l){CHECK(!*l);*l=1;}
void arch_spin_unlock(spinlock_t *l){CHECK(*l);*l=0;}
usb_dev usb_device2id(const struct usb_device_t *d){CHECK(d==&device);return 0;}
struct usb_device_t *usb_id2device(usb_dev id){CHECK(!id);return &device;}
u32 usb_g_iso_read(usb_dev id,u32 ep,void *p,u32 n,u32 last){CHECK(!id && ep==1 && !p && n==192 && !last);return rx_length;}
unsigned fm1_usb_packet_write(unsigned id,unsigned ep,const uint8_t *p,unsigned n,const volatile unsigned *epoch,unsigned expected){
    CHECK(!id && ep==1 && p && n==192 && epoch==&capture_epoch);
    if(*epoch!=expected || (tx_csr&1))return 0;
    if(tx_result==n)memcpy(dma[1],p,n);
    tx_length=n;tx_calls++;return tx_result;
}
u32 usb_read_txcsr(usb_dev id,u32 ep){CHECK(!id && ep==1);return tx_csr;}
void usb_write_txcsr(usb_dev id,u32 ep,u32 v){CHECK(!id && ep==1 && v==(TXCSRP_FlushFIFO|TXCSRP_ClrDataTog|TXCSRP_ISOCHRONOUS));tx_csr=0;tx_flushes++;}
void usb_write_rxcsr(usb_dev id,u32 ep,u32 v){CHECK(!id && ep==1 && v==(RXCSRP_FlushFIFO|RXCSRP_ClrDataTog|RXCSRP_ISOCHRONOUS));rx_flushes++;}
void usb_set_intr_txe(usb_dev id,u32 ep){CHECK(!id && ep==1 && interrupts[1]);}
void usb_set_intr_rxe(usb_dev id,u32 ep){CHECK(!id && ep==1 && interrupts[0]);}
void usb_clr_intr_txe(usb_dev id,u32 ep){CHECK(!id && ep==1);}
void usb_clr_intr_rxe(usb_dev id,u32 ep){CHECK(!id && ep==1);}
void usb_enable_ep(usb_dev id,u32 ep){CHECK(!id && ep==1);enabled++;}
u32 usb_g_ep_config(usb_dev id,u32 ep,u32 type,u32 ie,u8 *p,u32 n){CHECK(!id && type==1 && !ie && n==192 && !((uintptr_t)p%64));dma[ep>>7]=p;return 0;}
u32 usb_g_set_intr_hander(usb_dev id,u32 ep,void (*h)(struct usb_device_t *,u32)){CHECK(!id && (ep==1 || ep==0x81));interrupts[ep>>7]=h;return 0;}
u32 usb_set_interface_hander(usb_dev id,u32 i,u32 (*h)(struct usb_device_t *,struct usb_ctrlrequest *)){CHECK(!id && i>=2 && i<5);handlers[i]=h;return i;}
u32 usb_set_reset_hander(usb_dev id,u32 i,void (*h)(struct usb_device_t *,u32)){CHECK(!id && i>=2 && i<5);resets[i]=h;return i;}
void usb_set_setup_phase(struct usb_device_t *d,u8 p){CHECK(d==&device);phase=p;}
void *usb_get_setup_buffer(const struct usb_device_t *d){CHECK(d==&device);return device.setup;}
u8 *usb_set_data_payload(struct usb_device_t *d,struct usb_ctrlrequest *r,const void *p,u32 n){CHECK(d==&device && p==device.setup && n==r->wLength && n<=2);return device.setup;}
static void set(unsigned itf,unsigned alt){struct usb_ctrlrequest r={1,11,alt,itf,0};phase=99;handlers[itf](&device,&r);CHECK(phase==0);}
int main(void) {
    u8 desc[173];u32 itf=2;unsigned i;int32_t pcm[128]={0};
    fm1_usb_audio_init();CHECK(fm1_uac_desc_config(0,desc,&itf)==173 && itf==5 && !memcmp(desc,fm1_uac_descriptor,173));
    CHECK(!fm1_usb_audio_mic_bits() && !bridge.mic_enabled);
    set(3,1);set(4,1);CHECK(bridge.out_active && bridge.in_active && interrupts[0] && interrupts[1] && enabled);
    memset(dma[0],0,192);rx_length=192;interrupts[0](&device,1);CHECK(bridge.rx_packets==1 && bridge.playback.wr==48);
    rx_length=193;interrupts[0](&device,1);CHECK(bridge.bad_packets==1);
    for(i=0;i<5;i++)fm1_usb_audio_dac(pcm,64);interrupts[1](&device,1);CHECK(tx_length==192);
    {u8 held[192];unsigned rd=bridge.capture.rd,calls=tx_calls;char s[224];
     memcpy(held,dma[1],192);tx_csr=TXCSRP_TxPktRdy;
     interrupts[1](&device,1);
     CHECK(tx_calls==calls && bridge.capture.rd==rd && !memcmp(held,dma[1],192) && transport.busy==1);
     tx_csr=0;tx_result=191;interrupts[1](&device,1);CHECK(transport.short_write==1);
     fm1_usb_audio_transport_status(s,sizeof(s));CHECK(strstr(s,"short=1") && strstr(s,"busy=1") && strstr(s,"written=191"));
     tx_result=192;set(4,0);CHECK(!bridge.in_active && !interrupts[1] && !bridge.capture.primed);
     tx_csr=TXCSRP_TxPktRdy;set(4,1);CHECK(!tx_csr && bridge.in_active && transport.starts==2);
     CHECK(tx_flushes>=3 && rx_flushes>=1 && transport.submitted==3);}
    {u8 staged[192],held[192];unsigned rd;
     memcpy(held,dma[1],192);tx_result=0;interrupts[1](&device,1);
     CHECK(capture_pending==192 && !memcmp(held,dma[1],192));
     memcpy(staged,capture_packet,192);rd=bridge.capture.rd;
     fm1_usb_audio_dac(pcm,64);tx_result=192;interrupts[1](&device,1);
     CHECK(!capture_pending && bridge.capture.rd==rd && !memcmp(staged,dma[1],192));
     tx_result=0;interrupts[1](&device,1);CHECK(capture_pending==192);
     set(4,0);CHECK(!capture_pending);set(4,1);CHECK(capture_pending==192);
     /* New stream's unprimed packet is silence, not the old pending packet. */
     memset(staged,0,192);CHECK(!memcmp(staged,capture_packet,192));
     tx_result=192;interrupts[1](&device,1);CHECK(!capture_pending && !memcmp(staged,dma[1],192));}
    {struct usb_ctrlrequest r={0x81,10,0,4,1};handlers[4](&device,&r);CHECK(device.setup[0]==1);}
    for(i=0;i<200;i++){struct usb_ctrlrequest r={0x21,1,0,3,(uint16_t)i};phase=99;handlers[3](&device,&r);CHECK(phase==7);}
    fm1_usb_audio_microphone(1);
    for(i=0;i<48;i++){dma[0][4*i]=0;dma[0][4*i+1]=8;dma[0][4*i+2]=0;dma[0][4*i+3]=0xf8;}
    rx_length=192;for(i=0;i<12;i++)interrupts[0](&device,1);
    memset(pcm,0,sizeof(pcm));fm1_usb_audio_dac(pcm,64);CHECK(fm1_usb_audio_mic_bits()==4);
    {char s[160];fm1_usb_audio_mic_status(s,sizeof(s));CHECK(strstr(s,"enabled=1 active=1"));}
    fm1_usb_audio_microphone(0);CHECK(!fm1_usb_audio_mic_bits());
    fm1_usb_audio_microphone(1);memset(pcm,0,sizeof(pcm));fm1_usb_audio_dac(pcm,64);CHECK(fm1_usb_audio_mic_bits()==4);
    set(3,0);CHECK(!fm1_usb_audio_mic_bits());set(3,1);CHECK(!fm1_usb_audio_mic_bits());
    resets[2](&device,2);CHECK(!bridge.mic_enabled && !fm1_usb_audio_mic_bits() && !bridge.out_active && !bridge.in_active && !interrupts[0] && !interrupts[1]);
    set(3,1);set(4,1);fm1_usb_audio_stop();CHECK(!armed && !bridge.out_active && !bridge.in_active);
    set(3,1);CHECK(!bridge.out_active && !interrupts[0]);
    puts("PASS UAC target descriptor registration, alt settings, bounded DMA, EP0 lifetime, reset and UBOOT closure");return 0;
}
