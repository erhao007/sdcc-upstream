/* test_abi_types.c - Test basic scalar types, sizes and Big-Endian object representation */
#include "abi_test.h"

void main(void) {
    test_init();

    /* 1. Verify Sizes per SDCC MCS-251 Default Port Facts */
    ASSERT(sizeof(char) == 1);
    ASSERT(sizeof(short) == 2);
    ASSERT(sizeof(int) == 2);
    ASSERT(sizeof(long) == 4);
    ASSERT(sizeof(float) == 4);
    ASSERT(sizeof(long long) == 8);
    /* In SDCC default mode, double is strictly 4 bytes (single-precision IEEE-754) */
    ASSERT(sizeof(double) == 4);
    ASSERT(sizeof(size_t) == 4);
    ASSERT(sizeof(uintptr_t) == 4);

    /* 2. Verify Big-Endian representation in memory */
    volatile uint16_t val16 = 0x1234;
    volatile uint8_t *p16 = (volatile uint8_t *)&val16;
    ASSERT(p16[0] == 0x12);
    ASSERT(p16[1] == 0x34);

    volatile uint32_t val32 = 0x12345678UL;
    volatile uint8_t *p32 = (volatile uint8_t *)&val32;
    ASSERT(p32[0] == 0x12);
    ASSERT(p32[1] == 0x34);
    ASSERT(p32[2] == 0x56);
    ASSERT(p32[3] == 0x78);

    volatile uint64_t val64 = 0x0123456789abcdefULL;
    volatile uint8_t *p64 = (volatile uint8_t *)&val64;
    ASSERT(p64[0] == 0x01);
    ASSERT(p64[1] == 0x23);
    ASSERT(p64[2] == 0x45);
    ASSERT(p64[3] == 0x67);
    ASSERT(p64[4] == 0x89);
    ASSERT(p64[5] == 0xab);
    ASSERT(p64[6] == 0xcd);
    ASSERT(p64[7] == 0xef);

    test_pass();
}
