#ifndef FM1_MOD_H
#define FM1_MOD_H
#include <stddef.h>
#include <stdint.h>

#define FM1_MOD_CAPACITY (256u * 1024u)
#define FM1_MOD_RATE 44100u
#define FM1_MOD_BLOCK 128u
#define FM1_MOD_QUICK_SLOTS 8u
#define FM1_MOD_ENTRY_STEPS 8u
#define FM1_MOD_CLIPBOARD_SIZE 1024u

typedef struct {
    uint16_t period;
    uint8_t sample, effect, param;
} fm1_mod_cell;

typedef struct {
    uint8_t bytes[FM1_MOD_CAPACITY];
    size_t length, sample_offset;
    uint16_t orders, patterns;
    uint8_t dirty, loaded;
} fm1_mod_document;

/* A document owns its exact imported bytes. No input is executed or written to flash. */
int fm1_mod_open(fm1_mod_document *d, size_t length);
void fm1_mod_new(fm1_mod_document *d);
int fm1_mod_cell_get(const fm1_mod_document *d, unsigned pattern, unsigned row,
                     unsigned channel, fm1_mod_cell *cell);
int fm1_mod_cell_set(fm1_mod_document *d, unsigned pattern, unsigned row,
                     unsigned channel, fm1_mod_cell cell);
int fm1_mod_add_pattern(fm1_mod_document *d);
int fm1_mod_duplicate_pattern(fm1_mod_document *d, unsigned pattern);
int fm1_mod_get_order(const fm1_mod_document *d, unsigned position);
int fm1_mod_set_order(fm1_mod_document *d, unsigned position, unsigned pattern);
int fm1_mod_append_order(fm1_mod_document *d, unsigned pattern);
int fm1_mod_insert_order(fm1_mod_document *d, unsigned position, unsigned pattern);
int fm1_mod_delete_order(fm1_mod_document *d, unsigned position);
int fm1_mod_duplicate_order(fm1_mod_document *d, unsigned position);
int fm1_mod_set_title(fm1_mod_document *d, const char *title);
int fm1_mod_set_sample_name(fm1_mod_document *d, unsigned sample, const char *name);
int fm1_mod_set_sample_volume(fm1_mod_document *d, unsigned sample, unsigned volume);
int fm1_mod_set_sample_finetune(fm1_mod_document *d, unsigned sample, unsigned finetune);
/* PCM/loop sizes are even MOD word lengths. Failures leave all bytes unchanged.
 * pcm must be external storage or the same slot's existing payload. */
int fm1_mod_replace_sample(fm1_mod_document *d, unsigned slot, const char *name,
    const int8_t *pcm, size_t length, unsigned volume, unsigned finetune,
    size_t loop_start, size_t loop_length);
const int8_t *fm1_mod_sample(const fm1_mod_document *d, unsigned sample, size_t *length,
                            size_t *loop_start, size_t *loop_length);
uint16_t fm1_mod_period_for_midi(unsigned midi);
uint32_t fm1_mod_crc32(const uint8_t *bytes, size_t length);

typedef struct {
    uint64_t phase;
    uint32_t step;
    uint16_t period, base_period, target_period, delayed_period;
    uint8_t sample, volume, finetune, effect, param, sample_offset, porta_speed;
    uint8_t active, pan, vibrato_speed, vibrato_depth, vibrato_pos;
} fm1_mod_voice;

typedef struct {
    fm1_mod_document *document;
    fm1_mod_voice voices[4];
    uint8_t meter[4];
    int8_t scope[4][48];
    uint8_t scope_head;
    uint32_t clock;
    uint8_t order, row, tick, speed, bpm, playing, stereo; /* playing: 0 stopped, 1 song, 2 pattern. */
    uint8_t jump_order, break_row, jump_pending, break_pending;
    uint8_t row_delay, delayed_cycle;
    uint64_t preview_phase;
    uint32_t preview_step;
    uint8_t preview_sample, preview_volume, preview_channel, preview_active;
    uint8_t current_order, current_row; /* Last row whose notes were triggered. */
    const int8_t *preview_pcm;
    size_t preview_length, preview_loop_start, preview_loop_length;
} fm1_mod_player;

void fm1_mod_player_start(fm1_mod_player *p, fm1_mod_document *d);
int fm1_mod_player_start_at(fm1_mod_player *p, fm1_mod_document *d,
                            unsigned order, unsigned row);
