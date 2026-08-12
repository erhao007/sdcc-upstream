__sfr __at (0x88) mcs251_tcon;
#define TCON_TF1 0x80
volatile unsigned long __xdata mcs251_timer1_irq_count;
void T1_isr (void) __interrupt (3) { ++mcs251_timer1_irq_count; mcs251_tcon &= (unsigned char)~TCON_TF1; }
