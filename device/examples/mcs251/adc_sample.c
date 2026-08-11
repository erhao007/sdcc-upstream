/*
 * adc_sample.c - STC32G12K128 ADC sampling example for SDCC mcs251.
 *
 * Demonstrates reading the on-chip 12-bit ADC on channel 0 (P1.0) and
 * the internal 1.19 V reference (channel 15).  The result is right-
 * justified and signalled on P0 so it can be checked on real silicon
 * or inspected in the uCsim SFR view.
 *
 * ADC register summary (all traditional SFR on STC32G12K128):
 *   ADC_CONTR (0xBC)  control: ADC_POWER / ADC_START / ADC_FLAG / channel
 *   ADC_RES   (0xBD)  result high byte
 *   ADC_RESL  (0xBE)  result low byte
 *   ADCCFG    (0xDE)  RESFMT (bit 5 alignment) + SPEED[7:5]
 *   ADCTIM    (0x7EFEA8, extended)  internal sampling timing
 *
 * Build:
 *   sdcc -mmcs251 adc_sample.c
 *
 * The simulator (uCsim) does not model the ADC analogue front end, so
 * the conversion result reads as 0; on real STC32G12K128 silicon this
 * example returns the measured voltage count.
 */

#include <stc32g12k128.h>

/* ADC channel assignments on STC32G12K128 (data sheet). */
#define ADC_CH_P1_0      0       /* ADC channel 0  -> P1.0 */
#define ADC_CH_P1_1      1       /* ADC channel 1  -> P1.1 */
#define ADC_CH_INT_1V19  15      /* internal 1.19 V band-gap reference */

/* ADCCFG value for a ~24 MHz system clock:
 *   RESFMT (bit 5) = 1  -> result right-justified (low 8 bits in ADC_RESL,
 *                          high 4 bits in ADC_RES[3:0])
 *   SPEED[7:5] = 0b001  -> ADC_CLK = SYSclk / 2 / (1+1) = SYSclk/4
 * The SPEED field programs the low 3 bits of (SPEED+1), so 0x2F sets
 * SPEED=1 (ADC clock = SYSclk/4) and RESFMT=right-justified. */
#define ADCCFG_24MHZ_RIGHT  0x2F

/* Initialise the ADC: power up, set the timing and clock, allow the
 * analogue block to settle.  Channel is selected per-conversion. */
static void adc_init(void)
{
    P_SW2 |= P_SW2_EAXFR;        /* enable extended SFR access */
    ADCTIM  = 0x3F;              /* data-sheet reset value (22-30 MHz) */
    P_SW2 &= ~P_SW2_EAXFR;

    ADCCFG    = ADCCFG_24MHZ_RIGHT;
    ADC_CONTR = ADC_CONTR_ADC_POWER;   /* power on, no conversion yet */

    /* Allow the ADC to settle after power-up (data sheet: ~1 ms).
     * A busy-wait loop calibrated for ~24 MHz is good enough here. */
    { unsigned int i; for (i = 0; i < 5000; i++) ; }
}

/* Read one 12-bit sample from the given channel (0..15).
 * Returns the right-justified 12-bit result (0..4095). */
static unsigned int adc_read(unsigned char channel)
{
    unsigned int result;

    /* Select channel and start a conversion.  Keep ADC_POWER set. */
    ADC_CONTR = ADC_CONTR_ADC_POWER
              | ADC_CONTR_ADC_START
              | (channel & ADC_CONTR_ADC_CHS);

    /* Wait for the conversion-complete flag (hardware clears START). */
    while (!(ADC_CONTR & ADC_CONTR_ADC_FLAG))
        ;

    /* Clear the flag for the next conversion. */
    ADC_CONTR &= ~ADC_CONTR_ADC_FLAG;

    /* Right-justified: low 8 bits in ADC_RESL, high 4 bits in ADC_RES. */
    result  = ADC_RES & 0x0F;
    result <<= 8;
    result |= ADC_RESL;
    return result;
}

void main(void)
{
    unsigned int ch0, ch15;

    /* Configure P0 as push-pull output so we can display a status byte,
     * and P1.0 (ADC channel 0 input) as high-impedance input. */
    P0M1 = 0x00;  P0M0 = 0xFF;
    P1M1 |= 0x01;  P1M0 &= ~0x01;

    adc_init();

    ch0  = adc_read(ADC_CH_P1_0);        /* external voltage on P1.0 */
    ch15 = adc_read(ADC_CH_INT_1V19);    /* internal 1.19 V reference */

    /* Signal the low byte of each result on P0 so it is visible.
     * On the simulator P0 stays 0 (no ADC model); on real silicon the
     * value tracks the applied voltage. */
    P0 = (unsigned char)(ch0 ^ ch15);

    for (;;)
        ;
}
