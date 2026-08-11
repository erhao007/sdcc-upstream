/* PR tree-optimization/58984 */

struct S { int f0 : 8; int : 6; int f1 : 5; };
struct T { char f0; int : 6; int f1 : 5; };

int a, *c = &a, e, n, b, m;

#if defined(__SDCC_mcs251) && defined(__SDCC_MODEL_SMALL) && !defined(__SDCC_STACK_AUTO)
#define LOCAL_F_SIZE 1 /* Only f[0] is used; 36 far pointers do not fit DSEG. */
#else
#define LOCAL_F_SIZE 36
#endif

static int
foo (struct S p)
{
  const unsigned short *f[LOCAL_F_SIZE];
  for (; e < 2; e++)
    {
      const unsigned short **i = &f[0];
      *c ^= 1;
      if (p.f1)
	{
	  *i = 0;
	  return b;
	}
    }
  return 0;
}

static int
bar (struct T p)
{
  const unsigned short *f[LOCAL_F_SIZE];
  for (; e < 2; e++)
    {
      const unsigned short **i = &f[0];
      *c ^= 1;
      if (p.f1)
	{
	  *i = 0;
	  return b;
	}
    }
  return 0;
}

int
main ()
{
  struct S o = { 1, 1 };
  foo (o);
  m = n || o.f0;
  if (a != 1)
    __builtin_abort ();
  e = 0;
  struct T p = { 1, 1 };
  bar (p);
  m |= n || p.f0;
  if (a != 0)
    __builtin_abort ();
  return 0;
}
