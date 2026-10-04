#ifndef FM1_USB_CONTROL_H
#define FM1_USB_CONTROL_H
#include <stdint.h>
/* CDC-only control interface is always interface 0. No arbitrary EP0 lengths. */
static int fm1_cdc_request_valid(uint8_t type,uint8_t request,uint16_t value,
                                  uint16_t index,uint16_t length) {
    if(index!=0)return 0;
    if(request==0x20)return type==0x21 && value==0 && length==7;
    if(request==0x21)return type==0xa1 && value==0 && length<=7;
    if(request==0x22)return type==0x21 && !(value&~3u) && length==0;
    return 0;
}
#endif
