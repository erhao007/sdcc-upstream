/* ST-3: qsort/bsearch callback and generic-pointer paths. */
#include "abi_test.h"
#include <stdlib.h>

static int compare_u8(const void *left, const void *right) __reentrant {
    return (int)*(const uint8_t *)left - (int)*(const uint8_t *)right;
}

void main(void) {
    uint8_t values[6] = {9, 1, 7, 3, 5, 2};
    uint8_t key = 5;
    uint8_t missing = 4;

    test_init();

    qsort(values, 6, sizeof(values[0]), compare_u8);
    ASSERT(values[0] == 1 && values[1] == 2 && values[2] == 3);
    ASSERT(values[3] == 5 && values[4] == 7 && values[5] == 9);
    ASSERT(*(uint8_t *)bsearch(&key, values, 6, sizeof(values[0]), compare_u8) == 5);
    ASSERT(bsearch(&missing, values, 6, sizeof(values[0]), compare_u8) == NULL);

    test_pass();
}
