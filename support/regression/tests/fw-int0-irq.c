/* fw-int0-irq.c - interrupt end-to-end test (mcs251 simulator). */

#include <testfwk.h>
#if defined(__SDCC_mcs251)
#include <stc32g12k128.h>
extern volatile unsigned long __xdata mcs251_int0_irq_count;
#endif
void
testInt0InterruptFires(void)
{
#if defined(__SDCC_mcs251)
  mcs251_int0_irq_count = 0;
  TCON |= 0x01;
  IE  |= 0x81;
  TCON |= 0x02;
  unsigned int timeout = 5000;
  while (mcs251_int0_irq_count == 0 && --timeout)
    ;
  IE &= ~0x80;
  TCON &= ~0x01;
  ASSERT(mcs251_int0_irq_count > 0);
#endif
}
