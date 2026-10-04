#ifndef FM1_VOLUME_H
#define FM1_VOLUME_H
#include <stdint.h>
/* Caller serializes start/stop/tick/snapshot. Tick exactly every2ms; no waits,
   IRQ ownership, allocation, logging or timer operations inside this driver. */
typedef struct {
    uint32_t samples,errors;
    uint16_t raw,accepted;
    uint8_t running,valid,target,debounce,waiting;
} fm1_volume;
int fm1_volume_start(fm1_volume *);
void fm1_volume_stop(fm1_volume *);
void fm1_volume_tick(fm1_volume *);
#endif
