#ifndef FM1_UVC_SDK_ADAPTER_H
#define FM1_UVC_SDK_ADAPTER_H

#include "uvc_stream.h"
#ifdef FM1_UVC_ADAPTER_TEST
#include "tests/fake_uvc_sdk.h"
#else
#include "usb/device/usb_stack.h"
#endif

/* Optional USB0 full-speed screen function. Initialize once before USB attach
 * and before the tracker producer starts. The SDK CDC class stays installed. */
void fm1_uvc_setup_init(void);
u32 fm1_uvc_desc_config(const usb_dev id, u8 *destination, u32 *interface_number);
/* SETUP hook: return one when handled/stalled, zero for the SDK to continue. */
int fm1_uvc_setup_filter(struct usb_device_t *device, struct usb_ctrlrequest *request);

/* Producer calls require short caller IRQ guards, as documented by the core.
 * Stop serializes itself and permanently latches capture off until next boot's
 * setup_init; it never waits for an endpoint or allocates memory. */
fm1_uvc_stream *fm1_uvc_stream_get(void);
int fm1_uvc_capture_enabled(void);
void fm1_uvc_capture_stop(void);

typedef struct {
    uint32_t active, configured, committed, alt;
    uint32_t shutdown;
    uint32_t frames_started, frames_sent, frames_dropped;
    uint32_t packets_sent, retries, backpressure;
} fm1_uvc_status;
/* Self-serialized bounded snapshot. Sent counters mean submitted to endpoint
 * DMA, and cannot establish host receipt of an isochronous packet or frame. */
int fm1_uvc_get_status(fm1_uvc_status *status);

#endif
