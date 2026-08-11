/*
 * ext_int.c - STC32G12K128 external interrupt example for SDCC mcs251.
 *
 * Demonstrates external interrupt 0 (INT0, pin P3.2): each falling edge
 * on P3.2 triggers the ISR, which toggles P0.0.  This is the classic
 * "button -> interrupt -> action" pattern used for keypads, rotary
 * encoders, sensor ready-signals, and any edge-triggered external event.
 *
 * External interrupt register summary:
 *   IE    (0xA8)  bit 0 (EX0) enables INT0; bit 7 (EA) is global.
 *   TCON  (0x88)  bit 0 (IT0) selects trigger mode:
 *                   1 = falling-edge triggered (most common)
 *                   0 = low-level triggered
 *                 bit 1 (IE0) is the hardware flag (auto-cleared on
 *                 entry in edge mode).
 *   P3.2         the INT0 input pin (configure as high-impedance input
 *                 so the external pull-up/pull-down defines the idle
 *                 level).
 *
 * Build:
 *   sdcc -mmcs251 ext_int.c
 *
 * The uCsim simulator does not model external pins, so the ISR never
 * fires there; on real STC32G12K128 silicon, a button or signal on
 * P3.2 toggles P0.0 on each press.
 */

#include <stc32g12k128.h>

/* Volatile: written in ISR, read in main. */
static volatile unsigned char __xdata press_count;

/* INT0 ISR: fires on each falling edge of P3.2. */
void int0_isr(void) __interrupt(INT0_VECTOR)
{
    press_count++;
    /* Toggle P0.0 on each press. */
    P0 ^= 0x01;
}

/* Initialise external interrupt 0 on P3.2 (falling-edge trigger). */
static void int0_init(void)
{
    /* Configure P3.2 (INT0) as high-impedance input so the external
     * button/signal defines the idle level.  On STC32G the input-only
     * mode is PnM1=1, PnM0=0 for the bit. */
    P3M1 |= 0x04;                /* P3.2 input-only */
    P3M0 &= ~0x04;

    /* Falling-edge trigger (TCON.IT0 = 1).  In edge mode the hardware
     * auto-clears IE0 on ISR entry, so the ISR does not need to clear
     * the flag itself. */
    TCON |= TCON_IT0;

    /* Enable INT0 + global interrupts. */
    IE |= IE_EX0 | IE_EA;
}

void main(void)
{
    /* Configure P0.0 as push-pull output (LED). */
    P0M1 &= ~0x01;
    P0M0 |= 0x01;
    P0 = 0x00;

    press_count = 0;

    int0_init();

    /* Main loop: press_count is updated by the ISR.  A real firmware
     * would consume it here (debounce, auto-repeat, etc.). */
    for (;;)
        ;
}
