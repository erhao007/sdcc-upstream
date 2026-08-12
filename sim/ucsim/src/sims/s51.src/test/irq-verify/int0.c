__sfr __at(0x80) P0; __sfr __at(0x88) TCON; __sfr __at(0xA8) IE;
volatile unsigned long __xdata int0_count;
void int0_isr(void) __interrupt(0) { ++int0_count; TCON &= ~0x02; }
void main(void) {
  int0_count = 0;
  TCON |= 0x01; IE |= 0x81; TCON |= 0x02;
  unsigned int t = 5000; while (int0_count == 0 && --t);
  P0 = (int0_count > 0) ? 0x55 : 0xAA; while(1);
}
