/* ST-3: all 24 address bits participate in generic-pointer truth. */
#include "abi_test.h"

bool pointer_truth(const void *pointer) {
    if (pointer)
        return true;
    return false;
}

void main(void) {
    test_init();
    ASSERT(!pointer_truth(NULL));
    ASSERT(pointer_truth((const void *)UINT32_C(0x010000)));
    test_pass();
}
