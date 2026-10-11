/* SPDX-License-Identifier: GPL-2.0-only
 * Capture-only UAC1 SDK adapter. Derived from FM1 Doom 3d8a093d;
 * bounded endpoint ownership follows the current tracker UVC adapter. */
#ifdef FM1_UAC_ADAPTER_TEST
#include "tests/fake_uac_sdk.h"
#else
#include "app_config.h"
#include "system/includes.h"
#include "usb/device/usb_stack.h"
#endif
#include "fm1_usb_audio.h"
#include "fm1_usb_audio_target.h"
#include <stdio.h>
#include <string.h>

#define AUDIO_EP 1u
#ifdef FM1_UAC_ADAPTER_TEST
#ifdef _MSC_VER
#define DMA_STORAGE __declspec(align(64))
#else
#define DMA_STORAGE __attribute__((aligned(64)))
#endif
#define DMA_SYNC() fm1_uac_test_sync()
#else
#define DMA_STORAGE __attribute__((aligned(64), used))
#define DMA_SYNC() __asm__ volatile("csync" ::: "memory")
#endif
#ifdef _MSC_VER
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif

/* Controller DMA is always ordinary RAM, distinct from the producer FIFO and
 * persistent pending packet. The producer never touches this buffer. */
static DMA_STORAGE u8 fm1_uac_dma[256];
static volatile u8 ep_running, configured, capture_shutdown;
static u32 incomplete_retries;

typedef char descriptor_setup_capacity[
    9u + 75u + FM1_USB_AUDIO_DESCRIPTOR_BYTES
#ifdef FM1_UVC_SCREEN
    + 159u
#endif
    <= USB_SETUP_SIZE ? 1 : -1];

static u32 stall(struct usb_device_t *device)
{
    usb_set_setup_phase(device, USB_EP0_SET_STALL);
    return USB_EP0_SET_STALL;
}

/* USB ISR or counted CPU0 IRQ guard owns all controller state. Controller
 * calls never run under the cross-core capture lock. */
static void endpoint_stop(void)
{
    ep_running=0;
    usb_clr_intr_txe(FM1_USB_CONTROLLER,AUDIO_EP);
    usb_g_set_intr_hander(FM1_USB_CONTROLLER,FM1_USB_AUDIO_ENDPOINT,NULL);
    usb_disable_ep(FM1_USB_CONTROLLER,AUDIO_EP);
    usb_write_txcsr(FM1_USB_CONTROLLER,AUDIO_EP,BIT(14)|BIT(6)|BIT(3));
    usb_write_txcsr(FM1_USB_CONTROLLER,AUDIO_EP,BIT(14));
    usb_write_ep_cnt(FM1_USB_CONTROLLER,AUDIO_EP,0);
    (void)fm1_usb_audio_capture_stream(0);
}

static unsigned endpoint_arm(const uint8_t *bytes,unsigned length,
    const volatile unsigned *epoch,unsigned expected)
{
    u32 csr;
    if(capture_shutdown || !configured || !ep_running || !bytes || !epoch ||
       *epoch!=expected ||
       (length!=172u && length!=176u && length!=180u))return 0;
    csr=usb_read_txcsr(FM1_USB_CONTROLLER,AUDIO_EP);
    if(csr&BIT(0))return 0; /* DMA still belongs to the previous transaction. */
    memcpy(fm1_uac_dma,bytes,length);
    DMA_SYNC();
    usb_set_dma_taddr(FM1_USB_CONTROLLER,AUDIO_EP,fm1_uac_dma);
    usb_write_ep_cnt(FM1_USB_CONTROLLER,AUDIO_EP,length);
    usb_write_txcsr(FM1_USB_CONTROLLER,AUDIO_EP,(csr&~(BIT(7)|BIT(2)))|BIT(0));
    return length;
}

