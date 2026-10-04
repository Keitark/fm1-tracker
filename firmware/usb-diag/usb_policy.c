#include "app_config.h"
#include "usb/device/usb_stack.h"
/* Do not install the generic SDK private jump-to-ROM-updater command. */
u32 usb_root2_testing(void) {return 0;}
u32 check_ep_vaild(u32 ep) {return ep==0 || ep==2 || ep==3;}
static u32 filter(struct usb_device_t *d,struct usb_ctrlrequest *r) {
    unsigned recip=r->bRequestType&USB_RECIP_MASK;
    if((r->bRequestType&USB_TYPE_MASK)==USB_TYPE_VENDOR || recip==USB_RECIP_OTHER ||
       (recip==USB_RECIP_INTERFACE && r->wIndex>1) ||
       (recip==USB_RECIP_ENDPOINT && r->wIndex!=0 && r->wIndex!=0x80 &&
        r->wIndex!=2 && r->wIndex!=0x82 && r->wIndex!=0x83)) {
        usb_set_setup_phase(d,USB_EP0_SET_STALL); return 1;
    }
    return 0;
}
void user_setup_filter_install(struct usb_device_t *d) {usb_set_setup_hook(d,filter);}
