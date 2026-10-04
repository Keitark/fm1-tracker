#include "fm1_nes.h"
#ifdef FM1_TARGET_PI32V2
#include "fm1_wl82.h"
#endif
/* No implicit dev-board startup and no flash operations. The target adapter
   requires an installed FM-1 service/profile implementation before any I/O. */
int fm1_nes_target_start(const uint8_t *rom,size_t size) {
#ifdef FM1_TARGET_PI32V2
    return fm1_wl82_run(rom,size);
#else
    (void)rom; (void)size;
    return FM1_NES_BSP_UNVERIFIED;
#endif
}
