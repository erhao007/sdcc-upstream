/* ST-3: compact stdlib conversions. */
#include "abi_test.h"
#include <stdlib.h>

void main(void) {
    volatile int negative = -123;

    test_init();

    ASSERT(abs(negative) == 123);
    ASSERT(atoi("-321") == -321);
    test_pass();
}
