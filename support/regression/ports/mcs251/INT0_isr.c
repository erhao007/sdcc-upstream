__sfr __at (0x88) mcs251_tcon;
#define TCON_IE0 0x02
volatile unsigned long __xdata mcs251_int0_irq_count;
void INT0_isr (void) __interrupt (0) { ++mcs251_int0_irq_count; mcs251_tcon &= (unsigned char)~TCON_IE0; }
