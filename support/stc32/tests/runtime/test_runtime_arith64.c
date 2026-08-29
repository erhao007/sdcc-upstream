/* ST-3: 64-bit compiler-runtime arithmetic paths. */
#include "abi_test.h"

volatile uint64_t u64a = UINT64_C(0x1122334455667788);
volatile uint64_t u64b = UINT64_C(0x12345);
volatile uint64_t u64m = UINT64_C(0x12345678);
volatile uint64_t u64n = UINT64_C(0x10203);
volatile int64_t s64a = -INT64_C(1234567890123);
volatile int64_t s64b = INT64_C(12345);

void main(void) {
    test_init();
    ASSERT(u64m * u64n == UINT64_C(0x00001258f5c1f368));
    ASSERT(u64a / u64b == UINT64_C(0x00000f0f14696b50));
    ASSERT(u64a % u64b == UINT64_C(0x9af8));
    ASSERT((u64a << 13) == UINT64_C(0x46688aaccef10000));
    ASSERT((u64a >> 17) == UINT64_C(0x0000089119a22ab3));
    ASSERT(s64a * s64b == -INT64_C(15240740603568435));
    ASSERT(s64a / s64b == -INT64_C(100005499));
    ASSERT(s64a % s64b == -INT64_C(4968));
    test_pass();
}
