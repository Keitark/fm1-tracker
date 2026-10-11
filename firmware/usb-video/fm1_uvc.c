/* Minimal UVC1.0 USB0 adapter. The stock camera UVC driver is not linked.
 * USB IRQ handles only control bytes and one already prepared ISO packet.
 * Rendering and RGB conversion remain in the tracker task. */
#ifdef FM1_UVC_ADAPTER_TEST
#include "tests/fake_uvc_sdk.h"
#else
#include "app_config.h"
#include "system/includes.h"
#include "system/timer.h"
#endif
#include "fm1_uvc.h"
#include "uvc_control.h"
#include <string.h>

#ifdef FM1_UVC_SCREEN
#if FM1_USB_CONTROLLER != 0
#error "Screen UVC supports only the verified full-speed USB0 route"
#endif

#define VIDEO_EP 4u
#ifdef FM1_UVC_ADAPTER_TEST
#define CPU_CACHE
#define DMA_STORAGE
#define DMA_SYNC() fm1_uvc_test_sync()
#else
#define CPU_CACHE __attribute__((section(".memp_memory_x"), aligned(4), used))
#define DMA_STORAGE __attribute__((aligned(64), used))
#define DMA_SYNC() __asm__ volatile("csync" ::: "memory")
#endif

/* CPU-only state is in the reclaimed cache SRAM. Endpoint DMA is separately
 * owned ordinary SRAM; no peripheral DMA ever reads the cache state. */
static fm1_uvc_stream fm1_uvc_state CPU_CACHE;
static fm1_uvc_control fm1_uvc_control_state CPU_CACHE;
static uint32_t ep0_token CPU_CACHE;
static volatile uint8_t ep_running CPU_CACHE;
static volatile uint8_t capture_shutdown CPU_CACHE;
static uint8_t fm1_uvc_dma[1024] DMA_STORAGE;

typedef char descriptor_setup_capacity[
    9u + 75u + FM1_UVC_VIDEO_DESCRIPTOR_BYTES <= USB_SETUP_SIZE ? 1 : -1];
typedef char iso_dma_capacity[
    FM1_UVC_PACKET_BYTES <= sizeof(fm1_uvc_dma) ? 1 : -1];

static u32 stall(struct usb_device_t *device)
{
    usb_set_setup_phase(device, USB_EP0_SET_STALL);
    return USB_EP0_SET_STALL;
}

static u32 receive_stall(struct usb_device_t *device)
{
    /* The SDK OUT-data branch consumes returned phase 0/2, but does not arm
     * phase 3 in hardware. Explicitly stall before returning that phase. */
    usb_ep0_Set_Stall(USB0);
    return stall(device);
}

/* IRQs are disabled by the caller (USB ISR or the public stop wrapper). The
 * FUSB endpoint-disable bit prevents host transactions while FIFO is flushed.
 * FlushFIFO/ClrDataTog match the SDK's endpoint initialization sequence. */
static void endpoint_stop(void)
{
    ep_running = 0;
    usb_clr_intr_txe(USB0, VIDEO_EP);
    usb_g_set_intr_hander(USB0, FM1_UVC_VIDEO_ENDPOINT, NULL);
    usb_disable_ep(USB0, VIDEO_EP);
    usb_write_txcsr(USB0, VIDEO_EP, BIT(14) | BIT(6) | BIT(3));
    usb_write_txcsr(USB0, VIDEO_EP, BIT(14));
    usb_write_ep_cnt(USB0, VIDEO_EP, 0);
    fm1_uvc_stop(&fm1_uvc_state);
}

