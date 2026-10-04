#ifndef FM1_DIAG_PROTOCOL_H
#define FM1_DIAG_PROTOCOL_H
#include <stdint.h>
#include <stddef.h>
#define FM1_DIAG_LIMIT (512u * 1024u)
#define FM1_DIAG_LINE 300
typedef struct {
    uint32_t length, expected_crc, received, crc, last_ms;
    unsigned used;
    unsigned active, verified, dropping, boot_requested, test_requested;
    char line[FM1_DIAG_LINE];
} fm1_diag_protocol;
typedef void (*fm1_diag_reply)(void *, const char *);
void fm1_diag_reset(fm1_diag_protocol *p);
void fm1_diag_feed(fm1_diag_protocol *p, const uint8_t *data, size_t n,
                   uint32_t now, fm1_diag_reply reply, void *ctx);
void fm1_diag_tick(fm1_diag_protocol *p, uint32_t now, fm1_diag_reply reply, void *ctx);
#endif
