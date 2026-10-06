#include "bank.h"
#include <string.h>

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (uint16_t)p[1] << 8); }
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint32_t bank_crc(const uint8_t *p, uint32_t n) {
    uint32_t crc = 0xffffffffu, i; unsigned bit;
    for (i = 0; i < n; ++i) {
        crc ^= i >= 28 && i < 32 ? 0 : p[i];
        for (bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
int fm1_bank_load(fm1_bank *bank, const uint8_t *bytes, uint32_t length) {
    uint32_t cursor, prior_id = 0; unsigned i, j, count;
    if (!bank) return -1;
    memset(bank, 0, sizeof(*bank));
    if (!bytes || length < FM1_BANK_HEADER_SIZE || memcmp(bytes, "FM1BANK\0", 8)) return -1;
    count = le16(bytes + 12);
    if (le16(bytes + 8) != 1 || le16(bytes + 10) != FM1_BANK_ENTRY_SIZE ||
        !count || count > FM1_BANK_MAX_ENTRIES || le16(bytes + 14) ||
        le32(bytes + 16) != length || le32(bytes + 20) != FM1_BANK_HEADER_SIZE) return -1;
    cursor = FM1_BANK_HEADER_SIZE + count * FM1_BANK_ENTRY_SIZE;
    if (cursor > length || le32(bytes + 24) != cursor || le32(bytes + 28) != bank_crc(bytes, length)) return -1;
    for (i = 0; i < count; ++i) {
        const uint8_t *p = bytes + FM1_BANK_HEADER_SIZE + i * FM1_BANK_ENTRY_SIZE;
        uint32_t id = le32(p), offset = le32(p + 24), n = le32(p + 28);
        uint32_t start = le32(p + 32), loop = le32(p + 36), tags = le32(p + 40);
        int tune = p[45] < 128 ? p[45] : (int)p[45] - 256;
        if (!id || id <= prior_id || offset != cursor || !n || n > 131070u || (n & 1u) ||
            n > length - cursor || p[44] > 64 || tune < -8 || tune > 7 || le16(p + 46) ||
            (tags & ~63u) || (start & 1u) || (loop & 1u) || start > n || loop > n - start ||
            (loop ? loop < 4 : start != 0)) return -1;
        for (j = 0; j < FM1_BANK_NAME_SIZE && p[4 + j]; ++j)
            if (p[4 + j] < 32 || p[4 + j] > 126) return -1;
        if (!j || j == FM1_BANK_NAME_SIZE) return -1;
        for (++j; j < FM1_BANK_NAME_SIZE; ++j) if (p[4 + j]) return -1;
        prior_id = id; cursor += n;
    }
    if (cursor != length) return -1;
    bank->bytes = bytes; bank->length = length; bank->count = (uint16_t)count;
    return 0;
}
int fm1_bank_entry_get(const fm1_bank *bank, unsigned index, fm1_bank_entry *entry) {
    const uint8_t *p;
    if (!bank || !bank->bytes || index >= bank->count || !entry) return -1;
    p = bank->bytes + FM1_BANK_HEADER_SIZE + index * FM1_BANK_ENTRY_SIZE;
    entry->id = le32(p); entry->name = (const char *)p + 4;
    entry->pcm = (const int8_t *)(bank->bytes + le32(p + 24));
    entry->length = le32(p + 28); entry->loop_start = le32(p + 32);
    entry->loop_length = le32(p + 36); entry->tags = le32(p + 40);
    entry->volume = p[44]; entry->finetune = (int8_t)(p[45] < 128 ? p[45] : (int)p[45] - 256);
    return 0;
}
int fm1_bank_find_id(const fm1_bank *bank, uint32_t id) {
    unsigned first = 0, last;
    if (!bank || !bank->bytes) return -1;
    last = bank->count;
    while (first < last) {
        unsigned middle = first + (last - first) / 2;
        uint32_t found = le32(bank->bytes + FM1_BANK_HEADER_SIZE + middle * FM1_BANK_ENTRY_SIZE);
        if (id == found) return (int)middle;
        if (id < found) last = middle; else first = middle + 1;
    }
    return -1;
}
int fm1_bank_import(const fm1_bank *bank, unsigned index, fm1_mod_document *document, unsigned slot) {
    fm1_bank_entry entry;
    if (fm1_bank_entry_get(bank, index, &entry)) return -1;
    return fm1_mod_replace_sample(document, slot, entry.name, entry.pcm, entry.length,
                                   entry.volume, (unsigned)entry.finetune & 15u, entry.loop_start, entry.loop_length);
}
