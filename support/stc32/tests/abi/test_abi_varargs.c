/* test_abi_varargs.c - Test variable argument functions and stdarg.h on SPX stack */
#include "abi_test.h"
#include <stdarg.h>

uint32_t sum_u16_varargs(uint8_t count, ...) {
    va_list ap;
    va_start(ap, count);
    uint32_t total = 0;
    for (uint8_t i = 0; i < count; ++i) {
        /* In C standard / SDCC, integral promotions promote char/short in varargs */
        uint16_t val = va_arg(ap, int);
        total += val;
    }
    va_end(ap);
    return total;
}

uint32_t mix_varargs(uint8_t num, ...) {
    va_list ap;
    va_start(ap, num);
    uint32_t result = 0;
    for (uint8_t i = 0; i < num; ++i) {
        uint32_t val = va_arg(ap, uint32_t);
        result += val;
    }
    va_end(ap);
    return result;
}

void main(void) {
    test_init();

    /* 1. Sum variable count of 16-bit integers */
    uint32_t s1 = sum_u16_varargs(3, 10, 20, 30);
    ASSERT(s1 == 60);

    uint32_t s2 = sum_u16_varargs(5, 100, 200, 300, 400, 500);
    ASSERT(s2 == 1500);

    /* 2. Sum variable count of 32-bit integers */
    uint32_t m = mix_varargs(3, 0x10000UL, 0x20000UL, 0x30000UL);
    ASSERT(m == 0x60000UL);

    test_pass();
}
