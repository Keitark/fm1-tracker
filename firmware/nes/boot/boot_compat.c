/* Stock FM-1 UBOOT -> pinned AC79 SDK input-ABI bridge.
 * Runs before application RAM initialization: stack and XIP only, no globals,
 * library calls, peripherals, logging or key queries. Keep the existing SDK
 * initializer and its destinations; normalize only its input extensions.
 */
#include <stdint.h>
#ifndef FM1_BOOT_COMPAT_TEST
#include "app_config.h"
#if __SDRAM_SIZE__ != 0
#error "FM-1 boot compatibility bridge is for internal-RAM builds only"
#endif
#ifdef CONFIG_SDFILE_EXT_ENABLE
#error "FM-1 boot compatibility bridge does not supply external-app metadata"
#endif
#endif

extern void __real_boot_info_init(const volatile uint32_t *argument);

#if defined(__clang__) || defined(__GNUC__)
__attribute__((noinline, used))
#endif
void __wrap_boot_info_init(const volatile uint32_t *stock_argument)
{
    /* Volatile word accesses prohibit a pre-RAM memcpy/memset substitution.
     * Stock handoff is word aligned and explicitly copies six words. The
     * first word points to its separately initialized 32-byte flash header;
     * preserve that pointer. Only the SDK call below consumes the copy.
     */
    volatile uint32_t sdk_argument[23];
    unsigned i;
    for(i=0;i<6;++i) sdk_argument[i]=stock_argument[i];
    for(i=6;i<23;++i) sdk_argument[i]=0;
    __real_boot_info_init(sdk_argument);
}
