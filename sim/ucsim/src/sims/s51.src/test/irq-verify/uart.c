__sfr __at(0x80) P0;
__sfr __at(0x88) TCON;
__sfr __at(0x89) TMOD;
__sfr __at(0xA8) IE;
__sfr __at(0x8D) TH1;
__sfr __at(0x8B) TL1;
__sfr __at(0x98) SCON;
__sfr __at(0x99) SBUF;

volatile unsigned long __xdata uart_count;

void uart_isr(void) __interrupt(4) {
  ++uart_count;
  SCON &= ~(0x02 | 0x01);  /* clear TI | RI */
}

void main(void) {
  uart_count = 0;
  TMOD = (TMOD & 0x0F) | 0x20;   /* Timer1 mode 2 */
  TH1 = 0xFD; TL1 = 0x00;
  TCON |= 0x40;                   /* TR1 = 1 */
  SCON = 0x40;                    /* UART mode 1 */
  IE |= 0x90;                     /* ES | EA */
  SBUF = 'A';                     /* transmit */
  unsigned int timeout = 5000;
  while (uart_count == 0 && --timeout) ;
  P0 = (uart_count > 0) ? 0x55 : 0xAA;  /* 0x55 = success */
  while(1);
}
