#pragma once
/* FM-1 standalone port: bounded-memory, no filesystem or save writes. */
#define NES_ENABLE_SOUND 1
#define NES_USE_SRAM 0
#define NES_FRAME_SKIP 0
#define NES_COLOR_DEPTH 16
#define NES_COLOR_SWAP 0
#define NES_RAM_LACK 1
#define NES_USE_FS 0
#define NES_ROM_STREAM 0
#define NES_ENABLE_HEAVY_MAPPERS 0
#define NES_ENABLE_PLANE1_MAPPERS 0
#define NES_ENABLE_PLANE2_MAPPERS 0
#define NES_LOG_LEVEL NES_LOG_LEVEL_NONE
#define nes_log_printf(...) ((void)0)
