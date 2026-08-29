/* ST-5: target printf varargs matrix through an application sink. */
#include "abi_test.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static __xdata char sink[96];
static uint8_t sink_len;

int putchar(int c)
{
    if (sink_len < sizeof(sink))
        sink[sink_len++] = (char)c;
    return c;
}

static int format_v(const char *format, ...)
{
    va_list ap;
    int count;
    va_start(ap, format);
    count = vprintf(format, ap);
    va_end(ap);
    return count;
}

void main(void)
{
    int count;

    test_init();
    sink_len = 0;
    count = printf("%c|%s|%d|%u|%x|%ld|%lu|%lx", 'A', "ok", -12,
                   34u, 0x2au, -123456L, 123456UL, 0x89abcdefUL);
    ASSERT(count == 38);
    ASSERT(sink_len == 38);
    ASSERT(memcmp(sink, "A|ok|-12|34|2a|-123456|123456|89abcdef",
                  38) == 0);

    sink_len = 0;
    count = format_v("V:%u/%ld/%lx", 7u, 123456L, 0x1234abcdUL);
    ASSERT(count == 19);
    ASSERT(sink_len == 19);
    ASSERT(memcmp(sink, "V:7/123456/1234abcd", 19) == 0);

    test_pass();
}
