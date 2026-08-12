/** Test framework support functions.
 */
#include <testfwk.h>
#ifndef NO_VARARGS
#include <stdarg.h>
#endif

#ifdef __SDCC_ds390
#include <tinibios.h> /* main() must see the ISR declarations */
#endif

#ifdef __SDCC_mcs51
/* until changed, isr's must have a prototype in the module containing main */
void T2_isr (void) __interrupt (5);
#define MEMSPACE_BUF __idata
#elif defined(__SDCC_mcs251)
/* Keep the ISR prototypes in the module containing main so the MCS-251
   vector table generates correct ejmp entries (not self-jumps) for every
   interrupt source the framework provides.  The actual ISR bodies live in
   the port framework (T0_isr.c etc.) linked via fwk.lib; these prototypes
   only ensure the vector table is emitted. */
void T0_isr   (void) __interrupt (1);   /* Timer0  → 0x000B */
void T1_isr   (void) __interrupt (3);   /* Timer1  → 0x001B */
void INT0_isr (void) __interrupt (0);   /* INT0    → 0x0003 */
void uart1_isr(void) __interrupt (4);   /* UART    → 0x0023 */
/* MCS-251 has only 128 bytes of directly-addressable RAM (DSEG), like mcs51.
   Place the print buffers in XDATA (on-chip xRAM) so they neither consume
   scarce direct-addressing space nor crowd the ISEG/stack window in IRAM. */
#define MEMSPACE_BUF __xdata
#else
#define MEMSPACE_BUF
#endif

extern void _putchar(char c);
extern void _initEmu(void);
extern void _exitEmu(void);

int __numTests = 0;
static int __numFailures = 0;

#define __div(num, denom) ((num) / (denom))
#define __mod(num, denom) ((num) % (denom))

void
__prints (TESTFWK_CODE const char *s)
{
  char c;

  while ('\0' != (c = *s))
    {
      _putchar(c);
      ++s;
    }
}

/* Numeric formatting uses temporary buffers in XDATA on MCS-251.  Keep
   this helper generic; __prints is deliberately CODE-qualified so the
   framework's literal strings use MOVC in the large model. */
void
__prints_data (const char *s)
{
  char c;

  while ('\0' != (c = *s))
    {
      _putchar(c);
      ++s;
    }
}

#ifdef TARGET_VERY_LOW_MEMORY
static void
__printNibble (unsigned char c)
{
  c &= 0x0F;
  if (c <= 9 )
    c += '0';
  else
    c += 'A' - 10;
  _putchar(c);
}
void
__printu (unsigned int n)
{
  unsigned char chr;
  #define SWAP_BYTE(x)  ((x) >> 4 | (x) << 4)

  _putchar('x');

  // This seems to be the most efficient way to do it for PDK (both in RAM & ROM)
  chr = n >> 8;
  chr = SWAP_BYTE(chr);
  __printNibble(chr);
  chr = SWAP_BYTE(chr);
  __printNibble(chr);

  chr = n & 0xFF;
  chr = SWAP_BYTE(chr);
  __printNibble(chr);
  chr = SWAP_BYTE(chr);
  __printNibble(chr);
}
#else

#if defined(__SDCC_mcs51) || defined(__SDCC_mcs251) // This function is unused, but see bugs #3864.
void
__printd (int n)
{
  if (0 == n)
    {
      _putchar('0');
    }
  else
    {
      static char MEMSPACE_BUF buf[6];
      char MEMSPACE_BUF *p = &buf[sizeof (buf) - 1];
      char neg = 0;

      buf[sizeof(buf) - 1] = '\0';

      if (0 > n)
        {
          n = -n;
          neg = 1;
        }

      while (0 != n)
        {
          *--p = '0' + __mod (n, 10);
          n = __div (n, 10);
        }

      if (neg)
        _putchar('-');

      __prints_data(p);
    }
}
#endif

void
__printu (unsigned int n)
{
  if (0 == n)
    {
      _putchar('0');
    }
  else
    {
      static char MEMSPACE_BUF buf[6];
      char MEMSPACE_BUF *p = &buf[sizeof (buf) - 1];

      buf[sizeof(buf) - 1] = '\0';

      while (0 != n)
        {
          *--p = '0' + __mod (n, 10);
          n = __div (n, 10);
        }

      __prints_data(p);
    }
}
#endif

#ifndef NO_VARARGS
void
__printf (TESTFWK_CODE const char *szFormat, ...)
{
  va_list ap;
  va_start (ap, szFormat);

  while (*szFormat)
    {
      if (*szFormat == '%')
        {
          switch (*++szFormat)
            {
            case 's':
              {
                TESTFWK_CODE const char *sz = va_arg (ap, TESTFWK_CODE const char *);
                __prints(sz);
                break;
              }

            case 'd':
              {
                int i = va_arg (ap, int);
                __printd (i);
                break;
             }

            case 'u':
              {
                unsigned int i = va_arg (ap, unsigned int);
                __printu (i);
                break;
             }

           case '%':
             _putchar ('%');
             break;

           default:
             break;
          }
      }
    else
      {
        _putchar (*szFormat);
      }
    szFormat++;
  }
  va_end (ap);
}

void
__fail (TESTFWK_CODE const char *szMsg, TESTFWK_CODE const char *szCond, TESTFWK_CODE const char *szFile, int line) __reentrant
{
  __printf("--- FAIL: \"%s\" on %s at %s:%u\n", szMsg, szCond, szFile, line);
  __numFailures++;
}

int
main (void)
{
  _initEmu();

  __printf("--- Running: %s\n", __getSuiteName());

  __runSuite();

  __printf("--- Summary: %u/%u/%u: %u failed of %u tests in %u cases.\n",
     __numFailures, __numTests, __numCases,
     __numFailures, __numTests, __numCases
     );

  _exitEmu();

  return 0;
}
#else
void
__fail (TESTFWK_CODE const char *szMsg, TESTFWK_CODE const char *szCond, TESTFWK_CODE const char *szFile, int line) __reentrant
{
  __prints("--- FAIL: \"");
  __prints(szMsg);
  __prints("\" on ");
  __prints(szCond);
  __prints(" at ");
  __prints(szFile);
  _putchar(':');
  __printu(line);
  _putchar('\n');

  __numFailures++;
}

int
main (void)
{
  _initEmu();

  __prints("--- Running: ");
  __prints(__getSuiteName());
  _putchar('\n');

  __runSuite();

  __prints("--- Summary: ");
  __printu(__numFailures);
  _putchar('/');
  __printu(__numTests);
  _putchar('/');
  __printu(__numCases);

  #ifndef TARGET_VERY_LOW_MEMORY
  __prints(": ");
  __printu(__numFailures);
  __prints(" failed of ");
  __printu(__numTests);
  __prints(" tests in ");
  __printu(__numCases);
  __prints(" cases.\n");
  #else
  _putchar('\n');
  #endif

  _exitEmu();

  return 0;
}
#endif
