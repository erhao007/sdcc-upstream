/* ralloc2-extras.c - MT-1C directed-path fixture for shapes beyond the
   MT-1A baseline: setjmp/longjmp, pointer post-increment, switch,
   aggregate by-value/return, bit-scalar arithmetic and shifts.

   Same self-checking protocol as ralloc-baseline.c, but the control block is
   isolated in XDATA at 0x010100 (0x55 PASS / 0xEE + line FAIL).  This keeps
   the complete post-bit-bank DATA window available to the nine combined
   stress shapes when the fixture is built with --data-loc 0x30.
   All operands come from volatile XDATA seeds; expected constants were
   derived with an independent host calculation.  */

#include <stdint.h>
#include <setjmp.h>

volatile __xdata __at (0x010100) uint8_t  rx_status;
volatile __xdata __at (0x010101) uint8_t  rx_reserved;
volatile __xdata __at (0x010102) uint16_t rx_fail_line;
volatile __xdata __at (0x010104) uint32_t rx_extra;

#define RX_PASS() do { rx_status = 0x55; while (1); } while (0)
#define RX_ASSERT(cond) do { \
    if (!(cond)) { rx_status = 0xEE; rx_fail_line = __LINE__; while (1); } \
  } while (0)

volatile __xdata uint8_t  g_buf[8] = {
    0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87
};
volatile __xdata uint8_t  g_v = 0xAD;
volatile __xdata uint16_t g_w = 0x1234;
volatile __xdata uint32_t g_l = 0x89ABCDEFUL;
/* Use a directly addressable DATA byte here.  The MT-1C test is about the
   callee_saves R0/R1 scratch contract; an IDATA initializer would exercise a
   separate legacy startup/addressing limitation and make the test ambiguous. */
volatile __data uint8_t  g_iv = 0x5A;
volatile __xdata uint32_t g_d[4] = {
    0x11112222UL, 0x33334444UL, 0x55556666UL, 0x77778888UL
};
volatile __xdata uint16_t g_ma = 0xC0DE;
volatile __xdata uint16_t g_mb = 0xBEEF;
volatile __xdata uint8_t  g_marker;
volatile __xdata uint16_t g_pressure_seed[7] = { 1, 2, 3, 4, 5, 6, 7 };
volatile __idata uint16_t g_pressure_iv;

typedef struct { uint16_t a; uint32_t b; } Pair;

static Pair make_pair (uint16_t a, uint32_t b)
{
    Pair p;
    p.a = a;
    p.b = b;
    return p;
}

static uint32_t pair_sum (Pair p)
{
    return p.a + p.b;
}

#pragma callee_saves cs_helper
static uint16_t cs_helper (uint16_t x)
{
    return (uint16_t)((x << 1) + (x >> 2) + 3);
}

#pragma callee_saves cs_ptr
static uint16_t cs_ptr (uint16_t x)
{
    volatile __data uint8_t *p = &g_iv;
    return (uint16_t)(x + *p);
}

/* Keep seven words live while an IDATA word is accessed.  The legacy
   allocator excludes R0/R1 whenever pointer scratch is required; ralloc2
   must preserve the same invariant or getFreePtr can overwrite a live word
   operand while forming the indirect address. */
static uint16_t ptr_pressure (void)
{
    uint16_t w0 = g_pressure_seed[0], w1 = g_pressure_seed[1];
    uint16_t w2 = g_pressure_seed[2], w3 = g_pressure_seed[3];
    uint16_t w4 = g_pressure_seed[4], w5 = g_pressure_seed[5];
    uint16_t w6 = g_pressure_seed[6];
    g_pressure_iv = 9;
    return (uint16_t)((w6 + g_pressure_iv) ^ w0 ^ w1 ^ w2 ^
                      w3 ^ w4 ^ w5);
}