static int endpoint_arm(const uint8_t *bytes, uint16_t length)
{
    u32 csr;
    if (capture_shutdown || !ep_running || !fm1_uvc_control_state.configured ||
        fm1_uvc_control_state.alt != 1 || length > FM1_UVC_PACKET_BYTES)
        return 0;
    csr = usb_read_txcsr(USB0, VIDEO_EP);
    if (csr & BIT(0))
        return 0; /* The DMA buffer remains owned by the prior transaction. */
    if (length)
        memcpy(fm1_uvc_dma, bytes, length);
    DMA_SYNC();
    usb_set_dma_taddr(USB0, VIDEO_EP, fm1_uvc_dma);
    usb_write_ep_cnt(USB0, VIDEO_EP, length);
    usb_write_txcsr(USB0, VIDEO_EP, (csr & ~(BIT(7) | BIT(2))) | BIT(0));
    return 1; /* Includes a successfully armed empty ISO transaction. */
}

static void endpoint_complete(struct usb_device_t *device, u32 endpoint)
{
    fm1_uvc_read packet;
    int acquired;
    u32 csr;
    if (usb_device2id(device) != USB0 || (endpoint & 15u) != VIDEO_EP ||
        !ep_running)
        return;
    csr=usb_read_txcsr(USB0, VIDEO_EP);
    if(csr & BIT(0)) {
        /* SDK ISO recovery clears IncompTx and retries the existing DMA
         * transaction. Keep the same bytes/count and never dequeue a new
         * packet while that transaction still owns the DMA storage. */
        if(csr & BIT(7)) {
            usb_write_txcsr(USB0, VIDEO_EP, csr & ~(BIT(7) | BIT(2)));
            ++fm1_uvc_state.stats.retries;
        }
        return;
    }
    acquired = fm1_uvc_acquire(&fm1_uvc_state, &packet);
    if (acquired == FM1_UVC_OK) {
        /* The IRQ cannot be interrupted by the producer. Copy ownership ends
         * before commit; the endpoint keeps its distinct DMA buffer until the
         * hardware clears TXPKTRDY. No rendering, wait or heap call here. */
        int armed = endpoint_arm(packet.bytes, packet.length);
        (void)fm1_uvc_commit(&fm1_uvc_state, &packet, armed);
    } else {
        /* Empty ISO transactions keep the completion chain alive until the
         * task publishes its first packet or the next frame becomes due. */
        (void)endpoint_arm(NULL, 0);
    }
}

static void endpoint_start(void)
{
    if(capture_shutdown)return;
    endpoint_stop();
    fm1_uvc_start(&fm1_uvc_state, timer_get_ms());
    usb_g_set_intr_hander(USB0, FM1_UVC_VIDEO_ENDPOINT, endpoint_complete);
    usb_g_ep_config(USB0, FM1_UVC_VIDEO_ENDPOINT, USB_ENDPOINT_XFER_ISOC,
                    1, fm1_uvc_dma, FM1_UVC_PACKET_BYTES);
    /* usb_g_ep_config writes 1023 on FUSB; pin the advertised 962 byte mode. */
    usb_write_txmaxp(USB0, VIDEO_EP, FM1_UVC_PACKET_BYTES);
    usb_enable_ep(USB0, VIDEO_EP);
    ep_running = 1;
    (void)endpoint_arm(NULL, 0);
}

static void apply_event(uint8_t event)
{
    if (event == FM1_UVC_EVENT_START)
        endpoint_start();
    else if (event == FM1_UVC_EVENT_STOP)
        endpoint_stop();
}

static u32 receive_control(struct usb_device_t *device, struct usb_ctrlrequest *request)
{
    uint8_t payload[FM1_UVC_CONTROL_BYTES];
    fm1_uvc_control_reply reply;
    int result;
    u32 received;
    if (usb_device2id(device) != USB0 || request->bRequestType != 0x21 ||
        capture_shutdown ||
        request->bRequest != FM1_UVC_SET_CUR || request->wIndex != FM1_UVC_VS_INTERFACE ||
        request->wLength != FM1_UVC_CONTROL_BYTES) {
        fm1_uvc_control_cancel_pending(&fm1_uvc_control_state);
        return receive_stall(device);
    }
    received=usb_read_count0(USB0);
    if(received!=FM1_UVC_CONTROL_BYTES) {
        /* Classify an incomplete/oversized current OUT without reading any
         * payload. The core checks the saved token first: a superseded OUT
         * cannot overwrite a newer request error or cancel its reservation. */
        (void)fm1_uvc_control_out(&fm1_uvc_control_state,ep0_token,
                                NULL,(size_t)received,&reply);
        return receive_stall(device);
    }
    usb_read_ep0(USB0, payload, sizeof(payload));
    result = fm1_uvc_control_out(&fm1_uvc_control_state, ep0_token,
                                 payload, sizeof(payload), &reply);
    if (result != FM1_UVC_CONTROL_ACK)
        return receive_stall(device);
    apply_event(reply.event);
    return USB_EP0_STAGE_SETUP;
}

