/* SPDX-License-Identifier: GPL-3.0-only
 * Reused from Keitark/fm1-mdx at 4141057; see licenses/fm1-mdx-GPL-3.0.txt. */
#ifdef FM1_USB_PACKET_HOST
#include "packet_fake.h"
#else
#include "app_config.h"
#include "system/includes.h"
#include "usb/device/usb_stack.h"
#endif
#include "packet.h"
#include <string.h>
__attribute__((noinline,used))
unsigned fm1_usb_packet_write(unsigned id,unsigned ep,const uint8_t *data,
                            unsigned size,const volatile unsigned *epoch,
                            unsigned expected) {
    unsigned flags,csr,result=0;void *dma;
    if(id!=0 || !data || !epoch || !size ||
       !((ep==1 && size==192) || (ep==3 && size<64)))return 0;
    /* SDK register helpers use counted local IRQ masking. Nest it inside
     * a saved IRQ state so they cannot re-enable interrupts during commit.
     * Use the CPU-local counter, not local_irq_disable's global BT spinlock;
     * this USB0 task/IRQ path is pinned to CPU0.
     * Restore the caller's original state after releasing that counted mask.
     */
    local_irq_save(flags);__local_irq_disable();
    if(usb_id2device(0)->bDeviceStates!=USB_CONFIGURED)goto done;
    csr=usb_read_txcsr(0,ep);
    if((csr&TXCSRP_TxPktRdy) || *epoch!=expected)goto done;
    dma=usb_get_dma_taddr(0,ep);
    if(!dma || ((uintptr_t)dma&3))goto done;
    memcpy(dma,data,size);
    /* Mirror the pinned USB0 SDK's single-packet commit: configured TXADR is
     * unchanged, TXCNT is exact, memory is visible before TxPktRdy. Never call
     * usb_g_{bulk,iso}_write/usb_g_ep_write and their jiffies polling loop.
     */
    if(ep==1)JL_USB->EP1_CNT=size;else JL_USB->EP3_CNT=size;
    __asm_csync();
    usb_write_txcsr(0,ep,csr|TXCSRP_TxPktRdy);
    result=size;
done:
    __local_irq_enable();local_irq_restore(flags);return result;
}
