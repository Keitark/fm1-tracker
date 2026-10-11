#include "app_config.h"
#include "usb/device/usb_stack.h"
#ifdef FM1_UVC_SCREEN
#include "fm1_uvc.h"
#endif
#ifdef FM1_UAC_AUDIO
#include "fm1_usb_audio_target.h"
#endif
/* Do not install the generic SDK private jump-to-ROM-updater command. */
u32 usb_root2_testing(void) {return 0;}
u32 check_ep_vaild(u32 ep) {
#ifdef FM1_UAC_AUDIO
    if(ep==1)return 1;
#endif
#ifdef FM1_UVC_SCREEN
    if(ep==4)return 1;
#endif
    return ep==0 || ep==2 || ep==3;
}
static u32 filter(struct usb_device_t *d,struct usb_ctrlrequest *r) {
    unsigned recip=r->bRequestType&USB_RECIP_MASK;
#ifdef FM1_UVC_SCREEN
    /* This hook invalidates pending UVC OUT data on every new SETUP. */
    if(fm1_uvc_setup_filter(d,r))return 1;
    if(recip==USB_RECIP_ENDPOINT && r->wIndex==0x84) {
        /* Isochronous endpoints have no halt feature. Only standard status. */
        if(r->bRequestType==0x82 && r->bRequest==USB_REQ_GET_STATUS &&
           r->wValue==0 && r->wLength==2)return 0;
        usb_set_setup_phase(d,USB_EP0_SET_STALL);return 1;
    }
#endif
#ifdef FM1_UAC_AUDIO
    if(fm1_usb_audio_setup_filter(d,r))return 1;
    if(recip==USB_RECIP_ENDPOINT && r->wIndex==0x81) {
        if(r->bRequestType==0x82 && r->bRequest==USB_REQ_GET_STATUS &&
           r->wValue==0 && r->wLength==2)return 0;
        usb_set_setup_phase(d,USB_EP0_SET_STALL);return 1;
    }
#endif
    if((r->bRequestType&USB_TYPE_MASK)==USB_TYPE_VENDOR || recip==USB_RECIP_OTHER ||
       (recip==USB_RECIP_INTERFACE && r->wIndex>1) ||
       (recip==USB_RECIP_ENDPOINT && r->wIndex!=0 && r->wIndex!=0x80 &&
        r->wIndex!=2 && r->wIndex!=0x82 && r->wIndex!=0x83)) {
        usb_set_setup_phase(d,USB_EP0_SET_STALL); return 1;
    }
    return 0;
}
void user_setup_filter_install(struct usb_device_t *d) {usb_set_setup_hook(d,filter);}
