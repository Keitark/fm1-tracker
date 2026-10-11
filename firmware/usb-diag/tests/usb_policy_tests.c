/* Exercise the actual EP0 hook and endpoint guard, not a duplicated policy. */
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
struct usb_device_t { unsigned stalls; };
struct usb_ctrlrequest { uint8_t bRequestType,bRequest;uint16_t wValue,wIndex,wLength; };
static u32 (*installed)(struct usb_device_t *,struct usb_ctrlrequest *);
static void usb_set_setup_phase(struct usb_device_t *d,unsigned phase) {
    if(phase!=USB_EP0_SET_STALL)abort();++d->stalls;
}
static void usb_set_setup_hook(struct usb_device_t *d,
    u32 (*hook)(struct usb_device_t *,struct usb_ctrlrequest *)) {(void)d;installed=hook;}
#include "../usb_policy.c"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#x);return 1;}}while(0)
static int blocked(unsigned type,unsigned index) {
    struct usb_device_t d={0};
    struct usb_ctrlrequest r={(uint8_t)type,11,0,(uint16_t)index,0};
    unsigned result=installed(&d,&r);
    if(d.stalls!=result)abort();return (int)result;
}
int main(void) {
    struct usb_device_t d={0};unsigned i;
    user_setup_filter_install(&d);CHECK(installed);CHECK(usb_root2_testing()==0);
    for(i=0;i<256;i++) {
#ifdef FM1_USB_AUDIO
        CHECK(blocked(USB_RECIP_INTERFACE,i)==(i>4));
        CHECK(check_ep_vaild(i)==(i<=3));
        CHECK(blocked(USB_RECIP_ENDPOINT,i)==
              !(i==0 || i==0x80 || i==1 || i==0x81 || i==2 || i==0x82 || i==0x83));
#else
        CHECK(blocked(USB_RECIP_INTERFACE,i)==(i>1));
        CHECK(check_ep_vaild(i)==(i==0 || i==2 || i==3));
        CHECK(blocked(USB_RECIP_ENDPOINT,i)==
              !(i==0 || i==0x80 || i==2 || i==0x82 || i==0x83));
#endif
        CHECK(blocked(USB_TYPE_VENDOR|USB_RECIP_INTERFACE,i));
        CHECK(blocked(USB_TYPE_VENDOR|USB_RECIP_ENDPOINT,i));
        CHECK(blocked(USB_RECIP_OTHER,i));
    }
    CHECK(blocked(USB_RECIP_INTERFACE,0x100));
    CHECK(blocked(USB_RECIP_ENDPOINT,0x101));
    CHECK(blocked(USB_TYPE_VENDOR,0));
    CHECK(!blocked(0,0));CHECK(!blocked(0x80,0));
    puts("PASS actual setup hook: advertised addresses allowed, unknown/vendor addresses stalled");return 0;
}
