/* ralloc-baseline.c - MT-1A legacy register-allocator behaviour freeze.

   Purpose: lock the OBSERVABLE behaviour of the legacy mcs251 allocator
   (the former serial allocator) before ralloc2 work starts, so that any future change that
   is not a correct re-allocation shows up as a failing sample here.

   Covered hazards (each section is a minimal failing sample for one
   allocator responsibility):
     S1 byte register pressure      (R0..R9/R12..R15 byte pool)
     S2 word/dword tuple pressure   (WR/DR big-endian contiguous tuples)
     S3 byte/word/dword overlap     (mixed-width temporaries, aliasing maths)
     S4 values live across calls    (default caller-save push/pop)
     S5 forced spilling             (>14 live bytes in one scope)
     S6 native 16x16->32 multiply   (mul wr12,wr8 + R8-R15 scratch policy)
     S7 recursion (__reentrant)     (SPX frames, stack-auto compatible)
     S8 float helper calls          (soft-float runtime calls with live ints)
     S9 interrupt under pressure    (full 24-item ISR save/restore)

   All operands are loaded from volatile XDATA seeds so nothing can be
   constant-folded: every value below is materialised at run time and
   must survive register allocation, spilling, calls and one interrupt.

   Expected constants were derived with an independent host calculation;
   they are part of the frozen baseline.  The program reports through the
   same IRAM control block protocol as tests/abi (status 0x55 = PASS,
   0xEE = FAIL with source line at 0x32..0x33); build with --data-loc 0x38.

   This file freezes legacy BEHAVIOUR.  It must not be read as an ABI
   requirement: a future allocator may pick different registers as long
   as every assertion below still holds.  */

#include <stdint.h>

volatile __data __at (0x30) uint8_t  rb_status;
volatile __data __at (0x31) uint8_t  rb_reserved;
volatile __data __at (0x32) uint16_t rb_fail_line;
volatile __data __at (0x34) uint32_t rb_extra;

#define RB_PASS() do { rb_status = 0x55; while (1); } while (0)
#define RB_ASSERT(cond) do { \
    if (!(cond)) { rb_status = 0xEE; rb_fail_line = __LINE__; while (1); } \
  } while (0)

__sfr __at (0x88) TCON;
__sfr __at (0xA8) IE;

/* ------------------------------------------------------------------ */
/* Seeds: initialised XDATA, never written by the program.             */
/* ------------------------------------------------------------------ */
volatile __xdata uint8_t  g_b[14] = {
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
    0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE
};
volatile __xdata uint16_t g_w[8] = {
    0x1234, 0x5678, 0xABCD, 0xEF01, 0x0F0F, 0xF0F0, 0xAAAA, 0x5555
};
volatile __xdata uint32_t g_l[4] = {
    0x11223344UL, 0x55667788UL, 0x99AABBCCUL, 0xDDEEFF00UL
};
volatile __xdata uint16_t g_t[16] = {
    0x1000, 0x1111, 0x1222, 0x1333, 0x1444, 0x1555, 0x1666, 0x1777,
    0x1888, 0x1999, 0x1AAA, 0x1BBB, 0x1CCC, 0x1DDD, 0x1EEE, 0x1FFF
};
volatile __xdata uint16_t g_mul_a[3] = { 50000, 0xFFFF, 0x1234 };
volatile __xdata uint16_t g_mul_b[3] = { 40000, 0xFFFF, 0x5678 };
volatile __xdata uint16_t g_fg[4] = { 0x1111, 0x2222, 0x3333, 0x4444 };
volatile __xdata uint32_t g_fgl[2] = { 0xAABBCCDDUL, 0x12345678UL };
volatile __xdata uint32_t g_isr_seed = 0x00C0FFEEUL;
volatile __xdata float    g_f0 = 1.5f;
volatile __xdata float    g_f1 = 3.0f;
volatile __xdata float    g_f2 = 0.25f;
volatile __xdata float    g_f3 = 4.0f;

volatile __xdata uint32_t g_sink;
volatile __xdata uint32_t g_isr_result;
__data volatile uint8_t   g_isr_fired = 0;

/* ------------------------------------------------------------------ */
/* Callee used by call-pressure and ISR sections: allocates its own    */
/* registers, so callers must save whatever they keep live.            */
/* ------------------------------------------------------------------ */
unsigned long helper_mix(unsigned long x, unsigned int y)
{
    unsigned long t = x + x + x;
    return t + y + 7UL;
}

