#ifndef FM1_RX_CHANNEL_H
#define FM1_RX_CHANNEL_H
#include <stddef.h>
#include <stdint.h>
#define FM1_RX_CAPACITY 1024u
#define FM1_BOOT_TIMEOUT_MS 5000u
#define FM1_BOOT_CONFIRM "UBOOT CONFIRM\n"
/* Caller serializes access: USB IRQ producer; task uses an IRQ critical section. */
typedef struct {
    uint8_t bytes[FM1_RX_CAPACITY];
    unsigned read, write, armed, matched, fault, generation;
    uint32_t armed_ms;
} fm1_rx_channel;
void fm1_rx_reset(fm1_rx_channel *c, unsigned generation);
void fm1_rx_sync(fm1_rx_channel *c, unsigned generation, int ready, uint32_t now);
unsigned fm1_rx_take(fm1_rx_channel *c, uint8_t *out, unsigned limit);
int fm1_rx_arm(fm1_rx_channel *c, uint32_t now);
/* True only for a complete, exact confirmation while armed, at packet end. */
int fm1_rx_receive(fm1_rx_channel *c, const uint8_t *data, unsigned n, uint32_t now);
#endif
