#include "boot_trace.h"
extern void __real_memory_init(void);
volatile fm1_boot_trace_record fm1_boot_trace;

void fm1_boot_trace_mark(uint32_t stage) {
    if(stage<FM1_TRACE_CACHE_READY || stage>FM1_TRACE_WORKER)return;
    fm1_boot_trace.visited|=1u<<stage;
    fm1_boot_trace.last=stage;
}

/* SDK startup calls memory_init after its BSS/data/cache setup. Keep the real
 * initializer and its order. Only add ordinary initialized-RAM breadcrumbs. */
#if defined(__clang__) || defined(__GNUC__)
__attribute__((noinline,used))
#endif
void __wrap_memory_init(void) {
    fm1_boot_trace.magic=0x464d4254u; /* FMBT */
    fm1_boot_trace.version=1;
    fm1_boot_trace.visited=0;
    fm1_boot_trace_mark(FM1_TRACE_CACHE_READY);
    __real_memory_init();
    fm1_boot_trace_mark(FM1_TRACE_MEMORY_READY);
}
