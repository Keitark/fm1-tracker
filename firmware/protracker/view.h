#ifndef FM1_TRACKER_VIEW_H
#define FM1_TRACKER_VIEW_H
#include "library.h"

/* Snapshot shared by the device and offline preview. No heap allocations. */
typedef struct {
    char ui[10][40];
    char grid[10][40];
    int8_t scope[4][48];
    uint8_t page, loaded, channel, write, sample_mode;
    uint8_t field, block_mask[9];
    uint8_t peak[4];
} fm1_tracker_view;

typedef struct {
    uint8_t arranger, bank_open, quick_modifier, block_modifier, new_hold;
    uint16_t bank_index, bank_count;
    char bank_name[24], notice[40];
} fm1_tracker_view_context;

void fm1_tracker_view_update(fm1_tracker_view *, const fm1_mod_document *,
    const fm1_mod_player *, const fm1_mod_editor *, const fm1_mod_library *,
    unsigned page, unsigned running, unsigned stereo, unsigned save_hint);
/* Compact arranger, bank audition and transient feedback retain the grid. */
void fm1_tracker_view_context_apply(fm1_tracker_view *, const fm1_mod_document *,
    const fm1_mod_editor *, const fm1_tracker_view_context *);
/* Render consecutive scanlines as RGB565 big endian, 480 bytes per row. */
void fm1_tracker_view_render(const fm1_tracker_view *, unsigned y,
    unsigned rows, uint8_t *rgb565_be);
#endif
