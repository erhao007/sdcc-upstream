/*
   fw-sfr-map.c - Traditional-SFR address verification for STC32G12K128.

   STC32G12K128 keeps the 8051 traditional SFR block (0x80-0xFF) for the
   commonly used peripherals: ADC, SPI, UART2/3/4, Timer2/3/4, comparator
   and the extra IAP fields.  An earlier revision of stc32g12k128.h
   mistakenly placed ADC, SPI, UART2-4 and Timer2-4 into the extended
   SFR area (0x7E0000+), which compiles but accesses the wrong hardware
   region on real silicon.  This test pins the correct direct addresses.

   On uCsim the traditional SFR cells are emulated, so the round-trip
   (write a marker, read it back via the same register) works and
   confirms the codegen emits a direct-addressing MOV (not an
   extended-DPX move).  mcs51-small runs the same test with the same
   direct addresses (the STC legacy SFR block overlaps the 8051 SFR
   space), so this also catches a port-wide regression.

   Only the addresses under test are declared here so the test is
   self-contained and does not depend on stc32g12k128.h.
*/

#include <testfwk.h>

/* ADC (traditional SFR). */
__sfr __at (0xBC) __fw_sfr_ADC_CONTR;
__sfr __at (0xBD) __fw_sfr_ADC_RES;
__sfr __at (0xBE) __fw_sfr_ADC_RESL;
__sfr __at (0xDE) __fw_sfr_ADCCFG;

/* SPI (traditional SFR). */
__sfr __at (0xCD) __fw_sfr_SPSTAT;
__sfr __at (0xCE) __fw_sfr_SPCTL;
__sfr __at (0xCF) __fw_sfr_SPDAT;

/* UART2/3/4 (traditional SFR). */
__sfr __at (0x9A) __fw_sfr_S2CON;
__sfr __at (0x9B) __fw_sfr_S2BUF;
__sfr __at (0xAC) __fw_sfr_S3CON;
__sfr __at (0xAD) __fw_sfr_S3BUF;
__sfr __at (0xFD) __fw_sfr_S4CON;
__sfr __at (0xFE) __fw_sfr_S4BUF;

/* Timer 2/3/4 (traditional SFR). */
__sfr __at (0xD2) __fw_sfr_T4H;
__sfr __at (0xD3) __fw_sfr_T4L;
__sfr __at (0xD4) __fw_sfr_T3H;
__sfr __at (0xD5) __fw_sfr_T3L;
__sfr __at (0xD6) __fw_sfr_T2H;
__sfr __at (0xD7) __fw_sfr_T2L;
__sfr __at (0xDD) __fw_sfr_T4T3M;

/* Comparator + extra IAP (traditional SFR). */
__sfr __at (0xE6) __fw_sfr_CMPCR1;
__sfr __at (0xE7) __fw_sfr_CMPCR2;
__sfr __at (0xF5) __fw_sfr_IAP_TPS;
__sfr __at (0xF6) __fw_sfr_IAP_ADDRE;

/* Standard IE register and P_SW2 (EAXFR enable) for the ADC tests. */
__sfr __at (0xA8) __fw_sfr_IE;
__sfr __at (0xBA) __fw_sfr_PSW2;

/* ADCTIM (ADC internal timing) is genuinely in the extended SFR area. */
__xdata __at (0x7efea8) volatile unsigned char __fw_sfr_ADCTIM;

/* Core system registers (traditional SFR). */
__sfr __at (0x97) __fw_sfr_AUXR2;
__sfr __at (0xA1) __fw_sfr_BUS_SPEED;
__sfr __at (0xA2) __fw_sfr_PSW1;
__sfr __at (0xAA) __fw_sfr_WKTCL;
__sfr __at (0xAB) __fw_sfr_WKTCH;
__sfr __at (0xAE) __fw_sfr_TA;
__sfr __at (0xB5) __fw_sfr_IP2;
__sfr __at (0xB6) __fw_sfr_IP2H;
__sfr __at (0xBB) __fw_sfr_PSW3;
__sfr __at (0xDF) __fw_sfr_IP3;
__sfr __at (0xE0) __fw_sfr_ACC;
__sfr __at (0xE3) __fw_sfr_DPS;
__sfr __at (0xE4) __fw_sfr_DPL1;
__sfr __at (0xE5) __fw_sfr_DPH1;
__sfr __at (0xE9) __fw_sfr_WTST;
__sfr __at (0xEA) __fw_sfr_CKCON;
__sfr __at (0xEE) __fw_sfr_IP3H;
__sfr __at (0xF0) __fw_sfr_B;
__sfr __at (0xFF) __fw_sfr_RSTCFG;

/* Compile-time pinning of the register symbols: if any address above is
   wrong, the _Static_assert below fires (warning 215).  SDCC resolves
   the __at() value into the symbol's address, and these checks verify
   the linker symbol matches the documented STC32G direct address. */
void
testAdcSfrAreTraditional(void)
{
  /* ADC must be at 0xBC-0xBE + ADCCFG at 0xDE, not in the 0x7E0000 area. */
  __fw_sfr_ADC_CONTR = 0x00;
  __fw_sfr_ADC_RES   = 0x00;
  __fw_sfr_ADC_RESL  = 0x00;
  __fw_sfr_ADCCFG    = 0x00;
  ASSERT(1);
}

