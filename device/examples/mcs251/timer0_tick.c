/*
 * timer0_tick.c - STC32G12K128 Timer0 periodic-interrupt example for SDCC.
 *
 * Demonstrates the embedded "1 ms tick" pattern: Timer0 runs in 16-bit
 * auto-reload mode, generates an interrupt every 1 ms, and the ISR
 * drives a software counter that toggles P0.0 every 500 ms (a visible
 * heartbeat).  This is the foundation of every cooperative scheduler,
 * debounce routine and timeout guard on small MCUs.
 *
 * Timer0 register summary (all traditional SFR):
 *   TMOD (0x89)  mode select: Timer0 in bits [3:0], Timer1 in [7:4].
 *                bit 0-1 M0/M1 select Timer0 mode (00 = 16-bit auto-reload
 *                on STC32G's 1T core; differs from classic 8051 where
 *                mode 0 is 13-bit).
 *   TH0/TL0 (0x8C/0x8A)  reload value (16-bit auto-reload).
 *   TCON (0x88)  bit 5 (TF0) overflow flag; bit 4 (TR0) run control.
 *   IE   (0xA8)  bit 1 (ET0) Timer0 interrupt enable; bit 7 (EA) global.
 *   AUXR (0x8E)  bit 7 (T0x12) selects 1T (1) vs 12T (0) clock source.
 *
 * For a 24 MHz system clock, 1 ms in 1T mode needs:
 *   reload = 65536 - 24,000,000 / 1000 = 65536 - 24000 = 41536 = 0xA240
 *
 * Build:
 *   sdcc -mmcs251 timer0_tick.c
 *
 * Note: the uCsim mcs251 model (uc251.cc) currently has no Timer or
 * interrupt model, so the ISR does not fire in simulation — the test
 * only confirms the firmware runs without crashing.  On real
 * STC32G12K128 silicon the Timer0 interrupt fires every 1 ms and the
 * example blinks P0.0 at 1 Hz (500 ms half-period).
 */

#include <stc32g12k128.h>

/* 1 ms tick at 24 MHz, Timer0 in 1T 16-bit auto-reload mode. */
#define MAIN_Fosc       24000000UL
#define TIMER0_RELOAD  (65536UL - MAIN_Fosc / 1000)   /* 0xA240 */

/* Volatile: written in ISR, read in main. */
static volatile unsigned long __xdata tick_ms;
static volatile unsigned char __xdata led;

/* Timer0 ISR: fires every 1 ms.  Hardware clears TF0 on entry. */
void timer0_isr(void) __interrupt(TIMER0_VECTOR)
{
    tick_ms++;
    /* Toggle P0.0 every 500 ticks (500 ms half-period => 1 Hz blink). */
    if ((tick_ms % 500) == 0)
    {
        led ^= 1;
        P0 = (P0 & 0xFE) | led;   /* update only P0.0 */
    }
}

/* Initialise Timer0 for 1 ms periodic interrupts at 24 MHz. */
static void timer0_init(void)
{
    /* Timer0: STC32G mode 0 (16-bit auto-reload).  Note this differs
     * from the classic 8051 where mode 0 is 13-bit: on STC's 1T cores
     * (STC15/STC8H/STC32G) mode 0 is the 16-bit auto-reload mode, so
     * TH0/TL0 are reloaded from the overflow value automatically. */
    TMOD = (TMOD & 0xF0) | 0x00;

    /* 1T mode: Timer0 counts on the full system clock, not /12. */
    AUXR |= 0x80;                /* T0x12 = 1 */

    /* Load the 16-bit reload value (auto-reload on overflow). */
    TH0 = (unsigned char)(TIMER0_RELOAD >> 8);   /* 0xA2 */
    TL0 = (unsigned char)(TIMER0_RELOAD & 0xFF); /* 0x40 */

    /* Enable Timer0 interrupt + global interrupts, then start the timer.
     * We use the bit-mask macros (IE_ET0 / IE_EA / TCON_TR0) rather than
     * bit-addressing, matching the mcs251 SFR convention. */
    IE  |= IE_ET0 | IE_EA;       /* IE.1 (ET0) + IE.7 (EA) */
    TCON |= TCON_TR0;            /* TCON.4: start Timer0 */
}

void main(void)
{
    /* Configure P0.0 as push-pull output (LED), P0.1-7 as quasi-bidirectional. */
    P0M1 &= ~0x01;
    P0M0 |= 0x01;

    tick_ms = 0;
    led = 0;
    P0 = 0x00;

    timer0_init();

    /* Main loop sleeps; all work happens in the ISR. */
    for (;;)
        ;
}
