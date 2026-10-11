/* SPDX-License-Identifier: GPL-3.0-only
 * Reused from Keitark/fm1-mdx at 4141057; see licenses/fm1-mdx-GPL-3.0.txt. */
#ifndef FM1_USB_AUDIO_TARGET_H
#define FM1_USB_AUDIO_TARGET_H
#include <stdint.h>
#include <stddef.h>
void fm1_usb_audio_init(void);
void fm1_usb_audio_stop(void);
void fm1_usb_audio_dac(int32_t *,unsigned);
void fm1_usb_audio_status(char *,size_t);
void fm1_usb_audio_transport_status(char *,size_t);
void fm1_usb_audio_microphone(int on);
void fm1_usb_audio_mic_status(char *,size_t);
/* Single volatile read, no IRQ masking/spinlock in the emulated CPU. */
unsigned fm1_usb_audio_mic_bits(void);
#endif
