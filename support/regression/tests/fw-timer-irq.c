/*
   fw-timer-irq.c - Timer0 interrupt end-to-end test (mcs251 simulator).

   Verifies that the mcs251 simulator delivers a Timer0 overflow interrupt
   end to end. The peripheral counts, TF0 is raised, do_interrupt accepts it
   (pushing the return PC onto the SPX/edata stack via the cl_uc251
   inst_lcall override), the vector's ejmp reaches the ISR, and RETI unwinds
   it_levels and returns.  The ISR increments a counter the main thread
   observes.

   Uses Timer0 mode 2 (8-bit auto-reload, reload 0 -> overflow every 256
   ticks) which both the inherited mcs51 cl_timer0 model and the STC32G
   hardware support.
*/

#include <testfwk.h>

#if defined(__SDCC_mcs251)
#include <stc32g12k128.h>

extern volatile unsigned long __xdata mcs251_timer0_irq_count;
#endif

void
testTimer0Counts(void)
{
#if defined(__SDCC_mcs251)
  TMOD = (TMOD & 0xF0) | 0x02;
  TH0 = 0x00;
  TL0 = 0x00;
  TCON |= 0x10;             /* TR0 = 1 */

  unsigned char t0 = TL0;
  unsigned int i;
  for (i = 0; i < 300; i++)
    ;

  TCON &= ~0x10;
  ASSERT(TL0 != t0);
#endif
}

void
testTimer0InterruptFires(void)
{
#if defined(__SDCC_mcs251)
  mcs251_timer0_irq_count = 0;

  TMOD = (TMOD & 0xF0) | 0x02;
  TH0 = 0x00;
  TL0 = 0x00;
  IE  |= 0x82;             /* EA (0x80) | ET0 (0x02) */
  TCON |= 0x10;            /* TR0 = 1 */

  unsigned int timeout = 2000;
  while (mcs251_timer0_irq_count == 0 && --timeout)
    ;

  TCON &= ~0x10;
  IE &= ~0x80;

  ASSERT(mcs251_timer0_irq_count > 0);
#endif
}

/*
   Reinforcement for the 4-byte interrupt frame (CONFIG1.INTR=1, datasheet
   p1701): drive the Timer0 ISR many times back-to-back. If the entry/RETI
   frame did not pair exactly (e.g. push 4 / pop 2), SPX would drift on every
   interrupt and the stack would corrupt within a few dozen iterations,
   stalling the count. Reaching >= 50 proves the 4-byte frame pairs
   repeatedly and the it_levels bookkeeping in inst_reti251 survives
   consecutive IRQs.
*/
void
testTimer0ManyInterruptsStable4ByteFrame(void)
{
#if defined(__SDCC_mcs251)
  mcs251_timer0_irq_count = 0;

  TMOD = (TMOD & 0xF0) | 0x02;
  TH0 = 0x00;
  TL0 = 0x00;
  IE  |= 0x82;             /* EA (0x80) | ET0 (0x02) */
  TCON |= 0x10;            /* TR0 = 1 */

  unsigned long timeout = 200000;
  while (mcs251_timer0_irq_count < 50 && --timeout)
    ;

  TCON &= ~0x10;
  IE &= ~0x80;

  ASSERT(mcs251_timer0_irq_count >= 50);
#endif
}
