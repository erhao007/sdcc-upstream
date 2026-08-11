/*
 * uart_echo.c - Interrupt-driven UART1 echo for STC32G12K128.
 *
 * Receives characters on UART1 (P3.0/RxD, P3.1/TxD) and echoes them
 * back, toggling P0.0 on each received byte.  Timer 1 generates the
 * baud clock at 115200 baud (22.1184 MHz, 1T mode).
 *
 * This example demonstrates:
 *   - UART1 configuration (Timer 1 baud generator)
 *   - Interrupt-driven receive (ISR echoes + LED toggle)
 *   - __code const lookup table
 *   - GPIO mode configuration
 *
 * Build:
 *   sdcc -mmcs251 uart_echo.c
 *
 * The uCsim simulator does not fully model UART RX interrupts, so
 * this is primarily a compile + code-structure check.  On real
 * STC32G12K128 hardware it echoes characters and blinks the LED.
 */

#include <stc32g12k128.h>

/* LED state */
static volatile unsigned char led_state = 0;

/* Small lookup table: ASCII '0'-'9' → hex nibble value */
__code const unsigned char ascii_to_hex[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09
};

/* Initialise UART1 at 115200 baud using Timer 1.
 * Assumes 22.1184 MHz system clock, 1T mode. */
static void uart_init(void)
{
    P_SW2 |= P_SW2_EAXFR;
    TM0PS = 0;
    TM1PS = 0;
    P_SW2 &= ~P_SW2_EAXFR;

    /* Timer 1: mode 2 (8-bit auto-reload) as baud generator */
    TMOD = 0x20;
    /* 115200 @ 22.1184 MHz 1T, SMOD=0:
     *   reload = 256 - SYSclk/(32*baud) = 256 - 22118400/32/115200 = 250 = 0xFA
     * (The /4 divisor is for 16-bit auto-reload, not mode 2.) */
    TH1 = TL1 = 0xFA;
    AUXR |= 0x40;           /* Timer 1 in 1T mode */
    TCON |= TCON_TR1;       /* start Timer 1 */

    SCON = SCON_SM1 | SCON_REN;  /* mode 1 (8-bit UART), RX enabled */
    /* Note: do NOT set TI here.  TI is set by hardware after a transmit
     * completes; setting it before enabling the UART interrupt would
     * cause an immediate interrupt storm because the ISR clears RI but
     * not TI.  The welcome message below uses the blocking send which
     * waits for TI properly. */
}

/* Send one byte over UART1 (blocking).  Call only with interrupts
 * disabled around the TI wait, or before IE_ES is enabled. */
static void uart_send(unsigned char c)
{
    SCON &= ~SCON_TI;
    SBUF = c;
    while (!(SCON & SCON_TI))
        ;
}

/* UART1 interrupt service routine: echo received byte + toggle LED.
 * Must clear BOTH RI and TI: RI on receive, TI gets set by hardware
 * after uart_send() finishes its transmit.  Clearing TI here is safe
 * because the blocking send has already observed it. */
void uart1_isr(void) __interrupt(UART1_VECTOR)
{
    if (SCON & SCON_RI)
    {
        SCON &= ~SCON_RI;     /* clear RX flag */
        unsigned char c = SBUF;
        SBUF = c;             /* echo immediately (non-blocking) */
        led_state ^= 1;
        P0 = (P0 & 0xFE) | led_state;
    }
    if (SCON & SCON_TI)
    {
        SCON &= ~SCON_TI;     /* clear TX flag to avoid re-entry */
    }
}

void main(void)
{
    /* Configure P0.0 as push-pull (LED) */
    P0M1 &= ~0x01;
    P0M0 |= 0x01;

    /* P3.0/P3.1 quasi-bidirectional (UART1 RxD/TxD) */
    P3M1 &= ~0x03;
    P3M0 &= ~0x03;

    uart_init();

    /* Send a welcome message before enabling the UART interrupt, so
     * the blocking sends don't race the ISR. */
    uart_send('R');
    uart_send('E');
    uart_send('A');
    uart_send('D');
    uart_send('Y');
    uart_send('\r');
    uart_send('\n');

    /* Enable UART1 + global interrupts only after the welcome banner,
     * so TI is already clear and the ISR won't fire spuriously. */
    SCON &= ~SCON_TI;        /* make sure TI is clear before enabling ES */
    IE |= IE_ES | IE_EA;

    /* Main loop: nothing to do, ISR handles everything */
    while (1)
        ;
}
