#include "app_config.h"
#include "usb/usb_config.h"
/* Four disjoint, 64-byte aligned buffers: CDC IN, OUT, notification and EP0.
 * Reserve more than the largest 64-byte packet plus USB0 CRC/ping-pong
 * headroom. This replaces only the SDK DMA allocator, not its USB stack.
 * Allocation happens before IRQ registration; no live allocation/free. */
static u8 fm1_usb_dma[4][256] __attribute__((aligned(64)));
static u8 allocated[4];
void *usb_epbuf_alloc(usb_dev id,u32 size) {
    unsigned i;
    if(id!=FM1_USB_CONTROLLER || !size || size>128)return NULL;
    for(i=0;i<4;i++)if(!allocated[i]) {allocated[i]=1;return fm1_usb_dma[i];}
    return NULL;
}
void usb_epbuf_free(usb_dev id,void *p) {
    unsigned i; if(id!=FM1_USB_CONTROLLER)return;
    for(i=0;i<4;i++)if(p==fm1_usb_dma[i])allocated[i]=0;
}
