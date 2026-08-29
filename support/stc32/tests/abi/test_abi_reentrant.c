/* test_abi_reentrant.c - Test reentrant functions, recursion and SPX stack addressing */
#include "abi_test.h"

uint32_t fibonacci(uint8_t n) __reentrant {
    if (n == 0) return 0;
    if (n == 1) return 1;
    return fibonacci(n - 1) + fibonacci(n - 2);
}

uint32_t factorial(uint8_t n) __reentrant {
    if (n <= 1) return 1;
    return (uint32_t)n * factorial(n - 1);
}

uint32_t local_stack_frame(uint16_t x) __reentrant {
    volatile uint32_t arr[4];
    arr[0] = x + 1;
    arr[1] = x + 2;
    arr[2] = x + 3;
    arr[3] = x + 4;
    return arr[0] + arr[1] + arr[2] + arr[3];
}

void main(void) {
    test_init();

    /* 1. Recursion with Fibonacci */
    ASSERT(fibonacci(7) == 13);
    ASSERT(fibonacci(10) == 55);

    /* 2. Recursion with Factorial */
    ASSERT(factorial(5) == 120);
    ASSERT(factorial(7) == 5040);

    /* 3. Local stack array variables */
    ASSERT(local_stack_frame(10) == (11 + 12 + 13 + 14));

    test_pass();
}
