/* SPDX-License-Identifier: Apache-2.0 */
#ifndef FM1_MIDI_TARGET_FAKE_H
#define FM1_MIDI_TARGET_FAKE_H
#include "update_fake.h"
#include <stddef.h>
#ifdef _MSC_VER
#define __attribute__(x)
#endif
typedef unsigned usb_dev;
#define USB_CONFIGURED 4u
#define USB_ENDPOINT_XFER_BULK 2u
#define USB_EP0_SET_STALL 7u
#define USB_EP0_STAGE_SETUP 0u
#define TXCSRP_TxPktRdy 1u
struct usb_device_t {unsigned bDeviceStates;};
struct usb_ctrlrequest {u8 bRequestType,bRequest;u16 wValue,wIndex,wLength;};
struct midi_usb_regs {unsigned EP4_CNT;};
extern struct midi_usb_regs midi_usb_regs;
#define JL_USB (&midi_usb_regs)
unsigned usb_device2id(struct usb_device_t *);
struct usb_device_t *usb_id2device(usb_dev);
u32 usb_g_bulk_read(usb_dev,unsigned,void *,u32,unsigned);
void usb_clr_intr_rxe(usb_dev,unsigned);
void usb_g_set_intr_hander(usb_dev,unsigned,void (*)(struct usb_device_t *,u32));
void usb_g_ep_config(usb_dev,unsigned,unsigned,unsigned,void *,unsigned);
void usb_enable_ep(usb_dev,unsigned);
u8 *usb_get_setup_buffer(struct usb_device_t *);
void usb_set_setup_phase(struct usb_device_t *,unsigned);
void usb_set_data_payload(struct usb_device_t *,struct usb_ctrlrequest *,u8 *,unsigned);
u32 usb_set_interface_hander(usb_dev,unsigned,u32 (*)(struct usb_device_t *,struct usb_ctrlrequest *));
u32 usb_set_reset_hander(usb_dev,unsigned,void (*)(struct usb_device_t *,u32));
u32 fm1_midi_desc_config(usb_dev,u8 *,u32 *);
unsigned usb_read_txcsr(usb_dev,unsigned);
void *usb_get_dma_taddr(usb_dev,unsigned);
void usb_write_txcsr(usb_dev,unsigned,unsigned);
void __local_irq_disable(void);void __local_irq_enable(void);void __asm_csync(void);
#endif
