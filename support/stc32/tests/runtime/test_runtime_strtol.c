/* ST-3: strtol value and end-pointer path. */
#include "abi_test.h"
#include <stdlib.h>

static char * __xdata end;

void main(void) {
    test_init();
    ASSERT(strtol("  -0x2aZ", &end, 0) == -42);
    ASSERT(*end == 'Z');
    test_pass();
}
