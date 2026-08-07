/* Regression-test support for SDCC MCS251 on uCsim.
 *
 * Output uses the uCsim simulator interface (simif) virtual peripheral
 * at xram 0x7654 rather than the UART, so no timer-driven baud clock is
 * needed.  The SCON/SBUF declarations are kept only to satisfy the TI
 * bit used by the fallback path inherited from the mcs51 harness. */

/* define UART sfr only */
__sbit __at(0x98+1) TI;
__sfr  __at(0x99) SBUF;

unsigned char
__sdcc_external_startup (void)
{
  /* enable transmission of first byte for the (unused) UART fallback */
  TI = 1;
  return 0;
}

void
_putchar (char c)
{
  (* (volatile char __xdata *) 0x7654)= 'p';
  (* (volatile char __xdata *) 0x7654)= c;
  return;
  while (!TI)
    ;
  TI = 0;
  SBUF = c;
}

void
_initEmu (void)
{
}

void
_exitEmu (void)
{
  (* (volatile char __xdata *) 0x7654)= 's';
  * (char __idata *) 0 = * (char __xdata *) 0x7654;
}
