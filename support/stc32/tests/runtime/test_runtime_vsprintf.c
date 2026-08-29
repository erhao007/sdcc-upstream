/* ST-3: vsprintf va_list path. */
#include "abi_test.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static __xdata char buffer[32];

static int format_v(char *dst, const char *format, ...) {
    va_list ap;
    int result;
    va_start(ap, format);
    result = vsprintf(dst, format, ap);
    va_end(ap);
    return result;
}

void main(void) {
    int count;
    test_init();
    count = format_v(buffer, "%u/%ld", 42u, 123456L);
    ASSERT(count == 9);
    ASSERT(strcmp(buffer, "42/123456") == 0);
    test_pass();
}
