#ifndef FM1_BOOT_TRACE_H
#define FM1_BOOT_TRACE_H
#include <stdint.h>
enum {
    FM1_TRACE_CACHE_READY=1, FM1_TRACE_MEMORY_READY, FM1_TRACE_EARLY_BOARD,
    FM1_TRACE_BOARD, FM1_TRACE_APP, FM1_TRACE_WORKER
};
/* Live-debugger evidence only. Reset/UBOOT/helper entry may destroy this RAM.
 * No GPIO, UART, flash, heap, timer or boot-argument accesses. */
typedef struct {uint32_t magic,version,visited,last;} fm1_boot_trace_record;
extern volatile fm1_boot_trace_record fm1_boot_trace;
void fm1_boot_trace_mark(uint32_t stage);
void __wrap_memory_init(void);
#endif
