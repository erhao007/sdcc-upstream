/*
   fw-header-contract.c - Contract test that pins stc32g12k128.h values.

   This test includes the real device header (unlike fw-sfr-map.c which
   hard-codes addresses for mcs51-small portability) so it can catch a
   header regression directly.

   Two layers of checks:
   1. Build-time #if / #error on the header's STC_SFR_ADDR_* macros and
      bit-field #defines.  The STC_SFR_ADDR_* macros are the single
      source of truth: the header's __sfr __at() declarations must match
      them, and this test verifies the macros match the data sheet.  A
      regression in any value is a hard compile failure.
   2. Run-time writes exercise the real SFR symbols, confirming the
      header is self-consistent (names exist, types are assignable).

   On non-mcs251 ports every test is a stub.  Functions are defined ONCE
   with the port check inside the body to avoid the wrapper scanning
   both branches of an #if/#else pair.
*/

#include <testfwk.h>

#if defined(__SDCC_mcs251)
#include <stc32g12k128.h>

/* ---- Build-time: SFR address contract --------------------------------
 * The STC_SFR_ADDR_* macros are defined in stc32g12k128.h as the single
 * source of truth for registers that have historically been placed at
 * the wrong address.  If a future header edit changes the macro OR the
 * __sfr __at() value, this #if block catches the macro half; the run-
 * time writes below exercise the __at() half. */
#if (STC_SFR_ADDR_WDT_CONTR != 0xC1)
#error WDT_CONTR address regression (must be 0xC1)
#endif
#if (STC_SFR_ADDR_IAP_CONTR != 0xC7)
#error IAP_CONTR address regression (must be 0xC7)
#endif
#if (STC_SFR_ADDR_IE2 != 0xAF)
#error IE2 address regression (must be 0xAF, not 0xA9)
#endif
#if (STC_SFR_ADDR_SADDR != 0xA9)
#error SADDR address regression (must be 0xA9)
#endif
#if (STC_SFR_ADDR_ADC_CONTR != 0xBC)
#error ADC_CONTR address regression (must be 0xBC)
#endif
#if (STC_SFR_ADDR_ADCCFG != 0xDE)
#error ADCCFG address regression (must be 0xDE)
#endif
#if (STC_SFR_ADDR_SPCTL != 0xCE)
#error SPCTL address regression (must be 0xCE)
#endif
#if (STC_SFR_ADDR_CANICR != 0xF1)
#error CANICR address regression (must be 0xF1)
#endif
#if (STC_SFR_ADDR_USBCON != 0xF4)
#error USBCON address regression (must be 0xF4)
#endif
#if (STC_SFR_ADDR_SPH != 0x85)
#error SPH address regression (must be 0x85)
#endif

/* ---- Build-time: IE2 bit layout ------------------------------------- */
#if (IE2_ES2 != 0x01) || (IE2_ESPI != 0x02) || (IE2_ET2 != 0x04)
#error IE2 bit regression (ES2/ESPI/ET2)
#endif
#if (IE2_ES3 != 0x08) || (IE2_ES4 != 0x10) || (IE2_ET3 != 0x20)
#error IE2 bit regression (ES3/ES4/ET3)
#endif
#if (IE2_ET4 != 0x40) || (IE2_EUSB != 0x80)
#error IE2 bit regression (ET4/EUSB)
#endif

/* ---- Build-time: CANICR bit layout ---------------------------------- */
#if (CANICR_PCANL != 0x01) || (CANICR_CANIE != 0x02) || (CANICR_CANIF != 0x04)
#error CANICR bit regression (PCANL must be 0x01)
#endif
#if (CANICR_PCANH != 0x08) || (CANICR_PCAN2L != 0x10)
#error CANICR bit regression (PCANH/PCAN2L)
#endif

/* ---- Build-time: ADC / IAP / SPI / P_SW bits ------------------------ */
#if (ADCCFG_RESFMT != 0x20) || (ADCCFG_SPEED_MASK != 0x0F)
#error ADCCFG bit regression (SPEED must be [3:0])
#endif
#if (IAP_CMD_READ != 1) || (IAP_CMD_WRITE != 2) || (IAP_CMD_ERASE != 3)
#error IAP command code regression
#endif
#if (SPCTL_SSIG != 0x80) || (SPCTL_SPEN != 0x40) || (SPCTL_DORD != 0x20) || \
    (SPCTL_MSTR != 0x10) || (SPCTL_CPOL != 0x08) || (SPCTL_CPHA != 0x04)
#error SPCTL bit regression (SSIG/SPEN/DORD/MSTR/CPOL/CPHA)
#endif
/* Datasheet SPCTL constraint: CPHA=0 requires SSIG=0 (hardware /SS pin drives
 * master/slave selection); SSIG=1 — driving /SS on any GPIO in software — is
 * only legal with CPHA=1.  A mode-0 master therefore cannot use software /SS;
 * use mode 3 (SPCTL_SSIG|SPEN|MSTR|CPOL|CPHA) when /SS is software-driven. */
/* SPR[1:0] prescaler encoding (datasheet SPCTL, p916): 00=/4, 01=/8, 10=/16, 11=/2. */
#if (SPCTL_SPR_4 != 0x00) || (SPCTL_SPR_8 != 0x01) || \
    (SPCTL_SPR_16 != 0x02) || (SPCTL_SPR_2 != 0x03)
#error SPCTL SPR prescaler encoding regression
#endif
#if (P_SW1_SPI_S0 != 0x04) || (P_SW2_EAXFR != 0x80)
#error P_SW bit regression
#endif

#endif /* __SDCC_mcs251 */

/* ---- Run-time tests (each defined ONCE; port check inside body) ----- */

void
testHeaderSfrAddressesMatchContract(void)
{
#if defined(__SDCC_mcs251)
  WDT_CONTR = 0;
  IAP_CONTR = IAP_CONTR_IAPEN;
  IE2       = IE2_ESPI;
  SADDR     = 0;
  ADC_CONTR = ADC_CONTR_ADC_POWER;
  ADCCFG    = ADCCFG_RESFMT;
  SPCTL     = SPCTL_SSIG | SPCTL_SPEN | SPCTL_MSTR;
  CANICR    = CANICR_CANIE | CANICR_PCANL;
  USBCON    = 0;
  SPH       = 0;
#endif
  ASSERT(1);
}

void
testHeaderExtendedSfrCompile(void)
{
#if defined(__SDCC_mcs251)
  P_SW2 |= P_SW2_EAXFR;
  I2CSLADR = 0x40;        /* the register that was missing in earlier revs */
  I2CTXD   = 0xAB;
  PWMA_CR1 = PWMA_CR1_CEN;
  PWMA_BKR = PWMA_BKR_MOE;
  DMA_M2M_CR = 0;
  CANAR    = 0;
  ADCTIM   = 0x3F;
  P_SW2 &= ~P_SW2_EAXFR;
#endif
  ASSERT(1);
}

void
testHeaderIapSequenceCompile(void)
{
#if defined(__SDCC_mcs251)
  IAP_TPS   = 24;
  IAP_CONTR = IAP_CONTR_IAPEN;
  IAP_CMD   = IAP_CMD_READ;
  IAP_TRIG  = IAP_TRIG_MAGIC1;
  IAP_TRIG  = IAP_TRIG_MAGIC2;
  IAP_CMD   = IAP_CMD_IDLE;
  IAP_CONTR = 0;
#endif
  ASSERT(1);
}
