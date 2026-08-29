/* ST-3: public string.h and memory APIs. */
#include "abi_test.h"
#include <string.h>

void main(void) {
    char a[12];
    char b[12];
    char c[12];
    char overlap[12] = "abcdefghi";

    test_init();

    ASSERT(memset(a, 0x5a, sizeof(a)) == a);
    ASSERT(a[0] == 0x5a && a[11] == 0x5a);

    for (uint8_t i = 0; i < sizeof(b); ++i)
        b[i] = (char)(i + 1);
    ASSERT(memcpy(a, b, sizeof(b)) == a);
    ASSERT(memcmp(a, b, sizeof(b)) == 0);
    a[7] = 0;
    ASSERT(memcmp(a, b, sizeof(b)) < 0);
    ASSERT(memmove(overlap + 2, overlap, 7) == overlap + 2);
    ASSERT(memcmp(overlap, "ababcdefg", 9) == 0);
    ASSERT(memmove(overlap, overlap + 2, 7) == overlap);
    ASSERT(memcmp(overlap, "abcdefg", 7) == 0);

    ASSERT(strcpy(c, "openstc32") == c);
    ASSERT(strlen(c) == 9);
    ASSERT(strcmp(c, "openstc32") == 0);
    ASSERT(strcmp(c, "openstc31") > 0);
    ASSERT(strncmp(c, "open", 4) == 0);
    ASSERT(strchr(c, 's') == c + 4);
    ASSERT(strrchr(c, '3') == c + 7);

    memset(c, 0x7f, sizeof(c));
    ASSERT(strncpy(c, "xy", 5) == c);
    ASSERT(c[0] == 'x' && c[1] == 'y');
    ASSERT(c[2] == 0 && c[3] == 0 && c[4] == 0);

    test_pass();
}
