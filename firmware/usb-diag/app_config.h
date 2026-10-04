#ifndef APP_CONFIG_H
#define APP_CONFIG_H
#define __FLASH_SIZE__ (1024 * 1024)
#define __SDRAM_SIZE__ 0
#define LIB_DEBUG 0
#define CONFIG_DEBUG_LIB(x) 0
#define TCFG_PC_ENABLE 1
#define USB_DEVICE_CLASS_CONFIG 0x10
#define USB_DEVICE_CLASS_CONFIG_2_0 0
#define USB_MALLOC_ENABLE 1
#define CDC_DATA_EP_IN 3
#define CDC_DATA_EP_OUT 2
#define CDC_INTR_EP_IN 2
#define CDC_INTR_EP_ENABLE 1
#define MAXP_SIZE_CDC_BULKIN 64
#define MAXP_SIZE_CDC_BULKOUT 64
#define MAXP_SIZE_CDC_INTRIN 16
#include "usb/usb_common_def.h"
#include "usb/usb_std_class_def.h"
#ifndef FM1_USB_CONTROLLER
#error "Select one controller explicitly; physical connector routing is not yet qualified"
#endif
#endif
