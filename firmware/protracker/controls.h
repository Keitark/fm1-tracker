#ifndef FM1_TRACKER_CONTROLS_H
#define FM1_TRACKER_CONTROLS_H
#include "mod.h"

enum { FM1_CONTROL_NONE, FM1_CONTROL_KEY, FM1_CONTROL_QUICK_SELECT,
       FM1_CONTROL_QUICK_BIND, FM1_CONTROL_NEW_SONG };
typedef struct { uint8_t kind, index, sample; } fm1_tracker_control_event;
typedef struct {
    uint16_t held;
    uint8_t edit_used, seq_used, save_used, new_pending, new_fired;
    uint32_t new_since;
    uint8_t quick_pending[8], quick_bound[8], quick_sample[8];
    uint32_t quick_since[8];
} fm1_tracker_controls;

/* EDIT is a momentary modifier for eight song-sample shortcuts. A short
 * release selects; a 600 ms hold binds the sample captured on key-down.
 * Therefore holding a shortcut never changes the sample before binding. */
void fm1_tracker_controls_init(fm1_tracker_controls *);
fm1_tracker_control_event fm1_tracker_controls_key(fm1_tracker_controls *,
    unsigned slot, unsigned down, uint32_t now, unsigned current_sample);
fm1_tracker_control_event fm1_tracker_controls_tick(fm1_tracker_controls *, uint32_t now);
void fm1_tracker_controls_use_seq(fm1_tracker_controls *);
void fm1_tracker_controls_use_save(fm1_tracker_controls *);
int fm1_tracker_controls_held(const fm1_tracker_controls *, unsigned slot);
/* Stable seven-encoder pattern mapping. Mutating fields is stopped-only;
 * sample/field/channel selection still works during playback. */
int fm1_tracker_controls_encoder(fm1_tracker_controls *, fm1_mod_editor *,
    unsigned running, unsigned id, int detents);
#endif
