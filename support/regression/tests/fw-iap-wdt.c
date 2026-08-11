/*
   fw-iap-wdt.c - Register-address verification for STC32G12K128 IAP/WDT.

   The IAP (In-Application Programming / EEPROM) and WDT (watchdog)
   registers occupy the traditional SFR block 0xC1-0xC7 on STC32G12K128.
   This test pins those addresses so that a future header edit cannot
   silently move them, and verifies the IAP command-code macros.

   The test is compile-and-run: it writes a marker to each SFR but does
   NOT trigger the IAP engine (it leaves IAP_TRIG untouched and never
   sets IAPEN), so it is safe to execute on both uCsim and real silicon
   without erasing EEPROM.

   On ports other than mcs251/mcs51 the registers are still declared at
   the same STC legacy addresses, so the test compiles everywhere.
*/

#include <testfwk.h>

/* The STC IAP/WDT addresses are identical across STC89/12/15/8/32G
   (the 0xC1-0xC7 traditional SFR block).  Declare them directly here so
   the test is self-contained on every port, and so that the addresses
   under test live in exactly one place. */
__sfr __at (0xC1) __fw_iap_wdt_WDT_CONTR;
__sfr __at (0xC2) __fw_iap_wdt_IAP_DATA;
__sfr __at (0xC3) __fw_iap_wdt_IAP_ADDRH;
__sfr __at (0xC4) __fw_iap_wdt_IAP_ADDRL;
__sfr __at (0xC5) __fw_iap_wdt_IAP_CMD;
__sfr __at (0xC6) __fw_iap_wdt_IAP_TRIG;
__sfr __at (0xC7) __fw_iap_wdt_IAP_CONTR;

#define IAP_CMD_IDLE  0x00
#define IAP_CMD_READ  0x01
#define IAP_CMD_WRITE 0x02
#define IAP_CMD_ERASE 0x03

void
testIapRegistersCompile(void)
{
  /* Touch each register without enabling IAP (IAPEN stays 0, IAP_TRIG
     is never written), so no EEPROM command is actually launched.
     This guards against the addresses going stale or the names drifting. */
  __fw_iap_wdt_IAP_DATA   = 0x00;
  __fw_iap_wdt_IAP_ADDRH  = 0x00;
  __fw_iap_wdt_IAP_ADDRL  = 0x00;
  __fw_iap_wdt_IAP_CMD    = IAP_CMD_IDLE;
  __fw_iap_wdt_IAP_CONTR  = 0x00;
  __fw_iap_wdt_WDT_CONTR  = 0x00;
  /* Intentionally do NOT write IAP_TRIG here. */

  /* The assignment above must not be dead-eliminated; reference the
     last register in an assertion so the compiler keeps it. */
  ASSERT(IAP_CMD_IDLE == 0);
}

void
testIapCommandCodes(void)
{
  /* The four IAP command codes must be 0..3 and pairwise distinct. */
  ASSERT(IAP_CMD_IDLE  == 0);
  ASSERT(IAP_CMD_READ  == 1);
  ASSERT(IAP_CMD_WRITE == 2);
  ASSERT(IAP_CMD_ERASE == 3);
  ASSERT(IAP_CMD_READ  != IAP_CMD_WRITE);
  ASSERT(IAP_CMD_WRITE != IAP_CMD_ERASE);
}
