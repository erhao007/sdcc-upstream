/* ralloc2-mulreg.c - static allocation probe for the MT-1C directed
   gate: d0 takes DR4, w0 keeps the low word tuple busy while h is
   assigned to the high word tuple, and d1 can then reuse the low tuple.
   The native 16x16->32 product has no legal contiguous tuple; with the
   R8-R15 exclusion active it spills, while the disabled mutation reaches
   the high-byte fallback.  The gate checks assembly comments; it is not
   executed.  */

#include <stdint.h>

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
