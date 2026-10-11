#ifndef FM1_UVC_FAKE_SDK_H
#define FM1_UVC_FAKE_SDK_H
#include <stdint.h>
#include <stddef.h>

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint8_t usb_dev;
#define FM1_USB_CONTROLLER 0
#define USB0 0
#define BIT(bit) (1u << (bit))
#define USB_SETUP_SIZE 768u
#define USB_RECIP_MASK 0x1fu
#define USB_RECIP_INTERFACE 1u
#define USB_RECIP_ENDPOINT 2u
#define USB_REQ_SET_CONFIGURATION 9u
#define USB_REQ_GET_STATUS 0u
#define USB_ENDPOINT_XFER_ISOC 1u
#define USB_EP0_STAGE_SETUP 0u
#define USB_EP0_STAGE_IN 1u
#define USB_EP0_STAGE_OUT 2u
#define USB_EP0_SET_STALL 3u
#define USB_DEFAULT 2u
#define USB_ADDRESS 3u
#define USB_CONFIGURED 4u

struct usb_ctrlrequest {
    uint8_t bRequestType, bRequest;
    uint16_t wValue, wIndex, wLength;
};
struct usb_device_t {
    uint16_t wDataLength;
    uint8_t bsetup_phase, usb_id, bDeviceStates;
};
typedef u32 (*itf_hander)(struct usb_device_t *, struct usb_ctrlrequest *);
typedef void (*itf_reset_hander)(struct usb_device_t *, u32);
typedef void (*usb_interrupt)(struct usb_device_t *, u32);

extern unsigned fm1_uvc_test_irq_mask;
#define local_irq_save(flags) do { (flags)=fm1_uvc_test_irq_mask; fm1_uvc_test_irq_mask=1; } while(0)
#define local_irq_restore(flags) do { fm1_uvc_test_irq_mask=(flags); } while(0)
uint32_t timer_get_ms(void);
usb_dev usb_device2id(const struct usb_device_t *);
void usb_set_setup_phase(struct usb_device_t *, u8);
void usb_clr_intr_txe(usb_dev, u32);
u32 usb_g_set_intr_hander(usb_dev, u32, usb_interrupt);
void usb_disable_ep(usb_dev, u32);
void usb_enable_ep(usb_dev, u32);
void usb_write_txcsr(usb_dev, u32, u32);
u32 usb_read_txcsr(usb_dev, u32);
void usb_write_ep_cnt(usb_dev, u32, u32);
void usb_set_dma_taddr(usb_dev, u32, void *);
u32 usb_g_ep_config(usb_dev, u32, u32, u32, u8 *, u32);
void usb_write_txmaxp(usb_dev, u32, u32);
u32 usb_read_count0(usb_dev);
void usb_read_ep0(usb_dev, u8 *, u32);
void usb_ep0_Set_Stall(usb_dev);
u8 *usb_set_data_payload(struct usb_device_t *, struct usb_ctrlrequest *, const void *, u32);
void usb_set_setup_recv(struct usb_device_t *, itf_hander);
u32 usb_set_interface_hander(usb_dev, u32, itf_hander);
u32 usb_set_reset_hander(usb_dev, u32, itf_reset_hander);
void fm1_uvc_test_sync(void);
void sys_local_irq_disable(void);
void sys_local_irq_enable(void);
#endif