static u32 interface_control(struct usb_device_t *device, struct usb_ctrlrequest *request)
{
    fm1_uvc_setup setup;
    fm1_uvc_control_reply reply;
    int result;
    if (usb_device2id(device) != USB0)
        return stall(device);
    if(capture_shutdown &&
       ((request->bRequestType==0x21 && request->bRequest==FM1_UVC_SET_CUR) ||
        (request->bRequestType==0x01 && request->bRequest==11 &&
         request->wIndex==FM1_UVC_VS_INTERFACE && request->wValue==1)))
        return stall(device);
    setup.request_type = request->bRequestType;
    setup.request = request->bRequest;
    setup.value = request->wValue;
    setup.index = request->wIndex;
    setup.length = request->wLength;
    result = fm1_uvc_control_setup(&fm1_uvc_control_state, &setup, &reply);
    if (result == FM1_UVC_CONTROL_STALL)
        return stall(device);
    apply_event(reply.event);
    if (result == FM1_UVC_CONTROL_IN)
        usb_set_data_payload(device, request, reply.data, reply.length);
    else if (result == FM1_UVC_CONTROL_OUT) {
        ep0_token = reply.token;
        device->wDataLength = FM1_UVC_CONTROL_BYTES;
        usb_set_setup_phase(device, USB_EP0_STAGE_OUT);
        usb_set_setup_recv(device, receive_control);
    } else
        usb_set_setup_phase(device, USB_EP0_STAGE_SETUP);
    return 0;
}

static void interface_reset(struct usb_device_t *device, u32 interface_number)
{
    (void)interface_number; /* SDK bus reset passes UINT_MAX, not the VC index. */
    if (usb_device2id(device) != USB0)
        return;
    endpoint_stop();
    fm1_uvc_control_reset(&fm1_uvc_control_state);
    ep0_token = 0;
}

void fm1_uvc_setup_init(void)
{
    /* Exactly once before USB IRQ registration. Reset/stop never reinitializes
     * the stream: outstanding producer reservations keep their storage lease. */
    fm1_uvc_init(&fm1_uvc_state);
    fm1_uvc_control_init(&fm1_uvc_control_state);
    ep_running = 0;
    capture_shutdown = 0;
    ep0_token = 0;
}

u32 fm1_uvc_desc_config(const usb_dev id, u8 *destination, u32 *interface_number)
{
    if (id != USB0 || !destination || !interface_number ||
        *interface_number != FM1_UVC_VC_INTERFACE)
        return 0;
    if (usb_set_interface_hander(id, FM1_UVC_VC_INTERFACE, interface_control) != FM1_UVC_VC_INTERFACE ||
        usb_set_interface_hander(id, FM1_UVC_VS_INTERFACE, interface_control) != FM1_UVC_VS_INTERFACE ||
        usb_set_reset_hander(id, FM1_UVC_VC_INTERFACE, interface_reset) != FM1_UVC_VC_INTERFACE)
        return 0;
    if (!fm1_uvc_copy_video_descriptors(destination, FM1_UVC_VIDEO_DESCRIPTOR_BYTES))
        return 0;
    *interface_number += 2;
    return FM1_UVC_VIDEO_DESCRIPTOR_BYTES;
}

