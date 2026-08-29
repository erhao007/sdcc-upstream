/* ST-3: sprintf varargs path. */
#include "abi_test.h"
#include <stdio.h>
#include <string.h>

static __xdata char buffer[32];

void main(void) {
    int count;

    test_init();

    count = sprintf(buffer, "%s:%d:%x", "ok", -12, 0x2a);
    ASSERT(count == 9);
    ASSERT(strcmp(buffer, "ok:-12:2a") == 0);

    test_pass();
}
