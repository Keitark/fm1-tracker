#ifndef FM1_BOOT_ENTRY_H
#define FM1_BOOT_ENTRY_H
#include <stdint.h>
unsigned fm1_usb_rx_take(uint8_t *out, unsigned limit);
int fm1_usb_boot_arm(void);
int fm1_usb_rx_fault(void);
int fm1_usb_boot_pending(void);
unsigned fm1_usb_rx_generation(void);
#endif
