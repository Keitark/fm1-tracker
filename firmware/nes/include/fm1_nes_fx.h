#ifndef FM1_NES_FX_H
#define FM1_NES_FX_H
#include <stdint.h>
#include <stddef.h>
enum { FM1_FX_BYPASS, FM1_FX_LP, FM1_FX_BP, FM1_FX_HP };
typedef struct {
    uint8_t mode,cutoff,resonance,rate,depth,mix,preset;
    int8_t mode_edges,preset_edges;
} fm1_fx_controls;
typedef struct {
    int32_t low,band,frequency,damping,wet;
    uint32_t phase,phase_increment,limited;
    uint8_t mode,rate_cache;
} fm1_fx_state;
void fm1_fx_controls_reset(fm1_fx_controls *);
/* Physical encoder IDs: SELECT, ALGORITHM, KNOB1..4, PRESETS. Deltas are
   decoded CONTACT EDGES, not assumed detents or clockwise direction.
   ALGORITHM/PRESETS use four net edges per choice; other knobs one per step. */
void fm1_fx_control_edges(fm1_fx_controls *,unsigned encoder,int32_t edges);
void fm1_fx_reset(fm1_fx_state *);
/* Task-only, no locks, I/O, allocation or waits. Up to64 mono PCM16 frames,
   in-place allowed. Each control is0..127; mode0..3. Fixed44100Hz processing.
   Call once before queue-push retries, never once per retry. */
int fm1_fx_process(fm1_fx_state *,const fm1_fx_controls *,const int16_t *,int16_t *,size_t);
#endif