/* ------------------------------------------------------------------ */
/* S1: fourteen simultaneously live byte values.                       */
/* ------------------------------------------------------------------ */
uint32_t byte_pressure(void)
{
    uint8_t b0 = g_b[0],  b1 = g_b[1],  b2 = g_b[2],  b3 = g_b[3];
    uint8_t b4 = g_b[4],  b5 = g_b[5],  b6 = g_b[6],  b7 = g_b[7];
    uint8_t b8 = g_b[8],  b9 = g_b[9],  b10 = g_b[10], b11 = g_b[11];
    uint8_t b12 = g_b[12], b13 = g_b[13];

    b1 = (uint8_t)(b1 + b0);
    b3 = (uint8_t)(b3 - b2);
    b5 = (uint8_t)(b5 ^ b4);
    b7 = (uint8_t)(b7 + b6);
    b9 = (uint8_t)(b9 ^ b8);
    b11 = (uint8_t)(b11 + b10);
    b13 = (uint8_t)(b13 - b12);

    return (uint32_t)(uint16_t)(b0 + b1 + b2 + b3 + b4 + b5 + b6 + b7 +
                                b8 + b9 + b10 + b11 + b12 + b13);
}

/* ------------------------------------------------------------------ */
/* S2: eight live words + four live longs force WR/DR tuples and      */
/* spilling at the same time.                                          */
/* ------------------------------------------------------------------ */
uint32_t word_dword_pressure(void)
{
    uint16_t w0 = g_w[0], w1 = g_w[1], w2 = g_w[2], w3 = g_w[3];
    uint16_t w4 = g_w[4], w5 = g_w[5], w6 = g_w[6], w7 = g_w[7];
    uint32_t l0 = g_l[0], l1 = g_l[1], l2 = g_l[2], l3 = g_l[3];

    w1 = (uint16_t)(w1 ^ w0);
    w3 = (uint16_t)(w3 + w2);
    w5 = (uint16_t)(w5 - w4);
    w7 = (uint16_t)(w7 ^ w6);
    l1 = l1 ^ l0;
    l3 = l3 + l2;

    return (uint32_t)(w0 + w1 + w2 + w3 + w4 + w5 + w6 + w7) +
           (l0 + l1 + l2 + l3);
}

/* ------------------------------------------------------------------ */
/* S3: mixed-width access to the same values: bytes extracted from a   */
/* word, a word extracted from a dword, then rebuilt and combined.     */
/* ------------------------------------------------------------------ */
uint32_t overlap_probe(void)
{
    uint16_t w = g_w[2];
    uint32_t d = g_l[0];
    uint8_t hi = (uint8_t)(w >> 8);
    uint8_t lo = (uint8_t)(w & 0xFF);
    uint16_t lo16 = (uint16_t)(d & 0xFFFF);
    uint8_t b1 = (uint8_t)((d >> 16) & 0xFF);

    hi = (uint8_t)(hi ^ 0xFF);
    lo = (uint8_t)(lo + 1);
    b1 = (uint8_t)(b1 + 1);
    w = (uint16_t)((hi << 8) | lo);
    d = (d & 0xFF00FFFFUL) | ((uint32_t)b1 << 16) | 0x50UL;

    return (uint32_t)(w + lo16 + (uint8_t)(d >> 24));
}

/* ------------------------------------------------------------------ */
/* S4: multi-byte values live across two calls (default caller-save).  */
/* ------------------------------------------------------------------ */
uint32_t call_pressure(void)
{
    uint32_t a = g_l[0], c = g_l[2];
    uint16_t w = g_w[1];
    uint32_t r1 = helper_mix(a, w);
    uint32_t keep = a ^ c;          /* live across the first call */
    uint32_t r2 = helper_mix(keep, (uint16_t)r1);
    return r1 ^ r2 ^ keep;
}

/* ------------------------------------------------------------------ */
/* S5: sixteen simultaneously live words (32 bytes) overflow every     */
/* register tuple combination and must spill correctly.                */
/* ------------------------------------------------------------------ */
uint32_t spill_pressure(void)
{
    uint16_t t0 = g_t[0],  t1 = g_t[1],  t2 = g_t[2],  t3 = g_t[3];
    uint16_t t4 = g_t[4],  t5 = g_t[5],  t6 = g_t[6],  t7 = g_t[7];
    uint16_t t8 = g_t[8],  t9 = g_t[9],  t10 = g_t[10], t11 = g_t[11];
    uint16_t t12 = g_t[12], t13 = g_t[13], t14 = g_t[14], t15 = g_t[15];

    t1 = (uint16_t)(t1 ^ t0);
    t3 = (uint16_t)(t3 + t2);
    t5 = (uint16_t)(t5 ^ t4);
    t7 = (uint16_t)(t7 + t6);
    t9 = (uint16_t)(t9 ^ t8);
    t11 = (uint16_t)(t11 + t10);
    t13 = (uint16_t)(t13 ^ t12);
    t15 = (uint16_t)(t15 + t14);

    return (uint32_t)(t0 + t1 + t2 + t3 + t4 + t5 + t6 + t7 +
                      t8 + t9 + t10 + t11 + t12 + t13 + t14 + t15) +
           (uint16_t)(t0 ^ t5) + (uint16_t)(t3 ^ t7) + (uint16_t)(t9 ^ t14);
}