static void endpoint_complete(struct usb_device_t *device,u32 endpoint)
{
    const uint8_t *bytes;
    const volatile unsigned *epoch;
    unsigned expected,length,written;
    u32 csr;
    if(usb_device2id(device)!=FM1_USB_CONTROLLER || (endpoint&15u)!=AUDIO_EP ||
       !ep_running || capture_shutdown || !configured)return;
    csr=usb_read_txcsr(FM1_USB_CONTROLLER,AUDIO_EP);
    if(csr&BIT(0)) {
        /* Incomplete ISO retries its existing DMA bytes and count. No FIFO
         * dequeue or cadence advance occurs until that transaction completes. */
        if(csr&BIT(7)) {
            usb_write_txcsr(FM1_USB_CONTROLLER,AUDIO_EP,csr&~(BIT(7)|BIT(2)));
            ++incomplete_retries;
        } else {
            (void)fm1_usb_audio_capture_prepare(0,&bytes,&epoch,&expected);
        }
        return;
    }
    length=fm1_usb_audio_capture_prepare(1,&bytes,&epoch,&expected);
    if(!length)return;
    written=endpoint_arm(bytes,length,epoch,expected);
    fm1_usb_audio_capture_complete(expected,written);
}

static void endpoint_start(struct usb_device_t *device)
{
    if(capture_shutdown || !configured)return;
    endpoint_stop();
    if(!fm1_usb_audio_capture_stream(1))return;
    usb_g_set_intr_hander(FM1_USB_CONTROLLER,FM1_USB_AUDIO_ENDPOINT,endpoint_complete);
    usb_g_ep_config(FM1_USB_CONTROLLER,FM1_USB_AUDIO_ENDPOINT,
        USB_ENDPOINT_XFER_ISOC,1,fm1_uac_dma,FM1_USB_AUDIO_MAX_PACKET_BYTES);
    /* The SDK programs 1023 for FUSB ISO; enforce the advertised 180 maximum. */
    usb_write_txmaxp(FM1_USB_CONTROLLER,AUDIO_EP,FM1_USB_AUDIO_MAX_PACKET_BYTES);
    usb_enable_ep(FM1_USB_CONTROLLER,AUDIO_EP);
    ep_running=1;
    endpoint_complete(device,AUDIO_EP); /* One silent prime, then TX IRQ only. */
}

static u32 interface_control(struct usb_device_t *device,struct usb_ctrlrequest *request)
{
    uint8_t reply[2]={0,0};
    if(usb_device2id(device)!=FM1_USB_CONTROLLER || !configured ||
       device->bDeviceStates!=USB_CONFIGURED ||
       !fm1_usb_audio_capture_request_valid(request->bRequestType,
         request->bRequest,request->wValue,request->wIndex,request->wLength))
        return stall(device);
    if(request->bRequest==11) {
        if(request->wIndex==FM1_USB_AUDIO_AS_INTERFACE) {
            if(request->wValue && capture_shutdown)return stall(device);
            if(request->wValue)endpoint_start(device);
            else endpoint_stop();
        }
        usb_set_setup_phase(device,USB_EP0_STAGE_SETUP);
    } else {
        if(request->bRequest==10 && request->wIndex==FM1_USB_AUDIO_AS_INTERFACE)
            reply[0]=ep_running;
        usb_set_data_payload(device,request,reply,request->wLength);
    }
    return 0;
}

static void interface_reset(struct usb_device_t *device,u32 interface_number)
{
    (void)interface_number; /* SDK reset passes UINT_MAX. */
    if(usb_device2id(device)!=FM1_USB_CONTROLLER)return;
    endpoint_stop();
    configured=0;
}

u32 fm1_uac_desc_config(usb_dev id,u8 *out,u32 *interface_number)
{
    unsigned i;
    if(id!=FM1_USB_CONTROLLER || !out || !interface_number ||
       *interface_number!=FM1_USB_AUDIO_AC_INTERFACE)return 0;
    for(i=FM1_USB_AUDIO_AC_INTERFACE;i<=FM1_USB_AUDIO_AS_INTERFACE;++i) {
        if(usb_set_interface_hander(id,i,interface_control)!=i)return 0;
    }
    if(usb_set_reset_hander(id,FM1_USB_AUDIO_AC_INTERFACE,interface_reset)!=
       FM1_USB_AUDIO_AC_INTERFACE)return 0;
    memcpy(out,fm1_usb_audio_capture_descriptor,FM1_USB_AUDIO_DESCRIPTOR_BYTES);
    *interface_number+=2;
    return FM1_USB_AUDIO_DESCRIPTOR_BYTES;
}

