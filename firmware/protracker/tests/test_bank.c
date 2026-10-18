#include "bank.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static fm1_mod_document document;
static uint8_t fixture[FM1_BANK_HEADER_SIZE + FM1_BANK_MAX_ENTRIES * (FM1_BANK_ENTRY_SIZE + 4)];
static void put16(uint8_t *p, unsigned value) { p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8); }
static void put32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8); p[2] = (uint8_t)(value >> 16); p[3] = (uint8_t)(value >> 24);
}
static void crc(uint8_t *data, size_t length) {
    memset(data + 28, 0, 4); put32(data + 28, fm1_mod_crc32(data, length));
}
static size_t make_fixture(unsigned count) {
    unsigned i; size_t n = 32 + count * 52; uint32_t pcm = 32 + count * 48;
    memset(fixture, 0, sizeof(fixture)); memcpy(fixture, "FM1BANK\0", 8);
    put16(fixture + 8, 1); put16(fixture + 10, 48); put16(fixture + 12, count);
    put32(fixture + 16, (uint32_t)n); put32(fixture + 20, 32); put32(fixture + 24, pcm);
    for (i = 0; i < count; ++i) {
        uint8_t *entry = fixture + 32 + i * 48;
        put32(entry, 1000 + i); memcpy(entry + 4, "ORIGINAL", 8);
        put32(entry + 24, pcm + 4 * i); put32(entry + 28, 4);
        put32(entry + 40, FM1_BANK_TAG_DRUM); entry[44] = 64;
        fixture[pcm + i * 4] = 100; fixture[pcm + i * 4 + 1] = 70;
    }
    crc(fixture, n); return n;
}
static void reject(unsigned offset, uint8_t value) {
    fm1_bank bank; size_t n = make_fixture(2);
    assert(fm1_bank_load(&bank, fixture, (uint32_t)n) == 0);
    fixture[offset] = value; crc(fixture, n);
    assert(fm1_bank_load(&bank, fixture, (uint32_t)n) == -1 && !bank.bytes && !bank.count);
}
static void test_format(void) {
    fm1_bank bank; fm1_bank_entry entry; size_t n; unsigned i;
    n = make_fixture(FM1_BANK_MAX_ENTRIES);
    assert(fm1_bank_load(&bank, fixture, (uint32_t)n) == 0 && bank.count == 256);
    for (i = 0; i < 256; ++i) {
        assert(fm1_bank_entry_get(&bank, i, &entry) == 0 && entry.id == 1000 + i && entry.length == 4);
        assert(entry.pcm[0] == 100 && entry.pcm[1] == 70 && fm1_bank_find_id(&bank, entry.id) == (int)i);
    }
    assert(fm1_bank_entry_get(&bank, 256, &entry) == -1 && fm1_bank_find_id(&bank, 999) == -1);
    fixture[n - 1] ^= 1;
    assert(fm1_bank_load(&bank, fixture, (uint32_t)n) == -1);
    n = make_fixture(2);
    assert(fm1_bank_load(&bank, fixture, (uint32_t)n - 1) == -1);
    assert(fm1_bank_load(&bank, NULL, 0) == -1);
    reject(8, 2); reject(10, 47); reject(12, 0); reject(14, 1);
    reject(32 + 44, 65); reject(32 + 45, 8); reject(32 + 46, 1);
    reject(32 + 4, 0); reject(32 + 4, 31); reject(32 + 4 + 19, 'X');
    reject(32 + 28, 3); reject(32 + 32, 2); reject(32 + 36, 2); reject(32 + 40, 64);
    reject(32 + 48, 0xe8); /* Duplicates first ID1000 instead of1001. */
    reject(32 + 48 + 24, 128); /* Second PCM overlaps first. */
    n = make_fixture(1); put32(fixture + 32 + 28, UINT32_MAX); crc(fixture, n);
    assert(fm1_bank_load(&bank, fixture, (uint32_t)n) == -1);
    n = make_fixture(1); fixture[32 + 45] = 255; crc(fixture, n);
    assert(fm1_bank_load(&bank, fixture, (uint32_t)n) == 0);
    fm1_mod_new(&document);
    assert(fm1_bank_import(&bank, 0, &document, 1) == 0 && document.bytes[20 + 24] == 15);
}
static void test_original(const char *path) {
    FILE *file = fopen(path, "rb"); long length; uint8_t *bytes; fm1_bank bank; fm1_bank_entry entry;
    fm1_mod_player player = {0}; int16_t audio[512]; unsigned i; int heard = 0;
    assert(file); assert(fseek(file, 0, SEEK_END) == 0); length = ftell(file); assert(length > 0);
    assert(fseek(file, 0, SEEK_SET) == 0); bytes = malloc((size_t)length); assert(bytes);
    assert(fread(bytes, 1, (size_t)length, file) == (size_t)length); fclose(file);
    assert(length >= 64 * 1024 && length <= 96 * 1024);
    assert(fm1_bank_load(&bank, bytes, (uint32_t)length) == 0 && bank.count == 29);
    assert(fm1_bank_find_id(&bank, 601) >= 0);
    assert(fm1_bank_entry_get(&bank, (unsigned)fm1_bank_find_id(&bank, 601), &entry) == 0);
    assert(!strcmp(entry.name, "COME ON!") && (entry.tags & FM1_BANK_TAG_VOCAL));
    fm1_mod_new(&document);
    assert(fm1_bank_import(&bank, (unsigned)fm1_bank_find_id(&bank, 301), &document, 31) == 0);
    assert(document.dirty && document.loaded && fm1_mod_open(&document, document.length) == 0);
    assert(fm1_bank_entry_get(&bank, (unsigned)fm1_bank_find_id(&bank, 301), &entry) == 0);
    { size_t n, start, loop; const int8_t *pcm = fm1_mod_sample(&document, 31, &n, &start, &loop);
      assert(n == entry.length && start == entry.loop_start && loop == entry.loop_length && !memcmp(pcm, entry.pcm, n)); }
    document.dirty = 0;
    assert(fm1_mod_preview_pcm(&player, entry.pcm, entry.length, entry.volume, (unsigned)entry.finetune & 15,
                               entry.loop_start, entry.loop_length, 48, 2) == 0);
    fm1_mod_render(&player, audio, 256);
    for (i = 0; i < 512; ++i) if (audio[i]) heard = 1;
    assert(heard && !document.dirty);
    assert(fm1_bank_import(&bank, 0, &document, 0) == -1 && !document.dirty);
    fm1_mod_preview_stop(&player); free(bytes);
}
int main(int argc, char **argv) {
    test_format(); assert(argc == 2); test_original(argv[1]);
    puts("Active Bank parser, capacity, CRC, materialization and direct audition passed"); return 0;
}
