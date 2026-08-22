/* ralloc2-class5-wide.c - MT-1E Phase 1 Class 5 wide-value fixture.

   A 12-byte aggregate is the target's widest source-level value shape.  The
   front end lowers aggregate temporaries to the ABI pointer representation,
   while the hidden-return path still has to preserve all twelve payload
   bytes.  This probe keeps the payload live across a call and checks the
   resulting aggregate copy independently of the scalar >8-byte admission
   matrix in ralloc2's standalone test. */

#include <stdint.h>

volatile __data __at (0x30) uint8_t  c5w_status;
volatile __data __at (0x31) uint8_t  c5w_reserved;
volatile __data __at (0x32) uint16_t c5w_fail_line;
volatile __xdata uint32_t c5w_sink;

typedef struct
{
  uint32_t a;
  uint32_t b;
  uint32_t c;
} c5w_triple;

volatile __xdata c5w_triple c5w_seed = {
  0x01020304UL, 0x11121314UL, 0x21222324UL
};

#define C5W_PASS() do { c5w_status = 0x55; while (1); } while (0)
#define C5W_ASSERT(cond) do { \
    if (!(cond)) { c5w_status = 0xEE; c5w_fail_line = __LINE__; while (1); } \
  } while (0)

static c5w_triple
c5w_step (c5w_triple value)
{
  value.a += 1UL;
  value.b += 2UL;
  value.c += 3UL;
  return value;
}

void
main (void)
{
  c5w_triple value = c5w_step (c5w_seed);
  c5w_sink = value.a ^ value.b ^ value.c;
  C5W_ASSERT (c5w_sink ==
              (0x01020305UL ^ 0x11121316UL ^ 0x21222327UL));
  C5W_PASS ();
}
