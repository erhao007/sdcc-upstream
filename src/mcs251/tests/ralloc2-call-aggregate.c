/* MT-1E class-3 directed fixture: CALL/PCALL/SEND/RECEIVE and aggregate
   by-value traffic execute through production ralloc2 in model-small and
   model-large.  Build one mode at a time; WIDE_OVERLAP and PTR_CAPACITY
   separately prove fail-closed class-5 boundaries for the plain 14-byte pool
   and the 12-byte pool left after reserving R0/R1. */

#include <stdint.h>

volatile __data __at (0x30) uint8_t  c3_status;
volatile __data __at (0x31) uint8_t  c3_reserved;
volatile __data __at (0x32) uint16_t c3_fail_line;

#define C3_PASS() do { c3_status = 0x55; while (1); } while (0)
#define C3_ASSERT(cond) do { \
    if (!(cond)) { c3_status = 0xEE; c3_fail_line = __LINE__; while (1); } \
  } while (0)

typedef struct
{
  uint8_t a;
  uint8_t b;
} c3_pair;

typedef struct
{
  uint32_t a;
  uint32_t b;
  uint32_t c;
} c3_triple;

#if defined (MCS251_RALLOC2_CLASS3_INDIRECT)

volatile __xdata uint32_t c3_seed = 0x11223344UL;
volatile __xdata uint32_t c3_call_sink;
volatile __xdata uint8_t c3_bytes[4] = { 0x12, 0x34, 0x5A, 0x78 };

static uint32_t
c3_indirect_step (uint32_t value)
{
  return value ^ 0xA5A55A5AUL;
}

static uint32_t (* volatile c3_step) (uint32_t) = c3_indirect_step;

static volatile __xdata uint8_t *
c3_pointer_step (volatile __xdata uint8_t *base, uint8_t offset)
{
  return base + offset;
}

static void *
c3_generic_pointer_step (volatile __xdata uint8_t *base, uint8_t offset)
{
  return (void *)(base + offset);
}

void
main (void)
{
  uint32_t indirect = c3_step (c3_seed);
  volatile __xdata uint8_t *pointer = c3_pointer_step (c3_bytes, 2);
  void *generic_result = c3_generic_pointer_step (c3_bytes, 2);
  volatile __xdata uint8_t *generic_pointer =
    (volatile __xdata uint8_t *)generic_result;

  C3_ASSERT (*pointer == 0x5A);
  C3_ASSERT (*generic_pointer++ == 0x5A);
  C3_ASSERT (*generic_pointer == 0x78);
  c3_call_sink = indirect ^ *pointer;
  C3_ASSERT (c3_call_sink == 0xB4876944UL);
  C3_PASS ();
}

#elif defined (MCS251_RALLOC2_CLASS3_WIDE)

volatile __xdata uint64_t c3_wide_sink;

static uint64_t
c3_wide_value (void)
{
  return UINT64_C (0x111311171113111F);
}

void
main (void)
{
  uint64_t result = c3_wide_value ();
  c3_wide_sink = result;
  C3_PASS ();
}

#elif defined (MCS251_RALLOC2_CLASS3_PTR_CAPACITY)

volatile __idata uint8_t c3_idata;
volatile __xdata uint64_t c3_capacity_sink8;
volatile __xdata uint32_t c3_capacity_sink4;
volatile __xdata uint8_t c3_capacity_sink1;
volatile __xdata uint8_t c3_capacity_idata_sink;

static uint64_t
c3_capacity_ret8 (void)
{
  return UINT64_C (0x0102030405060708);
}

static uint32_t
c3_capacity_ret4 (void)
{
  return 0x11121314UL;
}

static uint8_t
c3_capacity_ret1 (void)
{
  return 0x21U;
}

