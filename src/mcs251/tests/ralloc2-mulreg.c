/* ralloc2-mulreg.c - executable allocation probes for the MT-1D gate.
   The default probe makes d0 take DR4 while w0 keeps the low word tuple
   busy and h is
   assigned to the high word tuple, and d1 can then reuse the low tuple.
   The native 16x16->32 product has no legal contiguous tuple; with the
   R8-R15 exclusion active it spills, while the disabled mutation reaches
   the high-byte fallback.  The gate checks allocation comments and executes
   the active images in uCsim, observing the linked XDATA result directly so
   the probe itself adds no status/comparison pressure.

   MCS251_RALLOC2_POINTER_SPILL (historical macro name) selects a separate
   low-pressure image that keeps a pointer value on the
   no-call/no-dereference ralloc2 path and verifies that its 24-bit arithmetic
   survives target-pointer register allocation.  */

#include <stdint.h>

#ifdef MCS251_RALLOC2_POINTER_SPILL

volatile __xdata uint8_t * volatile __xdata g_pointer_sink;
volatile __xdata uintptr_t g_pointer_base = 0x00010203UL;
volatile __xdata uintptr_t g_pointer_delta = 0x00000102UL;

void main (void)
{
  volatile __xdata uint8_t *pointer =
    (volatile __xdata uint8_t *)g_pointer_base;
  pointer += g_pointer_delta;
  g_pointer_sink = pointer;
  while (1);
}

#else

volatile __xdata uint32_t g_a = 0x11223344UL;
volatile __xdata uint32_t g_b = 0x55667788UL;
volatile __xdata uint16_t g_x = 0xC0DE;
volatile __xdata uint16_t g_y = 0xBEEF;
volatile __xdata uint32_t g_sink;

void main (void)
{
  uint32_t d0 = g_a;
  uint16_t w0 = g_x;
  /* w0 consumes the low word tuple while h is assigned.  It then dies;
     h takes a high word tuple, making the high DR tuple unavailable to
     d1 while leaving high bytes for the mutation's fallback. */
  uint16_t h = (uint16_t)(g_x + g_y);
  g_sink ^= w0 ^ h;
  uint32_t d1 = g_b;
  uint32_t p = (uint32_t)g_x * g_y;

  g_sink ^= d0 ^ d1 ^ p ^ h;
  while (1);
}

#endif
