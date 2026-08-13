/* fw-int0-irq.c - interrupt end-to-end test (mcs251 simulator). */

#include <testfwk.h>
#if defined(__SDCC_mcs251)
#include <stc32g12k128.h>
extern volatile unsigned long __xdata mcs251_int0_irq_count;
extern volatile unsigned long __xdata mcs251_timer0_irq_count;
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

/*
   Hardening for the 4-byte interrupt frame (CONFIG1.INTR=1): verify a
   higher-priority interrupt NESTS inside a lower-priority ISR. T0_isr pulses
   IE0 on every Timer0 overflow; with INT0 at top priority (IPH0/IP = 1/1) it
   is accepted before T0_isr returns, so both ISRs' 4-byte frames are live on
   the SPX stack at once. If the frame did not pair exactly, or inst_reti251
   did not unwind it_levels, the nested RETI would corrupt the stack within a
   few iterations and the counts would stall.
*/
void
testNestedInterrupts4ByteFrame(void)
{
#if defined(__SDCC_mcs251)
  mcs251_timer0_irq_count = 0;
  mcs251_int0_irq_count = 0;

  /* INT0: edge trigger, TOP priority so it nests the Timer0 ISR. */
  TCON |= 0x01;         /* IT0 edge */
  IPH0 |= 0x01;         /* PX0H = 1 */
  IP   |= 0x01;         /* PX0  = 1  -> INT0 priority 3 (highest) */
  IE   |= 0x81;         /* EX0 | EA */
  /* IE0 is pulsed by T0_isr on each Timer0 overflow. */

  /* Timer0: mode 2, default LOW priority (0). */
  TMOD = (TMOD & 0xF0) | 0x02;
  TH0 = 0x00;
  TL0 = 0x00;
  IE  |= 0x82;          /* ET0 | EA */
  TCON |= 0x10;         /* TR0 = 1 */

  unsigned long timeout = 200000;
  while ((mcs251_timer0_irq_count < 5 || mcs251_int0_irq_count < 5) && --timeout)
    ;

  TCON &= ~0x10;
  IE &= ~0x80;
  TCON &= ~0x01;

  ASSERT(mcs251_timer0_irq_count >= 5);
  ASSERT(mcs251_int0_irq_count >= 5);   /* INT0 nested into Timer0 ISR */
#endif
}

/*
   Hardening for the 4-byte interrupt frame: verify PSW1 is saved on entry
   and restored on RETI. INT0_isr clobbers PSW1 to 0; unless the frame
   saves/restores the PSW1 SFR cell, the pattern below is lost. Bits 7/6
   (N/Z) are masked off because the ISR's ++count ALU legitimately updates
   them; bits 0-5 (F1/RS etc.) must survive verbatim.
*/
void
testPSW1RestoredAcrossInterrupt(void)
{
#if defined(__SDCC_mcs251)
  mcs251_int0_irq_count = 0;
  PSW1 = 0x35;          /* recognizable pattern in bits 0-5 */
  TCON |= 0x01;         /* IT0 edge */
  IE  |= 0x81;          /* EX0 | EA */
  TCON |= 0x02;         /* trigger INT0 (ISR clobbers PSW1 to 0) */
  unsigned int timeout = 5000;
  while (mcs251_int0_irq_count == 0 && --timeout)
    ;
  IE &= ~0x80;
  TCON &= ~0x01;
  ASSERT((PSW1 & 0x3F) == (0x35 & 0x3F));   /* bits 0-5 restored by RETI */
#endif
}
