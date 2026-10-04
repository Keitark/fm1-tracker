#ifndef APP_CONFIG_H
#define APP_CONFIG_H
/* FM-1 offline boot-link candidate: internal 1 MiB flash, no SDRAM.
   No inherited demo-board GPIO/amplifier configuration. Board power uses
   parameters recovered from working stock 010, not SDK demo defaults. */
#define __FLASH_SIZE__ (1024 * 1024)
#define __SDRAM_SIZE__ 0
#define LIB_DEBUG 0
#define CONFIG_DEBUG_LIB(x) 0
#endif
