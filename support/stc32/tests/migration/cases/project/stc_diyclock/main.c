/* Independently authored behavior driver for the derived stc_diyclock fixture. */

#include "abi_test.h"
#include "clock_app.h"

void main(void)
{
    test_init();
    diyclock_reset();

    if (diyclock_snapshot() != 0x00)
        test_fail(__LINE__);

    diyclock_step();
    if (diyclock_snapshot() != 0x31)
        test_fail(__LINE__);

    diyclock_step();
    if (diyclock_snapshot() != 0x22)
        test_fail(__LINE__);

    test_pass();
}