int fm1_uvc_setup_filter(struct usb_device_t *device, struct usb_ctrlrequest *request)
{
    unsigned recipient = request->bRequestType & USB_RECIP_MASK;
    /* Even a CDC/device SETUP supersedes the pending UVC OUT data stage. */
    fm1_uvc_control_cancel_pending(&fm1_uvc_control_state);
    ep0_token = 0;
    if (recipient == USB_RECIP_INTERFACE &&
        ((request->wIndex == FM1_UVC_VC_INTERFACE || request->wIndex == FM1_UVC_VS_INTERFACE) ||
         ((request->bRequestType==0x21 || request->bRequestType==0xa1) &&
          ((request->wIndex & 0xffu)==FM1_UVC_VC_INTERFACE ||
           (request->wIndex & 0xffu)==FM1_UVC_VS_INTERFACE)))) {
        /* Only class/interface requests carry an entity ID in wIndex's high
         * byte. Route them to the bounded core for request-error reporting;
         * standard requests still require an exact interface number. */
        /* Interface GET_STATUS is routed to the class handler by this SDK.
         * Supply the bounded standard response here; no class controls. */
        if(request->bRequestType==0x81 && request->bRequest==USB_REQ_GET_STATUS &&
           request->wValue==0 && request->wLength==2) {
            const uint8_t status[2]={0,0};
            if(!fm1_uvc_control_state.configured || device->bDeviceStates!=USB_CONFIGURED)
                (void)stall(device);
            else
                usb_set_data_payload(device,request,status,sizeof(status));
            return 1;
        }
        (void)interface_control(device, request);
        return 1;
    }
    if (request->bRequestType == 0 && request->bRequest == USB_REQ_SET_CONFIGURATION) {
        if (request->wIndex || request->wLength || request->wValue > 1 ||
            (device->bDeviceStates != USB_ADDRESS && device->bDeviceStates != USB_CONFIGURED)) {
            (void)stall(device);
            return 1;
        }
        endpoint_stop();
        (void)fm1_uvc_control_configure(&fm1_uvc_control_state, (uint8_t)request->wValue);
    }
    return 0;
}

fm1_uvc_stream *fm1_uvc_stream_get(void) { return &fm1_uvc_state; }

int fm1_uvc_capture_enabled(void)
{
    return !capture_shutdown && ep_running && fm1_uvc_control_state.configured &&
           fm1_uvc_control_state.committed && fm1_uvc_control_state.alt == 1;
}

void fm1_uvc_capture_stop(void)
{
    unsigned flags;
    local_irq_save(flags);
    /* Permanent quiescence for tracker shutdown/UBOOT. Host reset or a new
     * configuration cannot restart DMA while the tracker has stopped. */
    capture_shutdown=1;
    /* SDK USB register helpers enter/leave its counted critical section.
     * Raw ICFG save alone would let an inner SDK exit re-enable interrupts
     * midway through teardown. Keep one counted level across all USB calls. */
    sys_local_irq_disable();
    endpoint_stop();
    fm1_uvc_control_reset(&fm1_uvc_control_state);
    ep0_token = 0;
    sys_local_irq_enable();
    local_irq_restore(flags);
}

int fm1_uvc_get_status(fm1_uvc_status *status)
{
    unsigned flags;
    if(!status)return 0;
    local_irq_save(flags);
    status->active=(uint32_t)fm1_uvc_capture_enabled();
    status->configured=fm1_uvc_control_state.configured;
    status->committed=fm1_uvc_control_state.committed;
    status->alt=fm1_uvc_control_state.alt;
    status->shutdown=capture_shutdown;
    status->frames_started=fm1_uvc_state.stats.frames_started;
    status->frames_sent=fm1_uvc_state.stats.frames_sent;
    status->frames_dropped=fm1_uvc_state.stats.frames_dropped;
    status->packets_sent=fm1_uvc_state.stats.packets_sent;
    status->retries=fm1_uvc_state.stats.retries;
    status->backpressure=fm1_uvc_state.stats.backpressure;
    local_irq_restore(flags);
    return 1;
}
#endif
