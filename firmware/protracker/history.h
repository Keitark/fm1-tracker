#ifndef FM1_MOD_HISTORY_H
#define FM1_MOD_HISTORY_H
#include "mod.h"

#define FM1_MOD_HISTORY_DEPTH 8u
#define FM1_MOD_HEADER_BYTES 1084u
#define FM1_MOD_PATTERN_BYTES 1024u
enum { FM1_HISTORY_PATTERN, FM1_HISTORY_HEADER, FM1_HISTORY_STRUCTURE };

typedef struct { uint8_t order, row, channel, field; } fm1_mod_history_cursor;
typedef struct {
    uint32_t length, sample_offset;
    uint16_t orders, patterns;
} fm1_mod_history_state;
typedef struct {
    uint8_t bytes[FM1_MOD_HEADER_BYTES]; /* Pattern OR header, according to scope. */
    fm1_mod_history_state before, after;
    fm1_mod_history_cursor before_cursor, after_cursor;
    uint32_t before_id, after_id;
    uint32_t before_crc, after_crc;
    uint32_t header_crc, source_crc;
    uint8_t scope, pattern, source_pattern, creation;
} fm1_mod_history_record;
/* External fixed storage: about 10.3 KiB. Target may place it in cache SRAM.
 * No sample PCM is retained; replacing PCM must explicitly reset history. */
typedef struct {
    fm1_mod_history_record records[FM1_MOD_HISTORY_DEPTH], pending;
    fm1_mod_document *document;
    uint32_t current_id, next_id, saved_id;
    uint8_t start, count, cursor, active, saved_valid;
} fm1_mod_history;

/* init also establishes a barrier after upload/new/accepted sample replacement.
 * It preserves document dirty state. Call mark_saved after a completed export. */
void fm1_mod_history_init(fm1_mod_history *, fm1_mod_document *);
int fm1_mod_history_begin(fm1_mod_history *, const fm1_mod_editor *, unsigned scope);
/* 1=recorded, 0=no change, -1=unsupported/invalid shape (history is reset).
 * PATTERN stores one pattern and rejects header changes. HEADER stores header
 * metadata only. STRUCTURE supports an appended blank/unchanged-source clone,
 * including header/order changes; redo verifies the retained source recipe.
 * Sample replacement is not a supported transaction, even at the same length. */
int fm1_mod_history_commit(fm1_mod_history *, const fm1_mod_editor *);
/* Cancel after a failed transactional operation. It does not roll back bytes. */
void fm1_mod_history_cancel(fm1_mod_history *);
int fm1_mod_history_undo(fm1_mod_history *, fm1_mod_editor *);
int fm1_mod_history_redo(fm1_mod_history *, fm1_mod_editor *);
void fm1_mod_history_mark_saved(fm1_mod_history *);
unsigned fm1_mod_history_undo_count(const fm1_mod_history *);
unsigned fm1_mod_history_redo_count(const fm1_mod_history *);
#endif
