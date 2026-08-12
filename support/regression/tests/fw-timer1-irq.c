/* fw-timer1-irq.c - interrupt end-to-end test (mcs251 simulator). */

#include <testfwk.h>
#if defined(__SDCC_mcs251)
#include <stc32g12k128.h>
extern volatile unsigned long __xdata mcs251_timer1_irq_count;
#endif
void
testTimer1InterruptFires(void)
{
#if defined(__SDCC_mcs251)
  mcs251_timer1_irq_count = 0;
  TMOD = (TMOD & 0x0F) | 0x20;
  TH1 = 0x00; TL1 = 0x00;
  IE  |= 0x88;
  TCON |= 0x40;
  unsigned int timeout = 5000;
  while (mcs251_timer1_irq_count == 0 && --timeout)
    ;
  TCON &= ~0x40;
  IE &= ~0x80;
  ASSERT(mcs251_timer1_irq_count > 0);
#endif
}
