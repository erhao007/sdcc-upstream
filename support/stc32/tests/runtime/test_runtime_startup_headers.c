/* ST-3: startup sections and core headers. */
#include "abi_test.h"
#include <stdarg.h>
#include <limits.h>

volatile __data uint8_t data_init = 0x5a;
volatile __data uint8_t data_zero;
volatile __xdata uint8_t xdata_init[3] = {0x12, 0x34, 0x56};
volatile __xdata uint8_t xdata_zero[3];

unsigned char __sdcc_external_startup(void) {
    data_init = 0xee;
    data_zero = 0xdd;
    xdata_init[0] = 0xee;
    xdata_init[1] = 0xee;
    xdata_init[2] = 0xee;
    xdata_zero[0] = 0xdd;
    xdata_zero[1] = 0xdd;
    xdata_zero[2] = 0xdd;
    return 0;
}

static uint32_t sum_promoted(uint8_t count, ...) {
    va_list ap;
    uint32_t sum = 0;
    va_start(ap, count);
    while (count--)
        sum += va_arg(ap, int);
    va_end(ap);
    return sum;
}

void main(void) {
    test_init();

    ASSERT(CHAR_BIT == 8);
    ASSERT(UINT16_MAX == 0xffffu);
    ASSERT(sizeof(size_t) == 4);
    ASSERT(sizeof(bool) == 1);
    ASSERT(sum_promoted(3, 10, 20, 30) == 60);

    ASSERT(data_init == 0x5a);
    ASSERT(data_zero == 0);
    ASSERT(xdata_init[0] == 0x12 && xdata_init[1] == 0x34 && xdata_init[2] == 0x56);
    ASSERT(xdata_zero[0] == 0 && xdata_zero[1] == 0 && xdata_zero[2] == 0);

    test_pass();
}