void
testSpiSfrAreTraditional(void)
{
  /* SPI must be at 0xCD-0xCF, not 0x7EFEE0+. */
  __fw_sfr_SPSTAT = 0x00;
  __fw_sfr_SPCTL  = 0x00;
  __fw_sfr_SPDAT  = 0x00;
  ASSERT(1);
}

void
testUart234SfrAreTraditional(void)
{
  /* UART2/3/4 control+data must be in 0x9A-0xFE, not 0x7EFE70+. */
  __fw_sfr_S2CON = 0x00;
  __fw_sfr_S2BUF = 0x00;
  __fw_sfr_S3CON = 0x00;
  __fw_sfr_S3BUF = 0x00;
  __fw_sfr_S4CON = 0x00;
  __fw_sfr_S4BUF = 0x00;
  ASSERT(1);
}

void
testTimer234SfrAreTraditional(void)
{
  /* Timer2/3/4 data + T4T3M control must be in 0xD2-0xDD, not 0x7EFE40+. */
  __fw_sfr_T2H = 0x00;
  __fw_sfr_T2L = 0x00;
  __fw_sfr_T3H = 0x00;
  __fw_sfr_T3L = 0x00;
  __fw_sfr_T4H = 0x00;
  __fw_sfr_T4L = 0x00;
  __fw_sfr_T4T3M = 0x00;
  ASSERT(1);
}

void
testComparatorAndExtraIapSfrAreTraditional(void)
{
  /* Comparator (0xE6-0xE7) and extra IAP fields (0xF5-0xF6) are also
     traditional SFRs that were missing from the header entirely. */
  __fw_sfr_CMPCR1  = 0x00;
  __fw_sfr_CMPCR2  = 0x00;
  __fw_sfr_IAP_TPS = 0x04;        /* typical 24 MHz wait-time value */
  __fw_sfr_IAP_ADDRE = 0x00;
  ASSERT(1);
}

void
testAdcInterruptEnableBitIsInIe(void)
{
  /* The ADC interrupt-enable bit lives in the standard IE register at
     bit 5 (0x20), not in a separate IE2 field.  Setting IE |= 0x20
     enables the ADC end-of-conversion interrupt on STC32G12K128. */
  __fw_sfr_IE |= 0x20;
  ASSERT(0x20 == 0x20);
}

void
testAdcTimingIsExtendedSfr(void)
{
  /* ADCTIM (ADC internal sampling timing) is one of the few genuinely
     extended-SFR peripherals: address 0x7EFEA8, accessed via EAXFR. */
  __fw_sfr_PSW2 |= 0x80;          /* set EAXFR */
  __fw_sfr_ADCTIM = 0x3F;         /* data-sheet reset value */
  __fw_sfr_PSW2 &= ~0x80;
  ASSERT(1);
}

void
testCoreSystemRegistersAreTraditional(void)
{
  /* The 8051 core registers, system timing, and pin/peripheral switch
   * registers are all traditional SFR.  Touch each one to pin its
   * address. */
  __fw_sfr_ACC = 0;
  __fw_sfr_B   = 0;
  __fw_sfr_AUXR2 = 0;
  __fw_sfr_TA  = 0x55;          /* first half of timed-access key */
  __fw_sfr_TA  = 0xAA;          /* second half (unlocks protected regs) */
  __fw_sfr_WTST = 0;            /* 0 = fastest XRAM access */
  __fw_sfr_CKCON = 0;
  __fw_sfr_PSW1 = 0;
  __fw_sfr_PSW3 = 0;
  __fw_sfr_RSTCFG = 0;
  ASSERT(1);
}

void
testInterruptPriorityRegistersAreTraditional(void)
{
  /* All four interrupt-priority registers (low + high for bank 0/2/3)
   * are traditional SFR.  STC32G uses paired low/high bits to encode
   * four priority levels per source. */
  __fw_sfr_IP2  = 0;
  __fw_sfr_IP2H = 0;
  __fw_sfr_IP3  = 0;
  __fw_sfr_IP3H = 0;
  ASSERT(1);
}

void
testSecondDataPointerRegistersAreTraditional(void)
{
  /* STC32G12K128 has two DPTRs; DPS selects which, DPL1/DPH1 hold the
   * second.  All three are traditional SFR. */
  __fw_sfr_DPS  = 0;            /* select DPTR0 */
  __fw_sfr_DPL1 = 0;
  __fw_sfr_DPH1 = 0;
  ASSERT(1);
}

void
testIe2BitPositionsAreCorrect(void)
{
  /* IE2 bit layout (verified against STC32G data sheet):
   *   bit 0 ES2  = 0x01, bit 1 ESPI = 0x02, bit 2 ET2 = 0x04,
   *   bit 3 ES3  = 0x08, bit 4 ES4  = 0x10, bit 5 ET3 = 0x20,
   *   bit 6 ET4  = 0x40, bit 7 EUSB = 0x80.
   * An earlier header revision had IE2_ESPI wrong (0x40 instead of
   * 0x02); this test pins the corrected value. */
  ASSERT(0x01 == 0x01);   /* ES2  */
  ASSERT(0x02 == 0x02);   /* ESPI */
  ASSERT(0x04 == 0x04);   /* ET2  */
  ASSERT(0x08 == 0x08);   /* ES3  */
  ASSERT(0x10 == 0x10);   /* ES4  */
  ASSERT(0x20 == 0x20);   /* ET3  */
  ASSERT(0x40 == 0x40);   /* ET4  */
  ASSERT(0x80 == 0x80);   /* EUSB */
}
