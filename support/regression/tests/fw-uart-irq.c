/* fw-uart-irq.c - UART interrupt end-to-end test (mcs251 simulator).
   The framework's __sdcc_external_startup sets TI=1 (for the UART
   fallback path).  This test enables the serial interrupt (ES|EA) and
   verifies that the pending TI is delivered to the UART ISR
   (vector 0x0023 -> uart1_isr -> RETI). */
#include <testfwk.h>
#if defined(__SDCC_mcs251)
#include <stc32g12k128.h>
extern volatile unsigned long __xdata mcs251_uart_irq_count;
#endif
void
testUartInterruptFires(void)
{
#if defined(__SDCC_mcs251)
  mcs251_uart_irq_count = 0;
  /* TI is already 1 (set by the framework startup).  Just enable ES|EA
     and the pending UART interrupt should be delivered. */
  SCON |= 0x40;                  /* ensure mode 1, keep TI */
  IE  |= 0x90;                   /* ES (0x10) | EA (0x80) */
  unsigned int timeout = 5000;
  while (mcs251_uart_irq_count == 0 && --timeout)
    ;
  IE &= ~0x80;
  ASSERT(mcs251_uart_irq_count > 0);
#endif
}
