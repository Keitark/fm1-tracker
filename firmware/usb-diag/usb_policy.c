/* Composite policy adapted from fm1-mdx4141057: GPL-3.0-only.
 * Original CDC policy retains its existing terms; see THIRD_PARTY.md. */
#ifndef FM1_USB_POLICY_HOST
#include "app_config.h"
#include "usb/device/usb_stack.h"
#endif
/* Do not install the generic SDK private jump-to-ROM-updater command. */
u32 usb_root2_testing(void) {return 0;}
u32 check_ep_vaild(u32 ep) {return ep==0 || ep==2 || ep==3
#ifdef FM1_USB_AUDIO
    || ep==1
#endif
;}
static u32 filter(struct usb_device_t *d,struct usb_ctrlrequest *r) {
    unsigned recip=r->bRequestType&USB_RECIP_MASK;
    if((r->bRequestType&USB_TYPE_MASK)==USB_TYPE_VENDOR || recip==USB_RECIP_OTHER ||
       (recip==USB_RECIP_INTERFACE && r->wIndex>
#ifdef FM1_USB_AUDIO
        4
#else
        1
#endif
       ) ||
       (recip==USB_RECIP_ENDPOINT && r->wIndex!=0 && r->wIndex!=0x80 &&
        r->wIndex!=2 && r->wIndex!=0x82 && r->wIndex!=0x83
#ifdef FM1_USB_AUDIO
        && r->wIndex!=1 && r->wIndex!=0x81
#endif
       )) {
        usb_set_setup_phase(d,USB_EP0_SET_STALL); return 1;
    }
    return 0;
}
void user_setup_filter_install(struct usb_device_t *d) {usb_set_setup_hook(d,filter);}
