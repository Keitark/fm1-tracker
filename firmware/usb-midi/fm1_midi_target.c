/* SPDX-License-Identifier: Apache-2.0
 * Independently authored USB0 MIDI 1.0 adapter for the pinned SDK. */
#ifdef FM1_MIDI_TARGET_HOST
#include "tests/target_fake.h"
#else
#include "app_config.h"
#include "system/includes.h"
#include "usb/device/usb_stack.h"
#endif
#include "fm1_midi.h"
#include "fm1_midi_target.h"
#include "fm1_midi_update.h"
#ifdef FM1_PERIPHERAL_TESTS
#include "peripherals.h"
#endif
#include <string.h>
#ifdef FM1_USB_VIDEO
#error "MIDI update EP4 IN conflicts with UVC; select one in this candidate"
#endif
#ifdef _MSC_VER
static __declspec(align(64)) uint8_t fm1_midi_dma_in[256],fm1_midi_dma_out[256];
#else
static uint8_t fm1_midi_dma_in[256] __attribute__((aligned(64)));
static uint8_t fm1_midi_dma_out[256] __attribute__((aligned(64)));
#endif
static fm1_midi_parser parser;
static uint8_t rx_ring[1024],tx_ring[1024];
static volatile unsigned rx_read,rx_write,rx_fault,usb_epoch;
static unsigned tx_read,tx_write,seen_epoch,was_configured;
static unsigned first_interface;
static void rx(struct usb_device_t *device,u32 ep){
    uint8_t packet[64];unsigned n,i,write=rx_write;(void)ep;
    if(usb_device2id(device)!=0)return;
    n=usb_g_bulk_read(0,3,packet,sizeof(packet),0);
    if(device->bDeviceStates!=USB_CONFIGURED)return;
    if(n>sizeof(packet) || (n&3u) || n>sizeof(rx_ring)-(write-rx_read)){rx_fault=1;return;}
    for(i=0;i<n;i++)rx_ring[(write+i)%sizeof(rx_ring)]=packet[i];
    __asm_csync();rx_write=write+n;
}
static void reset(struct usb_device_t *device,u32 itf){
    (void)itf;if(usb_device2id(device)!=0)return;
    ++usb_epoch;rx_read=rx_write=0;rx_fault=0;
    usb_clr_intr_rxe(0,3);usb_g_set_intr_hander(0,3,0);
    usb_g_ep_config(0,0x84,USB_ENDPOINT_XFER_BULK,0,fm1_midi_dma_in,64);
    usb_g_ep_config(0,3,USB_ENDPOINT_XFER_BULK,1,fm1_midi_dma_out,64);
    usb_g_set_intr_hander(0,3,rx);usb_enable_ep(0,4);
}
static u32 setup(struct usb_device_t *device,struct usb_ctrlrequest *r){
    uint8_t *reply=usb_get_setup_buffer(device);
    if(device->bDeviceStates!=USB_CONFIGURED ||
       (r->wIndex!=first_interface && r->wIndex!=first_interface+1u) || r->wValue ||
       !((r->bRequestType==0x81 && r->bRequest==10 && r->wLength==1) ||
         (r->bRequestType==1 && r->bRequest==11 && r->wLength==0))){
        usb_set_setup_phase(device,USB_EP0_SET_STALL);return 0;
    }
    if(r->bRequest==10){reply[0]=0;usb_set_data_payload(device,r,reply,1);}
    else usb_set_setup_phase(device,USB_EP0_STAGE_SETUP);
    return 0;
}
u32 fm1_midi_desc_config(usb_dev id,u8 *out,u32 *itf){
    unsigned base=*itf,i;
    if(id!=0 || base!=
#ifdef FM1_USB_AUDIO
       5u
#else
       2u
#endif
    )return 0;
    for(i=base;i<base+2u;i++){
        if(usb_set_interface_hander(id,i,setup)!=i || usb_set_reset_hander(id,i,reset)!=i)return 0;
    }
    first_interface=base;*itf=base+2u;return fm1_midi_descriptor(out,base);
}
static int enqueue(void *context,unsigned cmd,const uint8_t *p,unsigned n){
    uint8_t frame[80],events[108];unsigned bytes,k;(void)context;
    /* Only bounded identity/capability/ACK/file-request messages leave here. */
    if(n>47)return 0;bytes=fm1_midi_frame(cmd,p,n,frame,sizeof(frame));
    bytes=fm1_midi_event_encode(frame,bytes,events,sizeof(events));
    if(!bytes || bytes>sizeof(tx_ring)-(tx_write-tx_read))return 0;
    for(k=0;k<bytes;k++)tx_ring[(tx_write+k)%sizeof(tx_ring)]=events[k];tx_write+=bytes;return 1;
}
static void stop(void *context){(void)context;
#ifdef FM1_PERIPHERAL_TESTS
    fm1_peripheral_cancel();
#endif
}
static int idle(void *context){(void)context;
#ifdef FM1_PERIPHERAL_TESTS
    return fm1_peripheral_idle();
#else
    return 1;
#endif
}
void fm1_usb_midi_init(void){
    rx_read=rx_write=rx_fault=0;tx_read=tx_write=0;usb_epoch=seen_epoch=was_configured=0;
    fm1_midi_parser_init(&parser,fm1_midi_update_message,0);
    fm1_midi_update_init(enqueue,stop,idle,0);
}
static unsigned write_packet(const uint8_t *p,unsigned n,unsigned epoch){
    unsigned flags,csr,result=0;void *dma;
    local_irq_save(flags);__local_irq_disable();
    if(usb_id2device(0)->bDeviceStates==USB_CONFIGURED && usb_epoch==epoch){
        csr=usb_read_txcsr(0,4);dma=usb_get_dma_taddr(0,4);
        if(!(csr&TXCSRP_TxPktRdy) && dma && !((uintptr_t)dma&3u)){
            memcpy(dma,p,n);JL_USB->EP4_CNT=n;__asm_csync();
            usb_write_txcsr(0,4,csr|TXCSRP_TxPktRdy);result=n;
        }
    }
    __local_irq_enable();local_irq_restore(flags);return result;
}
void fm1_usb_midi_poll(uint32_t now){
    uint8_t packet[64];unsigned configured,n,i,flags,epoch;
    configured=usb_id2device(0)->bDeviceStates==USB_CONFIGURED;
    if(!configured && was_configured)++usb_epoch;was_configured=configured;
    if(rx_fault || seen_epoch!=usb_epoch){
        local_irq_save(flags);rx_read=rx_write=rx_fault=0;epoch=usb_epoch;local_irq_restore(flags);
        tx_read=tx_write=0;fm1_midi_parser_init(&parser,fm1_midi_update_message,0);
        fm1_midi_update_reset();seen_epoch=epoch;
    }
    if(!configured)return;
    n=rx_write-rx_read;if(n>sizeof(packet))n=sizeof(packet);
    for(i=0;i<n;i++)packet[i]=rx_ring[(rx_read+i)%sizeof(rx_ring)];rx_read+=n;
    if(n)fm1_midi_events(&parser,packet,n);
    fm1_midi_update_poll(now);
    n=tx_write-tx_read;if(n>sizeof(packet))n=sizeof(packet);
    for(i=0;i<n;i++)packet[i]=tx_ring[(tx_read+i)%sizeof(tx_ring)];
    if(n)tx_read+=write_packet(packet,n,seen_epoch);
}
