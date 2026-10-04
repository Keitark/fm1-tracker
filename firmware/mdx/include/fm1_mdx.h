#ifndef FM1_MDX_H
#define FM1_MDX_H
#include "retrofm_mdx_sequence.h"
#include "retrofm_pcm.h"
#include "ym2151.h"
#define FM1_MDX_RATE 44100u
#define FM1_MDX_BLOCK 128u
/* One task owns all player calls. Backing files must outlive playback. */
typedef struct {
    retrofm_mdx mdx;
    retrofm_pdx pdx;
    retrofm_mdx_sequencer sequence;
    retrofm_pcm_mixer pcm;
    struct ym2151 opm;
    uint64_t cycles;
    uint32_t remainder,pcm_phase;
    uint16_t mute_mask;
    uint8_t selected,loaded,playing;
    int live_note[8];
    uint8_t song_registers[256],registers[256],key_mask[8];
    uint32_t suppressed_keys;
    retrofm_mdx_result error;
    int16_t pcm_left,pcm_right;
    stream_sample_t left[FM1_MDX_BLOCK],right[FM1_MDX_BLOCK];
} fm1_mdx_player;
int fm1_mdx_load(fm1_mdx_player *,const uint8_t *,size_t,const uint8_t *,size_t);
void fm1_mdx_stop(fm1_mdx_player *);
int fm1_mdx_mute(fm1_mdx_player *,unsigned track,int mute);
int fm1_mdx_select(fm1_mdx_player *,unsigned track);
int fm1_mdx_note(fm1_mdx_player *,unsigned midi_note,int down);
int fm1_mdx_render(fm1_mdx_player *,int16_t *stereo,size_t frames);
/* Public event gate also permits direct regression tests and tracing. */
void fm1_mdx_song_write(fm1_mdx_player *,uint8_t reg,uint8_t value);
#endif
