/* ST-3: 32-bit integer arithmetic paths. */
#include "abi_test.h"

volatile uint32_t u32a = 100000UL, u32b = 37UL;
volatile int32_t s32a = -100000L, s32b = 37L;

void main(void) {
    test_init();
    ASSERT(u32a * u32b == 3700000UL);
    ASSERT(u32a / u32b == 2702UL);
    ASSERT(u32a % u32b == 26UL);
    ASSERT((u32a << 5) == 3200000UL);
    ASSERT((u32a >> 5) == 3125UL);
    ASSERT(s32a * s32b == -3700000L);
    ASSERT(s32a / s32b == -2702L);
    ASSERT(s32a % s32b == -26L);
    test_pass();
}
