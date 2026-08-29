/* ST-5: explicit printf_tiny/printf_small profile fixture. */
#include "abi_test.h"
#include "openstc32/stdio.h"
#include <string.h>

static __xdata __at (0x8000) char sink[40];
static uint8_t sink_len;

int putchar(int c)
{
    if (sink_len < sizeof(sink))
        sink[sink_len++] = (char)c;
    return c;
}

void main(void)
{
    test_init();
    sink_len = 0;
#if STC32_STDIO_PROFILE == STC32_STDIO_PROFILE_TINY
    stc32_printf("T:%c/%s/%d/%u/%x", 'A', "ok", -12, 34u, 0x2au);
    ASSERT(sink_len == 16);
    ASSERT(memcmp(sink, "T:A/ok/-12/34/2a", 16) == 0);
#else
    stc32_printf("S:%c/%s/%d/%ld/%lx", 'A', "ok", -12, 123456L, 0x2aUL);
    ASSERT(sink_len == 20);
    /* printf_small reuses SDCC __ltoa, whose hexadecimal digits are upper-case. */
    ASSERT(memcmp(sink, "S:A/ok/-12/123456/2A", 20) == 0);
#endif
    test_pass();
}
