/*
   fw-patterns.c - Firmware-pattern regression tests for mcs251.

   Tests patterns commonly used in STC32G firmware that are NOT well
   covered by the existing regression suite:

   1. __pdata array indexed write (was broken by missing dpxl)
   2. __pdata array indexed read
   3. __code const lookup table + __pdata write loop
   4. __xdata array indexed write/read
   5. Deep recursion (reentrant, long long)
   6. Function pointer arrays
   7. struct field access with __code const array

   These are integration-level tests: if any fails, a real firmware
   would also fail.
*/

#include <testfwk.h>
#include <string.h>

/* ---- Test 1-3: __pdata indexed access ---- */
#if defined(__SDCC_mcs251) || defined(__SDCC_mcs51)
__code const unsigned char lookup[] = {0x10, 0x20, 0x30, 0x40, 0x50};
__pdata unsigned char pdata_buf[5];
#endif

void
testPdataIndexedWrite(void)
{
#if defined(__SDCC_mcs251) || defined(__SDCC_mcs51)
  unsigned char i;
  for (i = 0; i < 5; i++)
    pdata_buf[i] = lookup[i] + 1;

  ASSERT(pdata_buf[0] == 0x11);
  ASSERT(pdata_buf[1] == 0x21);
  ASSERT(pdata_buf[2] == 0x31);
  ASSERT(pdata_buf[3] == 0x41);
  ASSERT(pdata_buf[4] == 0x51);
#endif
}

/* ---- Test 4: __xdata indexed access ---- */
__xdata unsigned char xdata_buf[5];

void
testXdataIndexedWrite(void)
{
  unsigned char i;
  for (i = 0; i < 5; i++)
    xdata_buf[i] = lookup[i] * 2;

  ASSERT(xdata_buf[0] == 0x20);
  ASSERT(xdata_buf[4] == 0xA0);
}

/* ---- Test 5: Deep recursion ---- */
unsigned int
sum_recursive(unsigned int n) __reentrant
{
  if (n > 1) return n + sum_recursive(n - 1);
  return 1;
}

void
testDeepRecursion(void)
{
  ASSERT(sum_recursive(10) == 55);
  ASSERT(sum_recursive(20) == 210);
}

/* ---- Test 6: Function pointers ---- */
typedef unsigned char (*binop_t)(unsigned char, unsigned char) __reentrant;

static unsigned char op_add(unsigned char a, unsigned char b) __reentrant { return a + b; }
static unsigned char op_mul(unsigned char a, unsigned char b) __reentrant { return a * b; }

__code const binop_t ops[] = { op_add, op_mul };

void
testFunctionPointers(void)
{
  ASSERT(ops[0](3, 4) == 7);
  ASSERT(ops[1](3, 4) == 12);
}

/* ---- Test 7: Struct + __code const ---- */
struct pin {
  unsigned char port;
  unsigned char mask;
};

__code const struct pin pins[] = {
  {0x80, 0x01},
  {0x90, 0x02},
  {0xA0, 0x04},
};

void
testStructCodeConst(void)
{
  ASSERT(pins[0].port == 0x80);
  ASSERT(pins[0].mask == 0x01);
  ASSERT(pins[1].port == 0x90);
  ASSERT(pins[2].mask == 0x04);
}

/* ---- Test 8: memset runtime library ---- */
__xdata unsigned char membuf[8];

void
testMemset(void)
{
  memset(membuf, 0xAB, 8);
  ASSERT(membuf[0] == 0xAB);
  ASSERT(membuf[7] == 0xAB);
}
