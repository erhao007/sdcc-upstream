/* ralloc2-class5-pressure.c - MT-1E Phase 1 Class 5 pressure fixture.

   Nine simultaneously live unsigned long long values require 72 bytes of
   temporary storage, exceeding the former 64-byte fail-closed threshold.
   The result is deliberately consumed as one expression so the allocator
   must preserve every byte while ordinary values spill to materialised
   slots.  No call or pointer shape is mixed into this probe; call-result
   capacity is covered independently by ralloc2-call-aggregate.c. */

#include <stdint.h>

volatile __data __at (0x30) uint8_t  c5_status;
volatile __data __at (0x31) uint8_t  c5_reserved;
volatile __data __at (0x32) uint16_t c5_fail_line;
volatile __xdata uint64_t c5_sink;
volatile __xdata uint64_t c5_seed[9] = {
  UINT64_C (0x0102030405060708),
  UINT64_C (0x1112131415161718),
  UINT64_C (0x2122232425262728),
  UINT64_C (0x3132333435363738),
  UINT64_C (0x4142434445464748),
  UINT64_C (0x5152535455565758),
  UINT64_C (0x6162636465666768),
  UINT64_C (0x7172737475767778),
  UINT64_C (0x8182838485868788)
};

#define C5_PASS() do { c5_status = 0x55; while (1); } while (0)
#define C5_ASSERT(cond) do { \
    if (!(cond)) { c5_status = 0xEE; c5_fail_line = __LINE__; while (1); } \
  } while (0)

void
main (void)
{
  uint64_t a0 = c5_seed[0];
  uint64_t a1 = c5_seed[1];
  uint64_t a2 = c5_seed[2];
  uint64_t a3 = c5_seed[3];
  uint64_t a4 = c5_seed[4];
  uint64_t a5 = c5_seed[5];
  uint64_t a6 = c5_seed[6];
  uint64_t a7 = c5_seed[7];
  uint64_t a8 = c5_seed[8];

  c5_sink = a0 ^ a1 ^ a2 ^ a3 ^ a4 ^ a5 ^ a6 ^ a7 ^ a8;
  C5_ASSERT (c5_sink == UINT64_C (0x8182838485868788));
  C5_PASS ();
}