void
main (void)
{
  uint64_t a;
  uint32_t b;
  uint8_t c;

  c3_idata = 0x31U;
  a = c3_capacity_ret8 ();
  b = c3_capacity_ret4 ();
  c = c3_capacity_ret1 ();
  c3_capacity_sink8 = a;
  c3_capacity_sink4 = b;
  c3_capacity_sink1 = c;
  c3_capacity_idata_sink = c3_idata;
  C3_ASSERT (c3_capacity_sink8 == UINT64_C (0x0102030405060708));
  C3_ASSERT (c3_capacity_sink4 == 0x11121314UL);
  C3_ASSERT (c3_capacity_sink1 == 0x21U);
  C3_ASSERT (c3_capacity_idata_sink == 0x31U);
  C3_PASS ();
}

#elif defined (MCS251_RALLOC2_CLASS3_WIDE_OVERLAP)

volatile __xdata uint8_t c3_wide_seeds[2] = { 0x11, 0x22 };
volatile __xdata uint64_t c3_wide_sink;

static uint64_t
c3_wide_from_seed (uint8_t seed)
{
  return UINT64_C (0x0102030405060700) | seed;
}

void
main (void)
{
  uint64_t first = c3_wide_from_seed (c3_wide_seeds[0]);
  uint64_t second = c3_wide_from_seed (c3_wide_seeds[1]);
  c3_wide_sink = first + second;
  C3_ASSERT (c3_wide_sink == UINT64_C (0x020406080A0C0E33));
  C3_PASS ();
}

#elif defined (MCS251_RALLOC2_CLASS3_AGGREGATE_PARAM)

volatile __xdata c3_pair c3_pair_seed = { 0x12, 0x34 };
volatile __xdata uint32_t c3_aggregate_sink;

static uint8_t
c3_pair_first (c3_pair value)
{
  return value.a;
}

void
main (void)
{
  uint8_t result = c3_pair_first (c3_pair_seed);
  C3_ASSERT (result == 0x12U);
  c3_aggregate_sink = result;
  C3_PASS ();
}

#elif defined (MCS251_RALLOC2_CLASS3_AGGREGATE_RETURN)

volatile __xdata uint32_t c3_aggregate_sink;

static c3_pair
c3_make_pair (uint8_t a, uint8_t b)
{
  c3_pair result;
  result.a = a;
  result.b = b;
  return result;
}

void
main (void)
{
  c3_pair value = c3_make_pair (0x12, 0x34);
  uint8_t result = value.a;
  C3_ASSERT (result == 0x12U);
  c3_aggregate_sink = result;
  C3_PASS ();
}

#elif defined (MCS251_RALLOC2_CLASS3_AGGREGATE_WIDE)

volatile __xdata c3_triple c3_triple_seed = {
  0x01020304UL, 0x11121314UL, 0x21222324UL
};
volatile __xdata uint32_t c3_aggregate_sink;

static c3_triple
c3_step_triple (c3_triple value)
{
  value.a += 1UL;
  value.b += 2UL;
  value.c += 3UL;
  return value;
}

void
main (void)
{
  c3_triple result = c3_step_triple (c3_triple_seed);
  uint32_t value = result.a ^ result.b ^ result.c;
  c3_aggregate_sink = value;
  C3_ASSERT (value ==
             (0x01020305UL ^ 0x11121316UL ^ 0x21222327UL));
  C3_PASS ();
}

#else

volatile __xdata uint32_t c3_dwords[3] = {
  0x11111111UL, 0x22222222UL, 0x33333333UL
};
volatile __xdata uint32_t c3_seed = 0x11223344UL;
volatile __xdata uint32_t c3_call_sink;

static uint32_t
c3_direct_step (uint32_t value)
{
  return value + 0x01020304UL;
}

void
main (void)
{
  uint32_t d0 = c3_dwords[0];
  uint32_t d1 = c3_dwords[1];
  uint32_t d2 = c3_dwords[2];
  uint32_t direct = c3_direct_step (c3_seed);

  c3_call_sink = direct ^ d0 ^ d1 ^ d2;
  C3_ASSERT (c3_call_sink == 0x12243648UL);
  C3_PASS ();
}

#endif
