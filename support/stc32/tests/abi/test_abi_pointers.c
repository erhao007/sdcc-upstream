/* test_abi_pointers.c - Test pointer sizes, representations, 24-bit arithmetic and uintptr_t roundtrip */
#include "abi_test.h"

__xdata uint8_t g_xdata_buf[16] = {0x10, 0x20, 0x30, 0x40};
const __code uint8_t g_code_buf[4] = {0xAA, 0xBB, 0xCC, 0xDD};

void dummy_func(void) {}

void main(void) {
    test_init();

    /* 1. Verify Pointer Sizes per ABI §3 */
    ASSERT(sizeof(uint8_t __data *) == 1);
    ASSERT(sizeof(uint8_t __idata *) == 1);
    ASSERT(sizeof(uint8_t __pdata *) == 1);
    ASSERT(sizeof(uint8_t __xdata *) == 3);
    ASSERT(sizeof(const uint8_t __code *) == 3);
    ASSERT(sizeof(uint8_t *) == 3);
    ASSERT(sizeof(void (*)(void)) == 3);

    /* 2. Verify 3-byte Flat Pointer Representation & Dereference */
    uint8_t *generic_ptr = g_xdata_buf;
    ASSERT(*generic_ptr == 0x10);
    ASSERT(*(generic_ptr + 1) == 0x20);

    const uint8_t *code_ptr = g_code_buf;
    ASSERT(*code_ptr == 0xAA);
    ASSERT(*(code_ptr + 2) == 0xCC);

    /* 3. Verify 3-byte Pointer Big-Endian Layout in Memory */
    volatile uint8_t *ptr_var = g_xdata_buf;
    volatile uint8_t *raw_ptr_bytes = (volatile uint8_t *)&ptr_var;
    /* On MCS-251, g_xdata_buf address in 24-bit space: byte 0 is high (23:16), byte 2 is low (7:0) */
    uint32_t addr_val = (uint32_t)g_xdata_buf;
    ASSERT(raw_ptr_bytes[0] == (uint8_t)(addr_val >> 16));
    ASSERT(raw_ptr_bytes[1] == (uint8_t)(addr_val >> 8));
    ASSERT(raw_ptr_bytes[2] == (uint8_t)(addr_val & 0xFF));

    /* 4. Verify uintptr_t Zero-Extension & Roundtrip */
    uintptr_t u = (uintptr_t)generic_ptr;
    ASSERT((u & 0xFF000000UL) == 0); /* high 8 bits must be zero */
    uint8_t *recovered_ptr = (uint8_t *)u;
    ASSERT(recovered_ptr == generic_ptr);
    ASSERT(*recovered_ptr == 0x10);

    test_pass();
}
