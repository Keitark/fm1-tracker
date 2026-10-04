#ifndef FM1_CHANNEL_FX_H
#define FM1_CHANNEL_FX_H
#include "fm1_nes_fx.h"
enum { FM1_VOICE_PULSE1,FM1_VOICE_PULSE2,FM1_VOICE_TRIANGLE,FM1_VOICE_NOISE,FM1_VOICE_MASTER,FM1_VOICE_COUNT };
typedef struct {
    fm1_fx_controls controls[FM1_VOICE_COUNT];
    fm1_fx_state state[FM1_VOICE_COUNT];
    int32_t dc[FM1_VOICE_COUNT];
    uint32_t limited;
    uint8_t selected,started;
    int8_t select_edges;
} fm1_channel_fx;
void fm1_channel_fx_reset(fm1_channel_fx *);
/* SELECT chooses one of five banks (four net edges/choice, clamped).
   Other encoders modify only that bank, retaining all other controls/states. */
void fm1_channel_fx_edges(fm1_channel_fx *,unsigned,int32_t);
const char *fm1_channel_fx_name(unsigned);
/* Up to64 frames, gain0..128 matches the board. Four raw0..15 voices,
   original mixed0..255 bytes, one PCM16 output. Invalid input is atomic.
   Task-only; no I/O, allocation, locks or waits. Master runs after summing. */
int fm1_channel_fx_process(fm1_channel_fx *,const uint8_t *,const uint8_t *const[4],
                           uint8_t gain,int16_t *,size_t);
#endif
