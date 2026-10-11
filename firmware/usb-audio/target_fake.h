/* SPDX-License-Identifier: GPL-3.0-only
 * Reused from Keitark/fm1-mdx at 4141057; see licenses/fm1-mdx-GPL-3.0.txt. */
#ifndef FM1_UAC_FAKE_H
#define FM1_UAC_FAKE_H
#include <stdint.h>
typedef uint8_t u8,usb_dev;
typedef uint32_t u32;
typedef unsigned spinlock_t;
struct usb_device_t {unsigned bDeviceStates;uint8_t setup[64];};
struct usb_ctrlrequest {uint8_t bRequestType,bRequest;uint16_t wValue,wIndex,wLength;};
#define FM1_USB_CONTROLLER 0
#define USB_CONFIGURED 4
#define USB_EP0_SET_STALL 7
#define USB_EP0_STAGE_SETUP 0
#define USB_ENDPOINT_XFER_ISOC 1
#define TXCSRP_TxPktRdy 0x01
#define TXCSRP_FlushFIFO 0x08
#define TXCSRP_ClrDataTog 0x40
#define TXCSRP_ISOCHRONOUS 0x4000
#define RXCSRP_FlushFIFO 0x10
#define RXCSRP_ClrDataTog 0x80
#define RXCSRP_ISOCHRONOUS 0x4000
#define local_irq_save(f) do{f=0;}while(0)
#define local_irq_restore(f) ((void)(f))
void arch_spin_lock(spinlock_t *);
void arch_spin_unlock(spinlock_t *);
usb_dev usb_device2id(const struct usb_device_t *);
struct usb_device_t *usb_id2device(usb_dev);
u32 usb_g_iso_read(usb_dev,u32,void *,u32,u32);
u32 usb_g_iso_write(usb_dev,u32,void *,u32);
void usb_clr_intr_txe(usb_dev,u32);
void usb_clr_intr_rxe(usb_dev,u32);
void usb_enable_ep(usb_dev,u32);
u32 usb_read_txcsr(usb_dev,u32);
void usb_write_txcsr(usb_dev,u32,u32);
void usb_write_rxcsr(usb_dev,u32,u32);
void usb_set_intr_txe(usb_dev,u32);
void usb_set_intr_rxe(usb_dev,u32);
u32 usb_g_ep_config(usb_dev,u32,u32,u32,u8 *,u32);
u32 usb_g_set_intr_hander(usb_dev,u32,void (*)(struct usb_device_t *,u32));
u32 usb_set_interface_hander(usb_dev,u32,u32 (*)(struct usb_device_t *,struct usb_ctrlrequest *));
u32 usb_set_reset_hander(usb_dev,u32,void (*)(struct usb_device_t *,u32));
void usb_set_setup_phase(struct usb_device_t *,u8);
void *usb_get_setup_buffer(const struct usb_device_t *);
u8 *usb_set_data_payload(struct usb_device_t *,struct usb_ctrlrequest *,const void *,u32);
#endif
