/* SPDX-License-Identifier: Apache-2.0 */
#ifndef FM1_MIDI_H
#define FM1_MIDI_H
#include <stddef.h>
#include <stdint.h>
#define FM1_MIDI_PAYLOAD_MAX 520u
#define FM1_MIDI_RAW_MAX (7u + FM1_MIDI_PAYLOAD_MAX)
#define FM1_MIDI_FRAME_MAX (2u + (FM1_MIDI_RAW_MAX * 8u + 6u) / 7u)
typedef void (*fm1_midi_message_fn)(void *,unsigned,const uint8_t *,unsigned);
typedef struct {
    uint8_t frame[FM1_MIDI_FRAME_MAX];
    unsigned used,discarded,errors;
    fm1_midi_message_fn message;
    void *context;
} fm1_midi_parser;
void fm1_midi_parser_init(fm1_midi_parser *,fm1_midi_message_fn,void *);
/* Four-byte USB-MIDI 1.0 event packets, cable zero only. */
void fm1_midi_events(fm1_midi_parser *,const uint8_t *,unsigned);
/* cmd 0x100 is the exact unencoded vendor update trigger. */
unsigned fm1_midi_frame(unsigned,const uint8_t *,unsigned,uint8_t *,unsigned);
unsigned fm1_midi_event_encode(const uint8_t *,unsigned,uint8_t *,unsigned);
/* USB-MIDI descriptors appended after CDC and optional UAC. */
unsigned fm1_midi_descriptor(uint8_t *,unsigned);
#endif
