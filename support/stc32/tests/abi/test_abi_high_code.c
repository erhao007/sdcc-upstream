/* test_abi_high_code.c - Test 24-bit function pointer indirect call into Region 1 (0x01xxxx) and ERET */
#include "abi_test.h"

typedef uint16_t (*high_func_t)(uint16_t);

extern uint16_t high_code_target(uint16_t a);

void main(void) {
    test_init();

    /* 1. Verify 24-bit address of high_code_target is in Region 1 (0x01xxxx) */
    uint32_t addr = (uint32_t)high_code_target;
    ASSERT((addr >> 16) == 0x01);

    /* 2. Direct call across regions */
    uint16_t r1 = high_code_target(0x0200);
    ASSERT(r1 == 0x0900);

    /* 3. Indirect call via 3-byte function pointer (ECALL @DR28) into Region 1 */
    volatile high_func_t fn = high_code_target;
    uint16_t r2 = fn(0x0300);
    ASSERT(r2 == 0x0A00);

    test_pass();
}
