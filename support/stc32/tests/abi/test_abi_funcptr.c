/* test_abi_funcptr.c - Test function pointers, indirect ECALL @DR28 and dispatch tables */
#include "abi_test.h"

typedef uint16_t (*binary_op_t)(uint16_t, uint16_t) __reentrant;

uint16_t op_add(uint16_t a, uint16_t b) __reentrant {
    return a + b;
}

uint16_t op_sub(uint16_t a, uint16_t b) __reentrant {
    return a - b;
}

uint16_t op_mul(uint16_t a, uint16_t b) __reentrant {
    return a * b;
}

static const binary_op_t op_table[3] = {
    op_add,
    op_sub,
    op_mul
};

uint16_t execute_op(binary_op_t fn, uint16_t x, uint16_t y) __reentrant {
    return fn(x, y);
}

void main(void) {
    test_init();

    /* 1. Direct function pointer call */
    binary_op_t fn = op_add;
    ASSERT(fn(20, 30) == 50);

    fn = op_sub;
    ASSERT(fn(100, 40) == 60);

    /* 2. Indirect call through higher-order dispatcher */
    ASSERT(execute_op(op_mul, 12, 10) == 120);

    /* 3. Dispatch table call */
    ASSERT(op_table[0](15, 25) == 40);
    ASSERT(op_table[1](100, 35) == 65);
    ASSERT(op_table[2](7, 8) == 56);

    test_pass();
}
