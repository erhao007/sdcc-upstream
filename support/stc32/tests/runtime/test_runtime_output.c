/* ST-3: printf/puts path with the application-supplied putchar hook. */
#include "abi_test.h"
#include <stdio.h>
#include <string.h>

static __xdata char sink[16];
static uint8_t sink_len;

int putchar(int c) {
    if (sink_len < sizeof(sink))
        sink[sink_len++] = (char)c;
    return c;
}

void main(void) {
    int count;

    test_init();

    sink_len = 0;
    count = printf("P:%u", 7u);
    ASSERT(count == 3);
    ASSERT(sink_len == 3 && memcmp(sink, "P:7", 3) == 0);

    sink_len = 0;
    ASSERT(puts("OK") != EOF);
    ASSERT(sink_len == 3 && memcmp(sink, "OK\n", 3) == 0);

    test_pass();
}
