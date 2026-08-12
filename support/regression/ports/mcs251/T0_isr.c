/* Do not include the complete device header here.  The common framework is
   linked into unrelated tests, some of which legitimately define names such
   as B that the header exports as SFR symbols. */
__sfr __at (0x88) mcs251_tcon;

#define TCON_TF0 0x20

volatile unsigned long __xdata mcs251_timer0_irq_count;

void
T0_isr (void) __interrupt (1)
{
  ++mcs251_timer0_irq_count;
  mcs251_tcon &= (unsigned char)~TCON_TF0;
}
