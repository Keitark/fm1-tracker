/* SPDX-License-Identifier: GPL-3.0-only
 * Reused from Keitark/fm1-mdx at 4141057; see licenses/fm1-mdx-GPL-3.0.txt. */
#ifndef FM1_USB_PACKET_FAKE_H
#define FM1_USB_PACKET_FAKE_H
#include <stdint.h>
#ifdef _MSC_VER
#define __attribute__(x)
#endif
#ifdef FM1_USB_PACKET_COMPOSITE_HOST
#include "../usb-audio/target_fake.h"
#undef local_irq_save
#undef local_irq_restore
#else
typedef unsigned usb_dev;
#define USB_CONFIGURED 4
#define TXCSRP_TxPktRdy 1
struct usb_device_t {unsigned bDeviceStates;};
#endif
struct fm1_packet_regs {volatile unsigned EP1_CNT,EP3_CNT;};
extern struct fm1_packet_regs fm1_packet_regs;
#define JL_USB (&fm1_packet_regs)
unsigned fm1_packet_irq_save(void);
void fm1_packet_irq_restore(unsigned);
void __local_irq_disable(void);
void __local_irq_enable(void);
void __asm_csync(void);
#define local_irq_save(f) do{f=fm1_packet_irq_save();}while(0)
#define local_irq_restore(f) fm1_packet_irq_restore(f)
#ifndef FM1_USB_PACKET_COMPOSITE_HOST
struct usb_device_t *usb_id2device(usb_dev);
unsigned usb_read_txcsr(usb_dev,unsigned);
void usb_write_txcsr(usb_dev,unsigned,unsigned);
#endif
void *usb_get_dma_taddr(usb_dev,unsigned);
#endif
