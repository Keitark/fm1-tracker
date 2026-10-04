#ifndef FM1_BOARD_POWER_TEST
#include "app_config.h"
#include "system/includes.h"
#include "device/includes.h"
#endif
#include "boot_trace.h"
#include "board_power.h"
#ifdef FM1_BOOT_EARLY_DISPLAY
#include "display_test.h"
#endif

/* No demo UART or inherited board GPIO. Power initialization uses the
   stock-derived profile; LCD/audio/keys remain explicit adapter services. */
#ifndef FM1_BOARD_POWER_TEST
REGISTER_DEVICES(device_table) = {};
#endif
void board_early_init(void) {fm1_boot_trace_mark(FM1_TRACE_EARLY_BOARD);}
void board_init(void) {
    fm1_board_power_init();
    fm1_boot_trace_mark(FM1_TRACE_BOARD);
#ifdef FM1_BOOT_EARLY_DISPLAY
    /* T2: record failure and continue SDK initialization, never skip checks. */
    fm1_display_test_init();
#endif
}