int fm1_usb_audio_setup_filter(struct usb_device_t *device,struct usb_ctrlrequest *request)
{
    unsigned recipient=request->bRequestType&USB_RECIP_MASK;
    unsigned index=request->wIndex;
    if(usb_device2id(device)!=FM1_USB_CONTROLLER)return 0;
    if(recipient==USB_RECIP_INTERFACE &&
       (index==FM1_USB_AUDIO_AC_INTERFACE || index==FM1_USB_AUDIO_AS_INTERFACE ||
        ((request->bRequestType&0x60u) &&
         ((index&0xffu)==FM1_USB_AUDIO_AC_INTERFACE ||
          (index&0xffu)==FM1_USB_AUDIO_AS_INTERFACE)))) {
        /* High-byte entities are class-only. No entities advertise controls,
         * so class/vendor requests must stall without reading OUT data. */
        (void)interface_control(device,request);
        return 1;
    }
    if(recipient==USB_RECIP_ENDPOINT && (index&0xffu)==FM1_USB_AUDIO_ENDPOINT) {
        if(configured && device->bDeviceStates==USB_CONFIGURED &&
           request->bRequestType==0x82 && request->bRequest==USB_REQ_GET_STATUS &&
           !request->wValue && index==FM1_USB_AUDIO_ENDPOINT && request->wLength==2) {
            const uint8_t reply[2]={0,0}; /* ISO endpoints cannot be halted. */
            usb_set_data_payload(device,request,reply,sizeof(reply));
        } else (void)stall(device);
        return 1;
    }
    if(request->bRequestType==0 && request->bRequest==USB_REQ_SET_CONFIGURATION) {
        if(request->wIndex || request->wLength || request->wValue>1 ||
           (device->bDeviceStates!=USB_ADDRESS && device->bDeviceStates!=USB_CONFIGURED)) {
            (void)stall(device);return 1;
        }
        endpoint_stop();
        configured=(uint8_t)request->wValue;
    }
    return 0; /* Let the existing SDK finish an accepted device request. */
}

void fm1_usb_audio_target_init(void)
{
    fm1_usb_audio_capture_init();
    ep_running=configured=capture_shutdown=0;
    incomplete_retries=0;
}

void fm1_usb_audio_target_stop(void)
{
    unsigned flags;
    local_irq_save(flags);
    capture_shutdown=1;
    /* SDK register helpers nest its counted IRQ guard. Raw IRQ save alone
     * cannot prevent an inner helper from re-enabling IRQ during teardown. */
    sys_local_irq_disable();
    endpoint_stop();
    configured=0;
    fm1_usb_audio_capture_stop();
    sys_local_irq_enable();
    local_irq_restore(flags);
}

void NOINLINE fm1_usb_audio_target_status(char *out,size_t length)
{
    fm1_usb_audio_capture_diagnostics d;
    unsigned flags;
    unsigned cfg,shutdown,retries;
    if(!out || !length)return;
    if(length>384u)length=384u;
    local_irq_save(flags);
    cfg=configured;shutdown=capture_shutdown;retries=incomplete_retries;
    fm1_usb_audio_capture_get_diagnostics(&d);
    local_irq_restore(flags);
    snprintf(out,length,
        "USB AUDIO rate=44100 bits=16 ch=2 active=%lu armed=%lu cfg=%u shutdown=%u fill=%lu pending=%lu pushed=%lu packets=%lu sent=%lu silent=%lu under=%lu over=%lu busy=%lu short=%lu retry=%u epoch=%lu\n",
        (unsigned long)d.active,(unsigned long)d.armed,cfg,shutdown,
        (unsigned long)d.fill,(unsigned long)d.pending_bytes,
        (unsigned long)d.pushed_frames,(unsigned long)d.submitted_packets,
        (unsigned long)d.sent_frames,(unsigned long)d.silent_frames,
        (unsigned long)d.underruns,(unsigned long)d.overruns,
        (unsigned long)d.busy,(unsigned long)d.short_writes,retries,
        (unsigned long)d.epoch);
}
