#ifndef FM1_MOD_H
#define FM1_MOD_H
#include <stddef.h>
#include <stdint.h>

#define FM1_MOD_CAPACITY (256u * 1024u)
#define FM1_MOD_RATE 44100u
#define FM1_MOD_BLOCK 128u

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
int fm1_mod_set_order(fm1_mod_document *d, unsigned position, unsigned pattern);
int fm1_mod_set_title(fm1_mod_document *d, const char *title);
int fm1_mod_set_sample_name(fm1_mod_document *d, unsigned sample, const char *name);
int fm1_mod_set_sample_volume(fm1_mod_document *d, unsigned sample, unsigned volume);
int fm1_mod_set_sample_finetune(fm1_mod_document *d, unsigned sample, unsigned finetune);
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
    uint8_t order, row, tick, speed, bpm, playing, stereo;
    uint8_t jump_order, break_row, jump_pending, break_pending;
    uint8_t row_delay, delayed_cycle;
    uint64_t preview_phase;
    uint32_t preview_step;
    uint8_t preview_sample, preview_volume, preview_channel, preview_active, preview_key;
} fm1_mod_player;

void fm1_mod_player_start(fm1_mod_player *p, fm1_mod_document *d);
void fm1_mod_player_stop(fm1_mod_player *p);
void fm1_mod_render(fm1_mod_player *p, int16_t *stereo, size_t frames);
int fm1_mod_preview_start(fm1_mod_player *p, fm1_mod_document *d,
                          unsigned sample, unsigned midi, unsigned channel);
void fm1_mod_preview_stop(fm1_mod_player *p);

typedef struct {
    fm1_mod_document *document;
    uint8_t order, row, channel, sample, step, edit, sample_mode;
    int8_t octave;
} fm1_mod_editor;

void fm1_mod_editor_init(fm1_mod_editor *e, fm1_mod_document *d);
int fm1_mod_editor_note(fm1_mod_editor *e, unsigned midi);
int fm1_mod_editor_clear(fm1_mod_editor *e);
int fm1_mod_editor_effect(fm1_mod_editor *e, unsigned effect, unsigned param);
void fm1_mod_editor_move(fm1_mod_editor *e, int rows, int channels, int orders);
void fm1_mod_format_cell(fm1_mod_cell cell, char out[12]);

#endif