void main (void)
{
    rx_status = 0x00;
    rx_fail_line = 0x0000;
    rx_extra = 0x00000000UL;

    /* X1: setjmp/longjmp round trip with a marker written between. */
    {
        jmp_buf env;
        g_marker = 0x00;
        if (setjmp (env) == 0)
            {
                g_marker = 0xC3;
                longjmp (env, 1);
            }
        RX_ASSERT (g_marker == 0xC3);
    }

    /* X2: pointer walk with post-increment. */
    {
        volatile __xdata uint8_t *p = g_buf;
        uint16_t s = 0;
        uint8_t i;
        for (i = 0; i < 8; ++i)
            s = (uint16_t)(s + *p++);
        RX_ASSERT (s == 0x025C);
    }

    /* X3: switch dispatch. */
    {
        uint8_t k = g_v & 0x07;
        uint16_t acc = 0;
        switch (k)
          {
          case 0: acc |= 0x01; break;
          case 1: acc |= 0x02; break;
          case 2: acc |= 0x04; break;
          case 3: acc |= 0x08; break;
          case 4: acc |= 0x10; break;
          case 5: acc |= 0x20; break;
          case 6: acc |= 0x40; break;
          case 7: acc |= 0x80; break;
          default: acc |= 0x100; break;
          }
        RX_ASSERT (acc == 0x0020);
    }

    /* X4: aggregate by-value parameter and hidden-pointer return. */
    {
        Pair p = make_pair (0x1234, 0xDEADBEEFUL);
        uint32_t t = pair_sum (p);
        RX_ASSERT (t == 0xDEADD123UL);
        RX_ASSERT ((uint16_t)(p.a * 3) == 0x369C);
    }

    /* X5: bit-scalar arithmetic and shifts. */
    {
        _Bool f = 1;
        uint16_t w = g_w;
        uint32_t l = g_l;
        f = (_Bool)(f && (w & 0x0200));
        RX_ASSERT (f == 1);
        RX_ASSERT ((uint16_t)(w << 3) == 0x91A0);
        RX_ASSERT ((uint16_t)(w >> 2) == 0x048D);
        RX_ASSERT (l >> 5 == 0x044D5E6FUL);
    }

    /* X6: callee_saves pragma — the callee must preserve the caller's
       registers itself, driven by the assignment's regsUsed output. */
    {
        uint16_t a = g_w;
        uint16_t keep = a ^ 0x00FF;
        uint16_t r = cs_helper (a);
        RX_ASSERT (keep == 0x12CB);
        RX_ASSERT (r == 0x28F8);
    }

    /* X7: four simultaneously live dwords exceed the three legal DR
       tuples, forcing the native 16x16->32 result into the
       bytes-anywhere fallback where the R8-R15 exclusion is what keeps
       it out of the native-multiply scratch set. */
    {
        uint32_t d0 = g_d[0], d1 = g_d[1];
        uint32_t d2 = g_d[2], d3 = g_d[3];
        uint32_t p = (uint32_t)g_ma * g_mb;
        RX_ASSERT (p == 0x8FD8D342UL);
        RX_ASSERT ((d0 ^ d1 ^ d2 ^ d3 ^ p) == 0x8FD85BCAUL);
    }

    /* X8: callee_saves + near/DATA pointer access with the caller's
       eight live bytes extending into R0/R1.  The callee must include
       ar0/ar1 in its save set (driven by the ported ptrRegReq
       accounting) or the caller's low bytes are destroyed. */
    {
        uint8_t b0 = g_buf[0], b1 = g_buf[1], b2 = g_buf[2];
        uint8_t b3 = g_buf[3], b4 = g_buf[4], b5 = g_buf[5];
        uint8_t b6 = g_buf[6], b7 = g_buf[7];
        uint16_t pv = cs_ptr (g_w);
        RX_ASSERT (pv == 0x128E);
        RX_ASSERT ((uint16_t)(b0 + b1 + b2 + b3 + b4 + b5 + b6 + b7)
                   == 0x025C);
    }

    /* X9: an IDATA word access while a same-function word remains live.
       This is distinct from X8's callee_saves check: R0/R1 must be excluded
       from the allocator, not merely added to the callee save set.  The
       legacy stack-auto path cannot keep this global IDATA probe isolated
       from its SPX frame, so retain the existing all-model stack-auto
       coverage in X1-X8 and run this allocator-specific probe in the two
       ordinary memory models. */
#ifndef __SDCC_STACK_AUTO
    RX_ASSERT (ptr_pressure () == 0x0017);
#endif

    RX_PASS ();
}
