/* Behavior driver for the project-owned multi-file clean-room sample. */

#include "abi_test.h"
#include "cleanroom_app.h"

static const unsigned char cleanroom_input[] = {2, 4, 6, 8, 10};

void main(void)
{
    unsigned int total;
    unsigned char snapshot;

    test_init();
    cleanroom_reset();
    total = cleanroom_accumulate(cleanroom_input, sizeof(cleanroom_input));

    if (total != 110)
        test_fail(__LINE__);
    snapshot = cleanroom_snapshot(2);
    if (snapshot != 0x33)
        test_fail(__LINE__);
    snapshot = cleanroom_snapshot(4);
    if (snapshot != 0x35)
        test_fail(__LINE__);
    if (cleanroom_snapshot(5) != 0xff)
        test_fail(__LINE__);

    cleanroom_reset();
    if (cleanroom_snapshot(0) != 0xff)
        test_fail(__LINE__);

    test_pass();
}
