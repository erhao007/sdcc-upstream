/*
   fw-header-contract.c - Contract test that pins stc32g12k128.h values.

   Unlike fw-sfr-map.c (which deliberately re-declares every register
   at a hard-coded address so it can run unchanged on mcs51-small), this
   test includes the real device header and fails to compile if any of
   the bit-field macros drift away from the STC32G data-sheet values.
   It is mcs251-only.

   The build-time checks use #if / #error on the header's #define'd
   macros, so a regression in a macro value is a hard compile failure
   rather than a silent ASSERT(1).  (SFR addresses declared with
   __sfr __at() cannot be tested by #if because they are not
   preprocessor constants; their correctness is covered indirectly by
   the run-time writes below and by the codegen-address assertions in
   fw-sfr-map.c.)
*/

#include <testfwk.h>

#if defined(__SDCC_mcs251)
#include <stc32g12k128.h>

/* ---- Build-time checks: IE2 bit layout ------------------------------ */
#if (IE2_ES2 != 0x01) || (IE2_ESPI != 0x02) || (IE2_ET2 != 0x04)
#error IE2 bit regression (ES2/ESPI/ET2)
#endif
#if (IE2_ES3 != 0x08) || (IE2_ES4 != 0x10) || (IE2_ET3 != 0x20)
#error IE2 bit regression (ES3/ES4/ET3)
#endif
#if (IE2_ET4 != 0x40) || (IE2_EUSB != 0x80)
#error IE2 bit regression (ET4/EUSB)
#endif

/* ---- Build-time checks: CANICR bit layout --------------------------- */
#if (CANICR_PCANL != 0x01) || (CANICR_CANIE != 0x02) || (CANICR_CANIF != 0x04)
#error CANICR bit regression (PCANL must be 0x01, not 0x08)
#endif
#if (CANICR_PCANH != 0x08) || (CANICR_PCAN2L != 0x10)
#error CANICR bit regression (PCANH/PCAN2L)
#endif
#if (CANICR_CAN2IE != 0x20) || (CANICR_CAN2IF != 0x40) || (CANICR_PCAN2H != 0x80)
#error CANICR bit regression (CAN2IE/CAN2IF/PCAN2H)
#endif

/* ---- Build-time checks: ADC config bits ----------------------------- */
#if (ADCCFG_RESFMT != 0x20) || (ADCCFG_SPEED_MASK != 0x0F)
#error ADCCFG bit regression (SPEED must be [3:0]=0x0F, not [7:5]=0xE0)
#endif

/* ---- Build-time checks: IAP command codes + control bits ------------ */
#if (IAP_CMD_IDLE != 0) || (IAP_CMD_READ != 1) || (IAP_CMD_WRITE != 2) || (IAP_CMD_ERASE != 3)
#error IAP command code regression
#endif
#if (IAP_CONTR_IAPEN != 0x80) || (IAP_TRIG_MAGIC1 != 0x5A) || (IAP_TRIG_MAGIC2 != 0xA5)
#error IAP_CONTR/TRIG macro regression
#endif

/* ---- Build-time checks: SPI control bits ---------------------------- */
#if (SPCTL_SSIG != 0x80) || (SPCTL_SPEN != 0x40) || (SPCTL_MSTR != 0x10)
#error SPCTL bit regression
#endif
#if (SPCTL_CPOL != 0x08) || (SPCTL_CPHA != 0x04)
#error SPCTL CPOL/CPHA regression
#endif

/* ---- Build-time checks: P_SW1/P_SW2 pin-select bits ----------------- */
#if (P_SW1_SPI_S0 != 0x04) || (P_SW1_SPI_S1 != 0x08)
#error P_SW1 SPI pin-select bit regression
#endif
#if (P_SW2_EAXFR != 0x80)
#error P_SW2 EAXFR bit regression
#endif

/* ---- Build-time checks: TCON/IE bit masks --------------------------- */
#if (TCON_IT0 != 0x01) || (TCON_TR0 != 0x10) || (TCON_TF0 != 0x20)
#error TCON bit regression
#endif
#if (IE_EX0 != 0x01) || (IE_ET0 != 0x02) || (IE_EA != 0x80) || (IE_EADC != 0x20)
#error IE bit regression
#endif

/* ---- Build-time checks: PWM key bits -------------------------------- */
#if (PWMA_CR1_CEN != 0x01) || (PWMA_BKR_MOE != 0x80)
#error PWMA bit regression
#endif

/* ---- Run-time checks: exercise the real SFR symbols ----------------- */
void
testHeaderTraditionalSfrCompile(void)
{
  /* Each write compiles only if the header declares the symbol.
   * The codegen address is verified separately by fw-sfr-map.c; here
   * we confirm the header is self-consistent (names exist, types are
   * assignable). */
  WDT_CONTR = 0;
  IAP_CONTR = IAP_CONTR_IAPEN;
  IAP_TPS   = 24;
  ADC_CONTR = ADC_CONTR_ADC_POWER;
  ADCCFG    = ADCCFG_RESFMT | 0x01;
  SPCTL     = SPCTL_SSIG | SPCTL_SPEN | SPCTL_MSTR;
  SADDR     = 0;
  IE2       = IE2_ESPI;
  CANICR    = CANICR_CANIE | CANICR_PCANL;
  USBCON    = 0;
  SPH       = 0;
  ASSERT(1);
}

void
testHeaderExtendedSfrCompile(void)
{
  /* Extended SFRs require EAXFR; this confirms the header declares
   * the corrected I2C layout (I2CSLADR was previously missing) and
   * the advanced-peripheral symbols. */
  P_SW2 |= P_SW2_EAXFR;
  I2CSLADR = 0x40;        /* the register that was missing before */
  I2CTXD   = 0xAB;
  PWMA_CR1 = PWMA_CR1_CEN;
  PWMA_BKR = PWMA_BKR_MOE;
  DMA_M2M_CR = 0;
  CANAR    = 0;
  ADCTIM   = 0x3F;
  P_SW2 &= ~P_SW2_EAXFR;
  ASSERT(1);
}

void
testHeaderIapTriggerSequence(void)
{
  /* The corrected IAP_CONTR layout (no wait-time in low bits) plus
   * IAP_TPS: write the setup, clear IAPEN at the end. */
  IAP_TPS   = 24;
  IAP_CONTR = IAP_CONTR_IAPEN;
  IAP_CMD   = IAP_CMD_READ;
  IAP_ADDRH = 0;
  IAP_ADDRL = 0;
  IAP_TRIG  = IAP_TRIG_MAGIC1;
  IAP_TRIG  = IAP_TRIG_MAGIC2;
  IAP_CMD   = IAP_CMD_IDLE;
  IAP_CONTR = 0;
  ASSERT(1);
}

/* Also defined on mcs251 so the auto-generated wrapper (which scans
 * for every test* function across all ports) can call it.  On mcs251
 * it is a trivial pass; on other ports it is the only test body. */
void
testHeaderContractNotApplicable(void)
{
  ASSERT(1);
}

#else
/* On non-mcs251 ports there is no stc32g12k128.h, so every test is a
 * no-op.  We still define stubs for the names the auto-generated
 * wrapper will call (the wrapper scans for test* across all ports). */
void testHeaderTraditionalSfrCompile(void) { ASSERT(1); }
void testHeaderExtendedSfrCompile(void)    { ASSERT(1); }
void testHeaderIapTriggerSequence(void)    { ASSERT(1); }
void testHeaderContractNotApplicable(void) { ASSERT(1); }
#endif
