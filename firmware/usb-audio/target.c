/* SPDX-License-Identifier: GPL-3.0-only
 * Reused from Keitark/fm1-mdx at 4141057; see licenses/fm1-mdx-GPL-3.0.txt. */
#ifdef FM1_UAC_TARGET_HOST
#include "target_fake.h"
#else
#include "app_config.h"
#include "system/includes.h"
#include "system/spinlock.h"
#include "usb/device/usb_stack.h"
#endif
#include "bridge.h"
#include "target.h"
#include "packet.h"
#include <stdio.h>
#include <string.h>
static fm1_uac_bridge bridge;
static spinlock_t lock;
static unsigned armed;
static volatile unsigned mic_bits;
static volatile unsigned capture_epoch;
static unsigned capture_pending;
static u8 capture_packet[192]; /* CPU staging; controller never owns this. */
static struct {uint32_t busy,submitted,short_write,starts,stops,last_csr,last_write;} transport;
#ifdef _MSC_VER
static __declspec(align(64)) u8 fm1_uac_dma[2][256];
#else
static u8 fm1_uac_dma[2][256] __attribute__((aligned(64)));
#endif
static unsigned take(void){unsigned f;local_irq_save(f);arch_spin_lock(&lock);return f;}
static void release(unsigned f){arch_spin_unlock(&lock);local_irq_restore(f);}
static void rx(struct usb_device_t *d,u32 ep) {
    unsigned f,n;const usb_dev id=usb_device2id(d);(void)ep;
    n=usb_g_iso_read(id,1,NULL,192,0);
    f=take();if(armed)fm1_uac_receive(&bridge,fm1_uac_dma[0],n);release(f);
}
static void tx(struct usb_device_t *d,u32 ep) {
    unsigned f,n,csr,result,epoch;const usb_dev id=usb_device2id(d);(void)ep;
    /* The SDK writer can poll TxPktRdy for seconds. Never enter that wait
       from a USB IRQ, or overwrite a buffer the controller still owns. */
    csr=usb_read_txcsr(id,1);
    f=take();transport.last_csr=csr;
    if(csr&TXCSRP_TxPktRdy){transport.busy++;release(f);return;}
    release(f);
    f=take();epoch=capture_epoch;
    if(armed && bridge.in_active && !capture_pending)
        capture_pending=(unsigned)fm1_uac_transmit(&bridge,capture_packet,sizeof(capture_packet));
    n=armed && bridge.in_active?capture_pending:0;release(f);
    if(n){result=fm1_usb_packet_write(id,1,capture_packet,n,&capture_epoch,epoch);
        f=take();transport.last_write=result;
        if(result==n){transport.submitted++;if(epoch==capture_epoch)capture_pending=0;}
        else transport.short_write++;release(f);}
}
static void stream(struct usb_device_t *d,unsigned direction,int on) {
    unsigned f;const usb_dev id=usb_device2id(d);
    /* Remove the completion callback before resetting its FIFO/DMA state. */
    if(direction){usb_clr_intr_txe(id,1);usb_g_set_intr_hander(id,0x81,NULL);
        usb_write_txcsr(id,1,TXCSRP_FlushFIFO|TXCSRP_ClrDataTog|TXCSRP_ISOCHRONOUS);}
    else {usb_clr_intr_rxe(id,1);usb_g_set_intr_hander(id,1,NULL);
        usb_write_rxcsr(id,1,RXCSRP_FlushFIFO|RXCSRP_ClrDataTog|RXCSRP_ISOCHRONOUS);}
    f=take();
    if(direction){++capture_epoch;capture_pending=0;}
    if(!armed)on=0;
    if(direction){if(on)transport.starts++;else transport.stops++;}
    fm1_uac_stream(&bridge,direction,on);mic_bits=bridge.mic_active?4u:0u;release(f);
    if(on) {
        if(direction)usb_enable_ep(id,1);
        usb_g_set_intr_hander(id,direction?0x81:1,direction?tx:rx);
        usb_g_ep_config(id,direction?0x81:1,USB_ENDPOINT_XFER_ISOC,0,fm1_uac_dma[direction],192);
        if(direction)tx(d,1);
        if(direction)usb_set_intr_txe(id,1);else usb_set_intr_rxe(id,1);
    }
}
static void reset(struct usb_device_t *d,u32 itf){(void)itf;fm1_usb_audio_microphone(0);stream(d,0,0);stream(d,1,0);}
static u32 setup(struct usb_device_t *d,struct usb_ctrlrequest *r) {
    unsigned f;u8 *reply=usb_get_setup_buffer(d);reply[0]=reply[1]=0;
    if(d->bDeviceStates!=USB_CONFIGURED || !fm1_uac_interface_request(r->bRequestType,r->bRequest,r->wValue,r->wIndex,r->wLength)) {
        usb_set_setup_phase(d,USB_EP0_SET_STALL);return 0;
    }
    if(r->bRequest==11) {
        if(r->wIndex>2)stream(d,r->wIndex==4,r->wValue);
        usb_set_setup_phase(d,USB_EP0_STAGE_SETUP);
    } else {
        if(r->bRequest==10 && r->wIndex>2){f=take();reply[0]=r->wIndex==4?bridge.in_active:bridge.out_active;release(f);}
        usb_set_data_payload(d,r,reply,r->wLength);
    }
    return 0;
}
u32 fm1_uac_desc_config(usb_dev id,u8 *out,u32 *itf) {
    unsigned i;if(id!=FM1_USB_CONTROLLER || *itf!=2)return 0;
    for(i=2;i<5;i++) {
        if(usb_set_interface_hander(id,i,setup)!=i || usb_set_reset_hander(id,i,reset)!=i)return 0;
    }
    memcpy(out,fm1_uac_descriptor,sizeof(fm1_uac_descriptor));*itf=5;return sizeof(fm1_uac_descriptor);
}
void fm1_usb_audio_init(void){unsigned f=take();fm1_uac_microphone(&bridge,0);mic_bits=0;armed=1;release(f);}
void fm1_usb_audio_stop(void) {
    unsigned f=take();armed=0;release(f);reset(usb_id2device(FM1_USB_CONTROLLER),2);
}
void fm1_usb_audio_dac(int32_t *p,unsigned n){unsigned f=take();if(armed)fm1_uac_dac(&bridge,p,n);mic_bits=bridge.mic_active?4u:0u;release(f);}
void fm1_usb_audio_microphone(int on){unsigned f=take();fm1_uac_microphone(&bridge,armed && on);mic_bits=0;release(f);}
#ifndef _MSC_VER
__attribute__((noinline))
#endif
unsigned fm1_usb_audio_mic_bits(void){return mic_bits;}
void fm1_usb_audio_mic_status(char *out,size_t n) {
    unsigned enabled,active,f=take();enabled=bridge.mic_enabled;active=bridge.mic_active;release(f);
    snprintf(out,n,"MIC source=usb-playback enabled=%u active=%u threshold=%u release=%u hold_ms=10\n",
             enabled,active,FM1_MIC_ATTACK,FM1_MIC_RELEASE);
}
void fm1_usb_audio_status(char *out,size_t n) {
    unsigned values[11],f=take();
    values[0]=bridge.out_active;values[1]=bridge.in_active;values[2]=bridge.rx_packets;values[3]=bridge.tx_packets;
    values[4]=bridge.bad_packets;values[5]=bridge.playback.wr-bridge.playback.rd;values[6]=bridge.capture.wr-bridge.capture.rd;
    values[7]=bridge.playback.underruns;values[8]=bridge.capture.underruns;values[9]=bridge.playback.overruns;values[10]=bridge.capture.overruns;
    release(f); /* Formatting must not extend the IRQ-disabled DMA critical section. */
    snprintf(out,n,"USB AUDIO rate=48000 bits=16 channels=2 out=%u in=%u rx=%u tx=%u bad=%u play_fill=%u capture_fill=%u under=%u,%u over=%u,%u\n",
        values[0],values[1],values[2],values[3],values[4],values[5],values[6],values[7],values[8],values[9],values[10]);
}
void fm1_usb_audio_transport_status(char *out,size_t n) {
    uint32_t busy,submitted,short_write,starts,stops,csr,written,epoch,pending;unsigned f=take();
    busy=transport.busy;submitted=transport.submitted;short_write=transport.short_write;
    starts=transport.starts;stops=transport.stops;csr=transport.last_csr;written=transport.last_write;
    epoch=capture_epoch;pending=capture_pending;release(f);
    snprintf(out,n,"USB TRANSPORT submitted=%lu short=%lu busy=%lu starts=%lu stops=%lu csr=%04lx written=%lu epoch=%lu pending=%lu\n",
        (unsigned long)submitted,(unsigned long)short_write,(unsigned long)busy,(unsigned long)starts,
        (unsigned long)stops,(unsigned long)csr,(unsigned long)written,(unsigned long)epoch,(unsigned long)pending);
}
