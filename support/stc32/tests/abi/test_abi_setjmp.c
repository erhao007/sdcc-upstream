/* test_abi_setjmp.c - Test setjmp / longjmp non-local jumps and SPX stack restoration */
#include "abi_test.h"
#include <setjmp.h>

jmp_buf env;

void deep_call(uint8_t level) __reentrant {
    if (level == 0) {
        longjmp(env, 42);
    }
    deep_call(level - 1);
}

void main(void) {
    test_init();

    volatile int step = 0;
    int ret = setjmp(env);

    if (ret == 0) {
        /* First return from setjmp */
        ASSERT(step == 0);
        step = 1;
        deep_call(5);
        /* Should not reach here */
        ASSERT(false);
    } else {
        /* Return from longjmp */
        ASSERT(step == 1);
        ASSERT(ret == 42);
        step = 2;
    }

    ASSERT(step == 2);
    test_pass();
}
