__sfr __at (0x88) mcs251_tcon;
__sfr __at (0xD1) mcs251_psw1;
#define TCON_IE0 0x02
volatile unsigned long __xdata mcs251_int0_irq_count;
void INT0_isr (void) __interrupt (0) {
  ++mcs251_int0_irq_count;
  mcs251_tcon &= (unsigned char)~TCON_IE0;
  /* Clobber PSW1 so the hardening test can prove the 4-byte interrupt frame
     restores it on RETI. Other INT0 tests don't read PSW1, so this is
     invisible to them. */
  mcs251_psw1 = 0x00;
}
