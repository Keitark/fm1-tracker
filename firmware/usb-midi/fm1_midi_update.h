/* SPDX-License-Identifier: Apache-2.0 */
#ifndef FM1_MIDI_UPDATE_H
#define FM1_MIDI_UPDATE_H
#include <stdint.h>
typedef int (*fm1_midi_send_fn)(void *,unsigned,const uint8_t *,unsigned);
typedef void (*fm1_midi_quiesce_fn)(void *);
typedef int (*fm1_midi_idle_fn)(void *);
void fm1_midi_update_init(fm1_midi_send_fn,fm1_midi_quiesce_fn,fm1_midi_idle_fn,void *);
void fm1_midi_update_message(void *,unsigned,const uint8_t *,unsigned);
void fm1_midi_update_poll(uint32_t);
void fm1_midi_update_reset(void);
unsigned fm1_midi_update_busy(void);
extern const uint8_t fm1_midi_build_id[32];
#endif
