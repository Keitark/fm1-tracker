#ifndef FM1_ROM50_H
#define FM1_ROM50_H
#include "fm1_nes.h"
/* Optional, locally generated ROM integration. Only present in --rom50 builds.
   Read-only lifetime is the whole program; final XIP placement needs a linker.
   This is NOT a reset vector, board initializer, updater or boot image. */
const uint8_t *fm1_rom50_data(size_t *size);
/* Calls the existing board-gated target entry. Never installs board services. */
int fm1_rom50_start(void);
#endif
