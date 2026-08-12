__sfr __at (0x98) mcs251_scon;
#define SCON_TI 0x02
#define SCON_RI 0x01
volatile unsigned long __xdata mcs251_uart_irq_count;
void uart1_isr (void) __interrupt (4) { ++mcs251_uart_irq_count; mcs251_scon &= (unsigned char)~(SCON_TI | SCON_RI); }
