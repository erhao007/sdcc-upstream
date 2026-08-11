/*
   fw-eaxfr.c - EAXFR (extended SFR, 0x7E0000+) backing-store contract test.

   On MCS-251 the extended peripherals (I2C, PWM, DMA, CAN, ...) are declared
   in stc32g12k128.h as __xdata __at(EAXFR_BASE + 0xXXXX) and accessed through
   a 24-bit @dpx pointer (DPXL=0x7E), which the simulator routes through
   write_edata()/read_edata().  Before the EAXFR backing store existed, the
   simulator folded every 0x7E.... address into xram[addr & 0xffff], so an
   EAXFR register silently aliased the xram cell at the same low offset
   (e.g. I2CCFG @ 0x7EFE80 collided with xram[0xFE80]).  This test pins the
   de-aliasing: writing I2CCFG must not be visible at the xram-offset mirror,
   and the EAXFR write must read back.

   Uses the real device header (I2CCFG, P_SW2, P_SW2_EAXFR).  On non-mcs251
   ports the test body is a stub (functions are defined ONCE with the port
   check inside the body so the test wrapper always finds them).

   Note: P_SW2.EAXFR (bit 7 of SFR 0xBA) gates EAXFR access on real hardware.
   The simulator does not enforce this gate (it routes 0x7E0000+ unconditionally;
   disabled-state behaviour is unverified).  The test sets the bit anyway to
   match real firmware practice.
*/

#include <testfwk.h>

#if defined(__SDCC_mcs251)
#include <stc32g12k128.h>

/* The xram-offset mirror of I2CCFG (0x7EFE80): flat 0x00FE80 lands in
   xram[0xFE80] via the edata window, exactly the cell that used to alias
   the EAXFR register.  Declared __xdata so the codegen reaches it through
   the same @dpx path. */
__xdata __at(0x00FE80) volatile unsigned char fw_eaxfr_low_mirror;
#endif

void
testEaxfrIsDecoupledFromXram(void)
{
#if defined(__SDCC_mcs251)
  P_SW2 |= P_SW2_EAXFR;                 /* enable EAXFR access (real-hw practice) */

  fw_eaxfr_low_mirror = 0x00;
  I2CCFG = 0x5A;                        /* EAXFR register @ 0x7EFE80 */

  ASSERT(I2CCFG == 0x5A);               /* EAXFR write reads back */
  ASSERT(fw_eaxfr_low_mirror == 0x00);  /* did NOT alias into xram[0xFE80] */

  P_SW2 &= (unsigned char)~P_SW2_EAXFR;
#endif
}
