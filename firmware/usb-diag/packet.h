/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Keitark
 * See firmware/usb-audio/RELICENSE.md for the scoped license grant. */
#ifndef FM1_USB_PACKET_H
#define FM1_USB_PACKET_H
#include <stdint.h>
/* USB0 only: EP1 audio IN (192 bytes), EP3 CDC IN (1..63 bytes).
 * One attempt, no TxPktRdy polling. SDK register acknowledgement waits remain.
 * Epoch invalidation cancels a staged old-session packet.
 * Accepted bytes are copied into the configured DMA buffer before the doorbell.
 */
unsigned fm1_usb_packet_write(unsigned id,unsigned ep,const uint8_t *data,
                            unsigned size,const volatile unsigned *epoch,
                            unsigned expected);
#endif
