/* test_abi_calling.c - Test scalar parameter passing, return values in registers and __callee_saves */
#include "abi_test.h"

/* 1-byte scalar */
uint8_t func_ret_u8(uint8_t a, uint8_t b) __reentrant {
    return a + b;
}

/* 2-byte scalar */
uint16_t func_ret_u16(uint16_t a, uint16_t b) __reentrant {
    return a + b;
}

/* 3-byte pointer */
uint8_t * func_ret_ptr(uint8_t *p, uint16_t offset) __reentrant {
    return p + offset;
}

/* 4-byte scalar */
uint32_t func_ret_u32(uint32_t a, uint32_t b) __reentrant {
    return a + b;
}

/* 8-byte scalar (DPL, DPH, B, A, R4, R5, R6, R7) */
uint64_t func_ret_u64(uint64_t a, uint64_t b) __reentrant {
    return a + b;
}

/* __bit return in CY */
__bit func_ret_bit(__bit a, __bit b) __reentrant {
    return a ^ b;
}

/* #pragma callee_saves function: Callee saves and restores registers it modifies */
#pragma callee_saves func_callee_saves_calc
uint16_t func_callee_saves_calc(uint16_t x) {
    volatile uint16_t temp1 = x * 3;
    volatile uint16_t temp2 = temp1 + 7;
    return temp2;
}

/* Multi-argument mixing */
uint32_t func_multi_args(uint8_t a, uint16_t b, uint32_t c, uint8_t *p) __reentrant {
    return (uint32_t)a + (uint32_t)b + c + (uint32_t)(*p);
}

__xdata uint8_t g_buf[8] = {1, 2, 3, 4, 5, 6, 7, 8};

void main(void) {
    test_init();

    /* 1. 1-byte return */
    ASSERT(func_ret_u8(0x12, 0x34) == 0x46);

    /* 2. 2-byte return */
    ASSERT(func_ret_u16(0x1234, 0x5678) == 0x68AC);

    /* 3. 3-byte pointer return */
    uint8_t *p = func_ret_ptr(g_buf, 3);
    ASSERT(*p == 4);

    /* 4. 4-byte return */
    ASSERT(func_ret_u32(0x12345678UL, 0x11111111UL) == 0x23456789UL);

    /* 5. 8-byte return */
    ASSERT(func_ret_u64(0x0123456789ABCDEFULL, 0x1000000000000001ULL) == 0x1123456789ABCDF0ULL);

    /* 6. True __bit return in CY */
    ASSERT(func_ret_bit(1, 0) == 1);
    ASSERT(func_ret_bit(1, 1) == 0);

    /* 7. __callee_saves execution with live-across-call variables */
    volatile uint16_t live_var1 = 0x1122;
    volatile uint16_t live_var2 = 0x3344;
    uint16_t cs_res = func_callee_saves_calc(10);
    ASSERT(cs_res == 37);
    ASSERT(live_var1 == 0x1122);
    ASSERT(live_var2 == 0x3344);

    /* 8. Multi-argument */
    uint8_t val = 10;
    ASSERT(func_multi_args(5, 100, 1000, &val) == 1115);

    test_pass();
}
