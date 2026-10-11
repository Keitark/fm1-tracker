/* SPDX-License-Identifier: GPL-2.0-only
 * Derived from FM1 Doom 3d8a093ddadd7e93a210c3847db6d57ce99202c0. */
#include "fm1_usb_audio.h"

const uint8_t fm1_usb_audio_capture_descriptor[FM1_USB_AUDIO_DESCRIPTOR_BYTES] = {
    8,11,FM1_USB_AUDIO_AC_INTERFACE,2,1,1,0,0,
    9,4,FM1_USB_AUDIO_AC_INTERFACE,0,0,1,1,0,0,
    9,36,1,0,1,30,0,1,FM1_USB_AUDIO_AS_INTERFACE,
    12,36,2,3,3,6,0,2,3,0,0,0,
    9,36,3,4,1,1,0,3,0,
    9,4,FM1_USB_AUDIO_AS_INTERFACE,0,0,1,2,0,0,
    9,4,FM1_USB_AUDIO_AS_INTERFACE,1,1,1,2,0,0,
    7,36,1,4,1,1,0,
    11,36,2,1,2,2,16,1,0x44,0xac,0,
    /* Independent native IIS clock: asynchronous isochronous IN. */
    9,5,0x81,5,180,0,1,0,0,
    7,37,1,0,0,0,0
};

int fm1_usb_audio_capture_request_valid(uint8_t t, uint8_t r,
                                      uint16_t v, uint16_t i, uint16_t n)
{
    if (i != FM1_USB_AUDIO_AC_INTERFACE && i != FM1_USB_AUDIO_AS_INTERFACE) return 0;
    if (r == 11) return t == 1 && n == 0 && v <= (i == FM1_USB_AUDIO_AC_INTERFACE ? 0 : 1);
    if (r == 10) return t == 0x81 && n == 1 && v == 0;
    if (r == 0) return t == 0x81 && n == 2 && v == 0;
    return 0;
}