int fm1_mod_player_start_pattern_at(fm1_mod_player *p, fm1_mod_document *d,
                                    unsigned order, unsigned row);
void fm1_mod_player_stop(fm1_mod_player *p);
void fm1_mod_render(fm1_mod_player *p, int16_t *stereo, size_t frames);
/* Render the same player once with tracker panning, without changing its
 * local-output stereo mode. USB capture taps this before speaker downmix. */
void fm1_mod_render_stereo(fm1_mod_player *p, int16_t *stereo, size_t frames);
int fm1_mod_preview_start(fm1_mod_player *p, fm1_mod_document *d,
                          unsigned sample, unsigned midi, unsigned channel);
int fm1_mod_preview_pcm(fm1_mod_player *p, const int8_t *pcm, size_t length,
    unsigned volume, unsigned finetune, size_t loop_start, size_t loop_length,
    unsigned midi, unsigned channel);
void fm1_mod_preview_stop(fm1_mod_player *p);

enum {
    FM1_FIELD_NOTE, FM1_FIELD_SAMPLE, FM1_FIELD_EFFECT, FM1_FIELD_PARAM,
    FM1_FIELD_COUNT
};
extern const uint8_t fm1_mod_entry_steps[FM1_MOD_ENTRY_STEPS];

typedef struct {
    fm1_mod_document *document;
    uint8_t order, row, channel, sample, step, edit, sample_mode;
    int8_t octave;
    uint8_t field, quick[FM1_MOD_QUICK_SLOTS];
    uint8_t block_order, block_anchor_row, block_anchor_channel;
    uint8_t block_end_row, block_end_channel, block_active;
    uint8_t clipboard[FM1_MOD_CLIPBOARD_SIZE];
    uint8_t clipboard_width, clipboard_height, clipboard_valid;
} fm1_mod_editor;

void fm1_mod_editor_init(fm1_mod_editor *e, fm1_mod_document *d);
int fm1_mod_editor_note(fm1_mod_editor *e, unsigned midi);
int fm1_mod_editor_clear_note(fm1_mod_editor *e);
int fm1_mod_editor_clear_effect(fm1_mod_editor *e);
int fm1_mod_editor_clear_cell(fm1_mod_editor *e);
int fm1_mod_editor_clear(fm1_mod_editor *e);
int fm1_mod_editor_effect(fm1_mod_editor *e, unsigned effect, unsigned param);
void fm1_mod_editor_move(fm1_mod_editor *e, int rows, int channels, int orders);
int fm1_mod_editor_field_move(fm1_mod_editor *e, int delta);
/* NOTE value is MIDI 36..71; SAMPLE/EFFECT/PARAM are 0..31/15/255. */
int fm1_mod_editor_set_field(fm1_mod_editor *e, unsigned value);
int fm1_mod_editor_adjust_field(fm1_mod_editor *e, int delta);
int fm1_mod_editor_transpose(fm1_mod_editor *e, int semitones);
/* high=0 edits low nibble, high=1 edits high; each nibble clamps at 0/F. */
int fm1_mod_editor_param_nibble(fm1_mod_editor *e, unsigned high, int delta);
int fm1_mod_editor_clear_field(fm1_mod_editor *e);
int fm1_mod_editor_clear_all(fm1_mod_editor *e);
int fm1_mod_editor_set_step(fm1_mod_editor *e, unsigned step);
int fm1_mod_editor_step_move(fm1_mod_editor *e, int delta);
/* Quick indices are 0..7; binding uses e->sample and selection is silent. */
int fm1_mod_editor_quick_bind(fm1_mod_editor *e, unsigned index);
int fm1_mod_editor_quick_select(fm1_mod_editor *e, unsigned index);
int fm1_mod_editor_block_start(fm1_mod_editor *e);
int fm1_mod_editor_block_update(fm1_mod_editor *e);
void fm1_mod_editor_block_clear(fm1_mod_editor *e);
int fm1_mod_editor_block_copy(fm1_mod_editor *e);
int fm1_mod_editor_block_paste(fm1_mod_editor *e);
int fm1_mod_editor_sync_player(fm1_mod_editor *e, const fm1_mod_player *p);
void fm1_mod_format_cell(fm1_mod_cell cell, char out[12]);

#endif
