/* ST-3: ctype classification and conversion. */
#include "abi_test.h"
#include <ctype.h>

void main(void) {
    volatile int upper = 'A';
    volatile int lower = 'q';
    volatile int digit = '7';
    volatile int space = ' ';
    volatile int newline = '\n';
    volatile int punct = '?';

    test_init();

    ASSERT(isalnum(upper) && isalnum(digit) && !isalnum(punct));
    ASSERT(isalpha(lower) && !isalpha(digit));
    ASSERT(isblank(space) && isblank('\t'));
    ASSERT(iscntrl(newline) && !iscntrl(upper));
    ASSERT(isdigit(digit) && !isdigit(lower));
    ASSERT(isgraph(punct) && !isgraph(space));
    ASSERT(islower(lower) && !islower(upper));
    ASSERT(isprint(space) && !isprint(newline));
    ASSERT(ispunct(punct) && !ispunct(lower));
    ASSERT(isspace(newline) && !isspace(lower));
    ASSERT(isupper(upper) && !isupper(lower));
    ASSERT(isxdigit(digit) && isxdigit(upper) && !isxdigit('g'));
    ASSERT(tolower(upper) == 'a' && toupper(lower) == 'Q');

    test_pass();
}
