/* MT-2B-CG1 stack float call regression. */

#include <testfwk.h>

#ifdef __SDCC
#pragma std_c99
#endif

#if defined(__SDCC_mcs251)

static float
addf (float a, float b)
{
  return a + b;
}

#endif

void testTortureExecute (void)
{
#if defined(__SDCC_mcs251)
  volatile float x = 0;

  x = addf (x, 1);
  ASSERT (x == 1);
  x = addf (x, 2);
  ASSERT (x == 3);
#else
  /* This is a MCS-251 stack-call regression; other ports retain a stub. */
  ASSERT (1);
#endif
}
