#ifndef FM1_FORGE_TARGET_H
#define FM1_FORGE_TARGET_H
#include "forge.h"
typedef void (*fm1_forge_reply)(void *,const char *);
int fm1_forge_clock_prepare(void);
int fm1_forge_usb_command(const char *,uint32_t,fm1_forge_reply,void *);
void fm1_forge_hal_snapshot(fm1_forge_input *);
/* Optional video producer stays internal to the foreground Forge task; user
 * modules render bounded RGB565 strips through the existing module ABI. */
#endif
