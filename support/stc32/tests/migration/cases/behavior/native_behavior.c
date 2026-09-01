/* Original uCsim behavior fixture for the canonical address-space forms. */

#include "abi_test.h"

volatile __data unsigned char behavior_data;
volatile __idata unsigned char behavior_idata;
volatile __xdata unsigned char behavior_xdata;
const __code unsigned char behavior_code[] = {0xa5, 0x5a};
__bit behavior_bit;

void main(void)
{
    test_init();

    behavior_data = 0x11;
    behavior_idata = 0x22;
    behavior_xdata = 0x33;
    behavior_bit = 1;

    if (behavior_data != 0x11 || behavior_idata != 0x22 ||
        behavior_xdata != 0x33 || behavior_code[0] != 0xa5 ||
        behavior_code[1] != 0x5a || !behavior_bit)
        test_fail(__LINE__);

    behavior_bit = 0;
    if (behavior_bit)
        test_fail(__LINE__);

    test_pass();
}
