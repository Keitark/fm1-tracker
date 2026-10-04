/* Diagnostic only: main has initialized RAM/cache/heap, clocks, interrupt
 * routing, debug and P33 latches, but has not initialized or started the OS.
 * The reviewed config=0 power path has no reachable scheduler/timer API.
 * Keep the original OS initializer and all subsequent init/checks reachable.
 */
#include "board_power.h"
#include "display_test.h"
extern void __real_os_init(void);

#if defined(__clang__) || defined(__GNUC__)
__attribute__((noinline,used))
#endif
void __wrap_os_init(void) {
    fm1_board_power_init();
    (void)fm1_display_test_init();
    __real_os_init();
}
