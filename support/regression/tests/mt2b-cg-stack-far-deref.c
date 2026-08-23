/* MT-2B-CG1 stack-object pointer dereference regression. */

#include <testfwk.h>

#ifdef __SDCC
#pragma std_c99
#endif

#if defined(__SDCC_mcs251)

static float
readf (float x)
{
  float _AUTOMEM *p = &x;
  return *p;
}

static unsigned long
readl (unsigned long x)
{
  unsigned long _AUTOMEM *p = &x;
  return *p;
}

static unsigned char
read_low_byte (unsigned long x)
{
  unsigned char _AUTOMEM *p = (unsigned char _AUTOMEM *)&x;
  return p[3];
}

static float
writef (float x)
{
  float _AUTOMEM *p = &x;
  *p = 2;
  return x;
}

static unsigned long
writel (unsigned long x)
{
  unsigned long _AUTOMEM *p = &x;
  *p = 0x89abcdeful;
  return x;
}

static unsigned long
write_low_byte (unsigned long x)
{
  unsigned char _AUTOMEM *p = (unsigned char _AUTOMEM *)&x;
  p[3] = 0xab;
  return x;
}

#endif

void testTortureExecute (void)
{
#if defined(__SDCC_mcs251)
  volatile float f = readf (1);
  volatile unsigned long l = readl (0x12345678ul);
  volatile unsigned char low = read_low_byte (0x12345678ul);
  volatile float fw = writef (0);
  volatile unsigned long lw = writel (0);
  volatile unsigned long loww = write_low_byte (0x12345678ul);

  ASSERT (f == 1);
  ASSERT (l == 0x12345678ul);
  ASSERT (low == 0x78);
  ASSERT (fw == 2);
  ASSERT (lw == 0x89abcdeful);
  ASSERT (loww == 0x123456abul);
#else
  /* This is a MCS-251 SPX/far-pointer regression.  Keep a single test
     entry in the shared harness, but do not execute it on other ports. */
  ASSERT (1);
#endif
}
