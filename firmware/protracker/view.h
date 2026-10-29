#ifndef FM1_TRACKER_VIEW_H
#define FM1_TRACKER_VIEW_H
#include "library.h"

#define FM1_VIEW_PATTERN_COLUMNS 30u
#define FM1_VIEW_LIST_COLUMNS 20u
#define FM1_VIEW_HELP_TOPICS 5u
enum { FM1_SAMPLE_FIELD_VOLUME, FM1_SAMPLE_FIELD_FINETUNE,
       FM1_SAMPLE_FIELD_LOOP_START, FM1_SAMPLE_FIELD_LOOP_LENGTH,
       FM1_SAMPLE_FIELD_COUNT };
enum { FM1_VIEW_CONFIRM_NONE, FM1_VIEW_CONFIRM_NEW, FM1_VIEW_CONFIRM_BANK };

/* Snapshot shared by the device and offline preview. No heap allocations. */
typedef struct {
    char ui[10][40];
    char grid[10][40];
    int8_t scope[4][48];
    uint8_t page, loaded, channel, write, sample_mode;
    uint8_t field, block_mask[9];
    uint8_t peak[4];
    int8_t wave_min[120], wave_max[120];
    uint16_t beat_rows;
    uint8_t wave_valid, wave_loop, wave_start, wave_end;
    uint8_t overlay, sample_field, mute_mask;
} fm1_tracker_view;

typedef struct {
    uint8_t arranger, bank_open, quick_modifier, block_modifier, new_hold;
    uint8_t sample_open, sample_field, help_open, help_topic;
    uint8_t undo_count, redo_count, mute_mask, solo_mask, confirm, save_modifier;
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
