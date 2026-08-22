/* ralloc2-aggregate-bit.c - MT-1E class-1/2 directed fixture: aggregate
   and bit-valued live ranges no longer force the whole-function legacy
   fallback.

   Five separately compiled modes (the pressure guard stays fail-closed
   for this class, so each mode is a small function inside its envelope;
   same pattern as ralloc2-mulreg.c):
     default              struct members: Trio arithmetic (aggregate
                          decay temporaries through the legacy aggrToPtr
                          convention)
     _AGG_BITFIELD        8-bit and 12-bit bitfield write/read/extract
                          (bitfield temporaries are plain scalar
                          registers by size, mirroring legacy isFlagVar;
                          true bit temporaries use the b0..b7 legacy
                          registers and only spill under pressure)
     _AGG_BITPTR          __bit true symbols, constant-index array and a
                          general 3-byte __xdata pointer walk (pointer
                          value still fail-closed spilt)
     _AGG_PTRIFX          volatile global pointer dereferenced directly
                          in if-conditions (review round 1 P1: volatile-
                          typed spilt temporaries, accuse and REG_CND
                          shapes)
     _AGG_PTRVAR          local pointer copied at runtime from a global
                          volatile pointer, then dereferenced (review
                          round 2 P1: packRegisters sir-packs the local,
                          so the spill pass must adopt the pre-assigned
                          storage or the dereference reads address 0)

   Aggregate/bitfield member access is GET_VALUE_AT_ADDRESS at the
   iCode level, so the aggregate class and dereferences close together;
   calls/ISRs/reentrant/stack-auto (still fail-closed to legacy) are
   deliberately absent, and there is no struct-to-struct assignment
   (it may lower to a runtime memcpy call).

   Same self-checking protocol as ralloc-baseline.c: control block at
   0x30 (0x55 PASS / 0xEE + line FAIL), build with --data-loc 0x38.
   All operands come from volatile XDATA seeds; expected constants were
   derived with an independent host calculation.  */

#include <stdint.h>

volatile __data __at (0x30) uint8_t  ab_status;
volatile __data __at (0x31) uint8_t  ab_reserved;
volatile __data __at (0x32) uint16_t ab_fail_line;
volatile __data __at (0x34) uint32_t ab_extra;

#define AB_PASS() do { ab_status = 0x55; while (1); } while (0)
#define AB_ASSERT(cond) do { \
    if (!(cond)) { ab_status = 0xEE; ab_fail_line = __LINE__; while (1); } \
  } while (0)

typedef struct { uint8_t lo; uint16_t mid; uint8_t hi; } Trio;
typedef struct { uint8_t seq : 3; uint8_t en : 1; uint8_t tag : 4; } Mix8;
typedef struct { uint16_t code : 12; uint16_t spare : 4; } Mix16;

volatile __xdata uint8_t  g_lo = 0x21;
volatile __xdata uint16_t g_mid = 0x1234;
volatile __xdata uint8_t  g_hi = 0x57;
volatile __xdata uint8_t  g_seq = 5;
volatile __xdata uint8_t  g_en = 1;
volatile __xdata uint8_t  g_tagseed = 0x3D;
volatile __xdata uint16_t g_code = 0x0ABC;
volatile __xdata uint16_t g_spareseed = 0x0005;
volatile __xdata uint8_t  g_bitseed = 0x81;
volatile __xdata uint8_t  g_arr[4] = { 0x10, 0x32, 0x54, 0x76 };
volatile __xdata uint32_t g_sink;
volatile __xdata uint8_t * volatile g_vptr = &g_arr[1];
volatile __xdata __at (0x012345) uint8_t g_pointee = 0x5A;
volatile __xdata uint8_t * volatile g_dynptr = &g_pointee;

__bit g_bitglob;

#ifdef MCS251_RALLOC2_AGG_BITFIELD

void main (void)
{
  Mix8 m8;
  Mix16 m16;
  m8.seq = g_seq;
  m8.en = g_en;
  m8.tag = (uint8_t)(g_tagseed & 0x0F);
  m16.code = g_code;

  uint8_t v8 = (uint8_t)((m8.seq << 1) + (m8.en ? 3u : 4u));
  uint16_t v16 = m16.code;

  AB_ASSERT (v8 == 0x0Du);
  AB_ASSERT (v16 == 0x0ABCu);

  g_sink = ((uint32_t)v16 << 8) | v8;
  AB_PASS ();
}

#elif defined (MCS251_RALLOC2_AGG_BITPTR)

void main (void)
{
  /* __bit true symbols through direct bit assignments and conditions.
     Deriving a bit from a comparison creates a two-point bit temporary
     that lands in b0..b7 and links with an undefined global under BOTH
     allocators today (pre-existing port defect, out of MT-1E scope);
     the literal-to-bit shapes here are the ones every existing gate
     already links. */
  g_bitglob = 1;
  uint8_t a3 = 0;
  if (g_bitglob)
    a3 += 0x40;
  g_bitglob = 0;
  if (!g_bitglob)
    a3 += 0x08;

  /* Constant-index array access (the store side is the POINTER_SET
     coverage). */
  g_arr[2] = (uint8_t)(g_arr[2] + 0x11);
  uint8_t a4 = g_arr[2];

  /* General 3-byte __xdata pointer walk: the pointer value stays
     fail-closed spilt, the derefs go through the type-driven paths. */
  volatile __xdata uint8_t *pb = g_arr;
  uint8_t a5 = *pb;
  pb += 2;
  a5 = (uint8_t)(a5 + *pb);

  AB_ASSERT (a3 == 0x48u);
  AB_ASSERT (a4 == 0x65u);
  AB_ASSERT (a5 == 0x75u);

  g_sink = ((uint32_t)a3 << 8) | a4;
  ab_extra = a5;
  AB_PASS ();
}

#elif defined (MCS251_RALLOC2_AGG_PTRIFX)

/* Review round 1 P1 regression: a volatile pointer variable dereferenced
   in an if-condition.  The pointer-value temporary carries a volatile
   type and spills; the byte deref result feeding a single IFX is the
   legacy accuse shape, and the compare result feeding a single IFX is
   the legacy REG_CND shape. */
void main (void)
{
  uint8_t a7 = 0;
  if (*g_vptr)
    a7 += 0x20;
  if (*g_vptr == 0x32)
    a7 += 0x04;

  AB_ASSERT (a7 == 0x24u);

  g_sink = a7;
  AB_PASS ();
}

#elif defined (MCS251_RALLOC2_AGG_PTRVAR)

/* Review round 2 P1 regression: a local pointer copied at runtime from
   a volatile global pointer and then dereferenced through a non-zero
   fixed address.  packRegisters sir-packs the local so the temporary's
   storage is pre-assigned to the true symbol; the spill pass must adopt
   that location, otherwise the symbol's storage is never emitted and
   the dereference reads address 0 (dptr,#0x0000, status 0 instead of
   the pointee value). */
void main (void)
{
  __xdata uint8_t *p = g_dynptr;
  uint8_t a8 = *p;

  AB_ASSERT (a8 == 0x5Au);

  g_sink = a8;
  AB_PASS ();
}

#else /* struct members */

void main (void)
{
  Trio t;
  t.lo = g_lo;
  t.mid = g_mid + 2;
  t.hi = g_hi;
  uint32_t a1 = (uint32_t)t.lo + t.mid + t.hi;

  AB_ASSERT (a1 == 0x12AEUL);

  g_sink = a1;
  AB_PASS ();
}

#endif
