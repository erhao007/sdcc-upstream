/* abi_test.h - Minimal test framework for MCS-251 ABI automated testing */
#ifndef ABI_TEST_H
#define ABI_TEST_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* IRAM test control block (0x30..0x37), above register banks (0x00..0x1f)
   and bit-addressable RAM (0x20..0x2f).  run_abi_tests.py links every
   executable ABI image with --data-loc 0x38 and verifies the exact symbols
   and DSEG origin in the map before simulation. */
#ifdef ABI_TEST_XDATA_CONTROL
/* Runtime/libc images can consume nearly all model-small IRAM.  Keep their
   observation block in an otherwise unused XRAM window; ABI tests retain the
   stricter IRAM control block below. */
volatile __xdata __at (0xff00) uint8_t  abi_test_status;
volatile __xdata __at (0xff01) uint8_t  abi_test_reserved_31;
volatile __xdata __at (0xff02) uint16_t abi_test_fail_line;
volatile __xdata __at (0xff04) uint32_t abi_test_extra;
#else
volatile __data __at (0x30) uint8_t  abi_test_status;
volatile __data __at (0x31) uint8_t  abi_test_reserved_31;
volatile __data __at (0x32) uint16_t abi_test_fail_line;
volatile __data __at (0x34) uint32_t abi_test_extra;
#endif

#define TEST_STATUS_REG  (abi_test_status)
#define TEST_FAIL_LINE   (abi_test_fail_line)
#define TEST_EXTRA_DATA  (abi_test_extra)

#define TEST_PASS_MARKER 0x55
#define TEST_FAIL_MARKER 0xEE

static inline void test_init(void) {
    TEST_STATUS_REG = 0x00;
    TEST_FAIL_LINE = 0x0000;
    TEST_EXTRA_DATA = 0x00000000;
}

static inline void test_pass(void) {
    TEST_STATUS_REG = TEST_PASS_MARKER;
    while (1);
}

static inline void test_fail(uint16_t line) {
    TEST_STATUS_REG = TEST_FAIL_MARKER;
    TEST_FAIL_LINE = line;
    while (1);
}

#define ASSERT(cond) do { \
    if (!(cond)) { \
        test_fail(__LINE__); \
    } \
} while (0)

#endif /* ABI_TEST_H */
