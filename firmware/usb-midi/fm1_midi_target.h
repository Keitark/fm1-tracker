/* SPDX-License-Identifier: Apache-2.0 */
#ifndef FM1_MIDI_TARGET_H
#define FM1_MIDI_TARGET_H
#include <stdint.h>
void fm1_usb_midi_init(void);
void fm1_usb_midi_poll(uint32_t now);
#endif
