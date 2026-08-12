__sfr __at(0x80) P0; __sfr __at(0x88) TCON; __sfr __at(0x89) TMOD;
__sfr __at(0xA8) IE; __sfr __at(0x8D) TH1; __sfr __at(0x8B) TL1;
volatile unsigned long __xdata t1_count;
void t1_isr(void) __interrupt(3) { ++t1_count; TCON &= ~0x80; }
void main(void) {
  t1_count = 0;
  TMOD = (TMOD & 0x0F) | 0x20; TH1 = 0; TL1 = 0;
  IE |= 0x88; TCON |= 0x40;
  unsigned int t = 5000; while (t1_count == 0 && --t);
  P0 = (t1_count > 0) ? 0x55 : 0xAA; while(1);
}
