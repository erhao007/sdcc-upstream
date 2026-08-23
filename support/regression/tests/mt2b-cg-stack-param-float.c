/* MT-2B-CG1 stack float parameter regression. */

#include <testfwk.h>

#ifdef __SDCC
#pragma std_c99
#endif

#if defined(__SDCC_mcs251)

static float
second (float a, float b)
{
  (void)a;
  return b;
}

#endif

void testTortureExecute (void)
{
#if defined(__SDCC_mcs251)
  volatile float a = second (0, 1);
  volatile float b = second (0, 2);
  ASSERT (a == 1);
  ASSERT (b == 2);
#else
  /* This is a MCS-251 stack-parameter regression; other ports retain a stub. */
  ASSERT (1);
#endif
}
