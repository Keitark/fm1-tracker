/* SPDX-License-Identifier: GPL-3.0-only
 * Reused from Keitark/fm1-mdx at 4141057; see licenses/fm1-mdx-GPL-3.0.txt. */
#include "packet_fake.h"
#include "packet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"Packet check failed line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
struct fm1_packet_regs fm1_packet_regs;
static struct usb_device_t device={USB_CONFIGURED};
static uint32_t storage[2][64];
static unsigned enabled=1,depth,csr[2],reads,writes,syncs,missing,epoch_change;
static volatile unsigned generation;
static unsigned slot(unsigned ep){CHECK(ep==1 || ep==3);return ep==3;}
unsigned fm1_packet_irq_save(void){unsigned old=enabled;enabled=0;return old;}
void fm1_packet_irq_restore(unsigned old){CHECK(!depth);enabled=old;}
void __local_irq_disable(void){enabled=0;++depth;}
void __local_irq_enable(void){CHECK(depth);if(!--depth)enabled=1;}
struct usb_device_t *usb_id2device(unsigned id){CHECK(!id && !enabled && depth);return &device;}
unsigned usb_read_txcsr(unsigned id,unsigned ep){unsigned value;CHECK(!id && !enabled && depth);
    /* Model the SDK's nested counted mask; it must not enable IRQs here. */
    __local_irq_disable();value=csr[slot(ep)];__local_irq_enable();CHECK(!enabled && depth);
    ++reads;if(epoch_change){++generation;epoch_change=0;}return value;
}
void *usb_get_dma_taddr(unsigned id,unsigned ep){CHECK(!id && !enabled && depth);return missing?NULL:storage[slot(ep)];}
void __asm_csync(void){CHECK(!enabled && depth);++syncs;}
void usb_write_txcsr(unsigned id,unsigned ep,unsigned value){CHECK(!id && !enabled && depth && syncs>writes);
    __local_irq_disable();csr[slot(ep)]=value;__local_irq_enable();CHECK(!enabled && depth);++writes;
}
static void unchanged(const uint8_t *before,unsigned ep){CHECK(!memcmp(before,storage[slot(ep)],256));}
int main(void){
    uint8_t data[192],before[256];unsigned i,j,r,w,count;
    memset(storage,0xa5,sizeof(storage));for(i=0;i<sizeof(data);i++)data[i]=(uint8_t)(i*17);
    memcpy(before,storage[0],256);csr[0]=1;r=reads;
    CHECK(!fm1_usb_packet_write(0,1,data,192,&generation,0));CHECK(reads==r+1 && !writes && enabled);unchanged(before,1);
    csr[0]=0;epoch_change=1;CHECK(!fm1_usb_packet_write(0,1,data,192,&generation,0));unchanged(before,1);
    missing=1;CHECK(!fm1_usb_packet_write(0,1,data,192,&generation,generation));unchanged(before,1);missing=0;
    device.bDeviceStates=0;r=reads;CHECK(!fm1_usb_packet_write(0,1,data,192,&generation,generation));CHECK(reads==r);device.bDeviceStates=4;
    r=reads;CHECK(!fm1_usb_packet_write(1,1,data,192,&generation,generation));
    CHECK(!fm1_usb_packet_write(0,2,data,192,&generation,generation));
    CHECK(!fm1_usb_packet_write(0,1,data,191,&generation,generation));
    CHECK(!fm1_usb_packet_write(0,3,data,64,&generation,generation));
    CHECK(!fm1_usb_packet_write(0,3,NULL,12,&generation,generation));
    CHECK(!fm1_usb_packet_write(0,3,data,0,&generation,generation));
    CHECK(!fm1_usb_packet_write(0,3,data,12,NULL,0));CHECK(reads==r);
    enabled=0;csr[0]=0;CHECK(fm1_usb_packet_write(0,1,data,192,&generation,generation)==192);
    CHECK(!enabled && !depth && fm1_packet_regs.EP1_CNT==192 && csr[0]==1);
    CHECK(!memcmp(storage[0],data,192));CHECK(!memcmp((uint8_t *)storage[0]+192,before+192,64));enabled=1;
    /* Mixed audio/CDC packets, busy intervals, stale epochs and close/reopen.
     * Exact copies and untouched headroom catch partial/shifted DMA writes. */
    for(i=0;i<100000;i++){
        unsigned ep=i&1?1:3,n=ep==1?192:1+i%63,which=slot(ep),expected=generation;
        memcpy(before,storage[which],256);for(j=0;j<n;j++)data[j]=(uint8_t)(i+j*23);
        csr[which]=i%5?0:1;epoch_change=(i%7==0);r=reads;w=writes;
        count=fm1_usb_packet_write(0,ep,data,n,&generation,expected);
        CHECK(reads==r+1 && enabled && !depth);
        if(csr[which]==1 && writes==w){CHECK(!count);unchanged(before,ep);}
        else if(expected!=generation){CHECK(!count && writes==w);unchanged(before,ep);}
        else{CHECK(count==n && writes==w+1 && csr[which]==1);CHECK(!memcmp(storage[which],data,n));
             CHECK(!memcmp((uint8_t *)storage[which]+n,before+n,256-n));
             CHECK((ep==1?fm1_packet_regs.EP1_CNT:fm1_packet_regs.EP3_CNT)==n);}
    }
    puts("PASS100000 mixed packets: busy/stale cancellation, exact DMA copies, headroom, nested IRQ restoration");return 0;
}
