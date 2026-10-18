/* SPDX-License-Identifier: Apache-2.0
 * Executes the existing shared composite policy under its new MIDI defines.
 * The included policy retains its original component license. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define FM1_USB_POLICY_HOST 1
typedef uint32_t u32;
#define USB_RECIP_MASK 31u
#define USB_TYPE_MASK 96u
#define USB_TYPE_VENDOR 64u
#define USB_RECIP_INTERFACE 1u
#define USB_RECIP_ENDPOINT 2u
#define USB_RECIP_OTHER 3u
#define USB_EP0_SET_STALL 7u
struct usb_device_t {unsigned stalled;};
struct usb_ctrlrequest {uint8_t bRequestType,bRequest;uint16_t wValue,wIndex,wLength;};
static u32 (*hook)(struct usb_device_t *,struct usb_ctrlrequest *);
static void usb_set_setup_phase(struct usb_device_t *d,unsigned phase){if(phase!=7)abort();d->stalled++;}
static void usb_set_setup_hook(struct usb_device_t *d,u32 (*fn)(struct usb_device_t *,struct usb_ctrlrequest *)){(void)d;hook=fn;}
#include "../../usb-diag/usb_policy.c"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned blocked(unsigned type,unsigned index){struct usb_device_t d={0};struct usb_ctrlrequest r={(uint8_t)type,11,0,(uint16_t)index,0};unsigned n=hook(&d,&r);CHECK(n==d.stalled);return n;}
int main(void){
    struct usb_device_t d={0};unsigned i,allowed;
    user_setup_filter_install(&d);CHECK(hook && !usb_root2_testing());
    for(i=0;i<256;i++){
        allowed=i==0 || i==0x80 || i==2 || i==0x82 || i==3 || i==0x83 || i==0x84;
#ifdef FM1_USB_AUDIO
        allowed=allowed || i==1 || i==0x81;
        CHECK(blocked(1,i)==(i>6));CHECK(check_ep_vaild(i)==(i<=4));
#else
        CHECK(blocked(1,i)==(i>3));CHECK(check_ep_vaild(i)==(i==0 || (i>=2 && i<=4)));
#endif
        CHECK(blocked(2,i)==!allowed);CHECK(blocked(USB_TYPE_VENDOR|1,i));CHECK(blocked(USB_TYPE_VENDOR|2,i));
    }
    CHECK(blocked(1,0x100) && blocked(2,0x103) && blocked(USB_TYPE_VENDOR,0));
    puts("PASS shared composite EP0 policy with MIDI interfaces/endpoints; unadvertised and vendor requests stall");return 0;
}
