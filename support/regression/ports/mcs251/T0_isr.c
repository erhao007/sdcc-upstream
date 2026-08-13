/* Do not include the complete device header here.  The common framework is
   linked into unrelated tests, some of which legitimately define names such
   as B that the header exports as SFR symbols. */
__sfr __at (0x88) mcs251_tcon;

#define TCON_TF0 0x20
#define TCON_IE0 0x02

volatile unsigned long __xdata mcs251_timer0_irq_count;

void
T0_isr (void) __interrupt (1)
{
  ++mcs251_timer0_irq_count;
  mcs251_tcon &= (unsigned char)~TCON_TF0;
  /* Pulse IE0 so a higher-priority INT0 can nest inside this ISR (exercised by
     the nested-interrupt hardening test). When EX0 is disabled this is a
     no-op: IE0 stays pending but is never accepted, so other Timer0 tests are
     unaffected. */
  mcs251_tcon |= TCON_IE0;
}