/* ------------------------------------------------------------------ */
/* S6: native unsigned 16x16->32 multiplies with live longs and one    */
/* call interleaved (exercises the mul wr12,wr8 scratch policy and    */
/* its save/restore path).                                             */
/* ------------------------------------------------------------------ */
uint32_t native_mul_pressure(void)
{
    uint32_t keep0 = g_l[1], keep1 = g_l[3];
    uint32_t p1 = (uint32_t)g_mul_a[0] * g_mul_b[0];
    uint32_t keep2 = helper_mix(keep0, 0x0007);
    uint32_t p2 = (uint32_t)g_mul_a[1] * g_mul_b[1];
    uint32_t p3 = (uint32_t)g_mul_a[2] * g_mul_b[2];
    return p1 ^ p2 ^ p3 ^ keep0 ^ keep1 ^ keep2;
}

/* ------------------------------------------------------------------ */
/* S7: recursion via __reentrant SPX frames.                           */
/* ------------------------------------------------------------------ */
uint16_t fib_r(uint8_t n) __reentrant
{
    if (n < 2)
        return n;
    return (uint16_t)(fib_r((uint8_t)(n - 1)) + fib_r((uint8_t)(n - 2)));
}

uint32_t fact_r(uint8_t n) __reentrant
{
    if (n < 2)
        return 1UL;
    return (uint32_t)n * fact_r((uint8_t)(n - 1));
}

/* ------------------------------------------------------------------ */
/* S9: non-leaf ISR doing its own allocation + call; foreground keeps  */
/* multi-byte values in registers across the interrupt.                */
/* ------------------------------------------------------------------ */
void isr_pressure(void) __interrupt (1)
{
    g_isr_result = helper_mix(g_isr_seed, 7);
    g_isr_fired++;
}

void main(void)
{
    rb_status = 0x00;
    rb_fail_line = 0x0000;
    rb_extra = 0x00000000UL;

    /* S1 */
    g_sink = byte_pressure();
    RB_ASSERT(g_sink == 0x0582UL);

    /* S2 (word sum wraps at 16 bits, dword sum at 32, host-derived) */
    g_sink = word_dword_pressure();
    RB_ASSERT(g_sink == 0x66AB275CUL);

    /* S3 */
    g_sink = overlap_probe();
    RB_ASSERT(g_sink == 0x00008823UL);

    /* S8: soft-float helper calls with integers live ACROSS them.
       lf0/lf1/ll0 are loaded before the first __fsmul/__fsadd call and
       consumed only after the last one, so the caller-save discipline
       around runtime helpers must preserve them.  Executed before the
       S4/S6 call sections so a broken call-site save policy fails HERE
       first, proving this section's own detection power. */
    {
        uint16_t lf0 = g_fg[0], lf1 = g_fg[1];
        uint32_t ll0 = g_fgl[1];
        float f = g_f0;
        f = f * g_f1;
        f = f + g_f2;
        RB_ASSERT((uint32_t)(f * g_f3) == 19UL);
        RB_ASSERT((uint16_t)(lf0 + lf1) == 0x3333);
        RB_ASSERT(ll0 == 0x12345678UL);
    }

    /* S4: r1 = helper_mix(0x11223344,0x5678) = 0x3366F04B,
            keep = 0x11223344^0x99AABBCC = 0x88888888,
            r2 = helper_mix(keep,0xF04B) = 0x999A89EA (host-derived). */
    g_sink = call_pressure();
    RB_ASSERT(g_sink == 0x2274F129UL);

    /* S5 */
    g_sink = spill_pressure();
    RB_ASSERT(g_sink == 0x0000C218UL);

    /* S6 */
    g_sink = native_mul_pressure();
    RB_ASSERT(g_sink == 0x06567A4FUL);

    /* S7 */
    RB_ASSERT(fib_r(12) == 144);
    RB_ASSERT(fact_r(6) == 720);

    /* S9: interrupt under register pressure.  The fg values are loaded
       BEFORE the software-latched Timer0 interrupt and combined AFTER
       it; the ISR's full save/restore set must keep them intact. */
    {
        uint16_t f0 = g_fg[0], f1 = g_fg[1], f2 = g_fg[2], f3 = g_fg[3];
        uint32_t fl0 = g_fgl[0], fl1 = g_fgl[1];
        uint32_t mixed = fl0 + fl1;

        IE |= 0x82;      /* EA=1, ET0=1 */
        TCON |= 0x20;    /* TF0: latch Timer0 interrupt */

        RB_ASSERT((uint16_t)(f0 + f1 + f2 + f3) == 0xAAAA);
        RB_ASSERT(mixed == 0xBCF02355UL);
        RB_ASSERT(g_isr_fired == 1);
        RB_ASSERT(g_isr_result == 0x0242FFD8UL);
        IE &= (uint8_t)~0x80;
    }

    RB_PASS();
}
