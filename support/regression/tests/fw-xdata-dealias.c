/*
   fw-xdata-dealias.c - XDATA (0x010000+) vs edata-window (0x0100-0xFFFF)
   de-aliasing contract test.

   MCS-251 places XDATA at flat 0x010000-0x01FFFF (SDCC default xdata_loc),
   reached by the codegen via a 24-bit @dpx pointer with DPXL=0x01.  The
   edata window (region 00, 0x0100-0xFFFF) is reached with DPXL=0x00.  In the
   simulator, both used to fold into the same 64 KiB xram backing store via
   `addr & 0xffff`, so e.g. an XDATA cell at 0x01F000 silently aliased the
   edata cell at 0x00F000 (both = xram[0xF000]).  This test pins the
   de-aliasing: writing one must not be visible at the other.

   On non-mcs251 ports the test body is a stub (functions are defined ONCE
   with the port check inside the body so the test wrapper always finds them).

   Address choice: 0x00F000 is above any test program's code, so read_edata
   returns the xram cell there (not the von-Neumann ROM mirror of empty ROM).
*/

#include <testfwk.h>

#if defined(__SDCC_mcs251)
/* Region 01 (XDATA, DPXL=0x01) and region 00 (edata window, DPXL=0x00),
   both at offset 0xF000 so they collapse into xram[0xF000] before the
   de-aliasing backing store is added. */
__xdata __at(0x01F000) volatile unsigned char fw_xdata_cell;
__xdata __at(0x00F000) volatile unsigned char fw_edata_cell;
#endif

void
testXdataIsDecoupledFromEdataWindow(void)
{
#if defined(__SDCC_mcs251)
  fw_xdata_cell = 0x5A;
  fw_edata_cell = 0xA5;

  ASSERT(fw_xdata_cell == 0x5A);   /* XDATA write reads back */
  ASSERT(fw_edata_cell == 0xA5);   /* edata-window write reads back */
  ASSERT(fw_xdata_cell != fw_edata_cell);   /* they must not alias */
#endif
}
