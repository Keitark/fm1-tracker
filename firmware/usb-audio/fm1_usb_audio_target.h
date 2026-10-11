#ifndef FM1_USB_AUDIO_TARGET_H
#define FM1_USB_AUDIO_TARGET_H
#include <stddef.h>
#ifdef FM1_UAC_ADAPTER_TEST
#include "tests/fake_uac_sdk.h"
#else
#include "usb/device/usb_stack.h"
#endif
/* Called once before USB attach. stop permanently prevents host restart until
 * a fresh init; configuration/reset never re-arms the capture core. */
void fm1_usb_audio_target_init(void);
void fm1_usb_audio_target_stop(void);
void fm1_usb_audio_target_status(char *out, size_t length);
/* SDK usb_device_t, usb_ctrlrequest and usb_dev are declared by the caller's
 * usb_stack.h (or the host fake). The filter returns 1 when it consumes SETUP.
 * Run for every SETUP before ordinary SDK dispatch, including device requests. */
int fm1_usb_audio_setup_filter(struct usb_device_t *, struct usb_ctrlrequest *);
u32 fm1_uac_desc_config(usb_dev id, u8 *out, u32 *interface_number);
#endif
