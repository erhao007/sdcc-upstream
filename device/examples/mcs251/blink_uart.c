/*
 * blink_uart.c - Minimal STC32G12K128 firmware example for SDCC mcs251.
 *
 * Blinks P0.0 and echoes UART1 (P3.0/P3.1) characters at 115200 baud
 * (22.1184 MHz clock, Timer 1 in 8-bit auto-reload mode).  This is a
 * self-contained smoke test: it needs no runtime library beyond crtstart.
 *
 * Build:
 *   sdcc -mmcs251 blink_uart.c
 *
 * The simulator (uCsim) does not model pin toggling or the UART baud
 * clock, so this is primarily a compile + code-size check.  On real
 * STC32G hardware (or QEMU stc32g144k246-evb) it blinks and echoes.
 */

#include <stc32g12k128.h>

/* Simple busy-wait delay (not calibrated; just to make blinking visible). */
static void delay(void)
{
    unsigned int i, j;
    for (i = 0; i < 1000; i++)
        for (j = 0; j < 1000; j++)
            ;
}

/* Send one byte over UART1, waiting for the previous transmission. */
static void uart_send(unsigned char c)
{
    while (!(SCON & SCON_TI))   /* wait for TX ready */
        ;
    SCON &= ~SCON_TI;
    SBUF = c;
}

/* Initialise UART1 at 115200 baud using Timer 1 as baud generator.
 * Assumes 22.1184 MHz system clock. */
static void uart_init(void)
{
    P_SW2 |= P_SW2_EAXFR;       /* allow access to extended SFRs */
    TM0PS = 0;                  /* Timer 0 pre-scale = 1 */
    TM1PS = 0;                  /* Timer 1 pre-scale = 1 */
    P_SW2 &= ~P_SW2_EAXFR;

    /* Timer 1: mode 2 (8-bit auto-reload), used as UART1 baud generator. */
    TMOD = 0x20;                /* T1 mode 2 */
    /* 115200 @ 22.1184 MHz, 1T mode, SMOD=0:
     *   reload = 256 - SYSclk / (32 * baud) = 256 - 22118400/32/115200
     *          = 256 - 6 = 250 = 0xFA
     * (The /4 divisor applies to 16-bit auto-reload mode, not mode 2.) */
    TH1 = TL1 = 0xFA;
    AUXR |= 0x40;               /* Timer 1 in 1T mode (STC extension) */
    TCON |= TCON_TR1;           /* start Timer 1 */

    SCON = SCON_SM1 | SCON_REN; /* mode 1 (8-bit UART), receive enabled */
    SCON |= SCON_TI;            /* allow first TX */
}

void main(void)
{
    unsigned char led = 0;

    /* Configure P0.0 as push-pull output (LED), P3.0/P3.1 quasi-bidirectional (UART). */
    P0M1 &= ~0x01;
    P0M0 |= 0x01;

    uart_init();
    uart_send('H');
    uart_send('i');

    for (;;)
    {
        P0 = (led ^= 1);        /* toggle P0.0 */
        uart_send('.');
        delay();
    }
}
