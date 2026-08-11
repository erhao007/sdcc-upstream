/* MCS-251 pointer/integer ABI regression. */

#include <stdint.h>
#include <testfwk.h>

static int target;

void
testPointerIntegerRoundtrip (void)
{
#if defined(__SDCC_mcs251)
  uintptr_t address = (uintptr_t)&target;

  ASSERT ((int *)address == &target);
  ASSERT ((address & UINT32_C(0xff000000)) == 0);
#endif
}
