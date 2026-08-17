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
   saves/restores the PSW1 SFR cell, the pattern below is lost. PSW1
   layout per STC32G p.553: bit7 CY, bit6 AC, bit5 N, bit4:3 RS1:RS0,
   bit2 OV, bit1 Z, bit0 reserved; CY/AC/RS/OV mirror PSW. The pattern
   keeps RS = 0 so the register bank never switches, and N/Z are masked
   off because the TCON/IE read-modify-writes legitimately update them
   before the frame is taken; CY/AC/OV must survive
   verbatim through the ISR's clobber and be restored by RETI.
*/
void
testPSW1RestoredAcrossInterrupt(void)
{
#if defined(__SDCC_mcs251)
  volatile unsigned char captured_psw1 = 0;
  mcs251_int0_irq_count = 0;
  PSW1 = 0xE4;          /* CY|AC|N|OV set, Z clear, RS = 0 (0xE4 = 0x80|0x40|0x20|0x04) */
  TCON |= 0x01;         /* IT0 edge */
  IE  |= 0x81;          /* EX0 | EA */
  TCON |= 0x02;         /* trigger INT0 (ISR clobbers PSW1 to 0, RETI restores 0xE4) */
  captured_psw1 = PSW1; /* captured immediately after RETI before any flag-modifying control flow */
  IE &= ~0x80;
  TCON &= ~0x01;
  ASSERT(mcs251_int0_irq_count > 0);
  ASSERT((captured_psw1 & 0xC4) == 0xC4);   /* CY/AC/OV restored by RETI */
#endif
}
