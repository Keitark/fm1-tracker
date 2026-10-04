/* Host scalar shim only. The power struct and selector enums are the actual
 * pinned SDK headers, not a duplicated test definition. No MMIO is executed. */
#ifndef FM1_POWER_TEST_TYPEDEF
#define FM1_POWER_TEST_TYPEDEF
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t s32;
#define BIT(n) (1u<<(n))
#define AT(x)
#define SEC_USED(x)
#endif
