#ifndef FM1_BANK_H
#define FM1_BANK_H
#include "mod.h"
#include <stdint.h>

#define FM1_BANK_MAX_ENTRIES 256u
#define FM1_BANK_HEADER_SIZE 32u
#define FM1_BANK_ENTRY_SIZE 48u
#define FM1_BANK_NAME_SIZE 20u
#define FM1_BANK_TAG_DRUM 1u
#define FM1_BANK_TAG_BREAK 2u
#define FM1_BANK_TAG_BASS 4u
#define FM1_BANK_TAG_MUSICAL 8u
#define FM1_BANK_TAG_FX 16u
#define FM1_BANK_TAG_VOCAL 32u

/* Views reference immutable bank storage. No allocation, file I/O or flash write. */
typedef struct {
    const uint8_t *bytes;
    uint32_t length;
    uint16_t count;
} fm1_bank;

typedef struct {
    uint32_t id, length, loop_start, loop_length, tags;
    const char *name; /* NUL-terminated ASCII, at most 19 characters. */
    const int8_t *pcm;
    uint8_t volume;
    int8_t finetune; /* -8 .. 7. */
} fm1_bank_entry;

/* On failure bank is cleared, preventing use of a previously loaded view. */
int fm1_bank_load(fm1_bank *bank, const uint8_t *bytes, uint32_t length);
int fm1_bank_entry_get(const fm1_bank *bank, unsigned index, fm1_bank_entry *entry);
int fm1_bank_find_id(const fm1_bank *bank, uint32_t id);
/* Explicitly materializes one bank entry in song slot 1..31. */
int fm1_bank_import(const fm1_bank *bank, unsigned index,
                    fm1_mod_document *document, unsigned slot);
#endif
