#ifndef FM1_BOOT_SERVICES_H
#define FM1_BOOT_SERVICES_H
#include "fm1_wl82.h"

/* Full replacement-app services. Not for injection into a running stock OS.
   No rail voltage change, invented reset pin, hardware mute or flash access.
   Caller owns the board; stock power/reset handoff still needs qualification. */
int fm1_sdk_services_init(fm1_wl82_services *);

typedef struct {
    uint32_t sys_hz,lsb_hz,scans,audio_irqs;
    int fault;
} fm1_sdk_diagnostics;
extern volatile fm1_sdk_diagnostics fm1_sdk_diag;
#endif
