/* MT-1E Phase 2A retained-fallback fixture.

   Each named helper carries one formerly guarded iCode shape.  The directed
   gate checks production/forced-ralloc2 identity and then rebuilds ralloc2
   with one historical fallback restored at a time; the corresponding route
   marker must disappear. */

__data __at (0x30) volatile unsigned char p2a_status;
__xdata volatile unsigned char p2a_xbuf[4] = {0x11, 0x22, 0x33, 0x44};
__pdata volatile unsigned char p2a_pbuf[4];
__xdata unsigned char * volatile p2a_source0;
__xdata unsigned char * volatile p2a_source1;
__xdata unsigned char * volatile p2a_source2;
__xdata unsigned char * volatile p2a_source3;
__xdata unsigned char * volatile p2a_source4;
__xdata unsigned char * volatile p2a_source5;

static unsigned char
p2a_control_path (unsigned char n)
{
  return n + 1;
}

static unsigned char
p2a_switch_path (unsigned char n)
{
  switch (n)
    {
    case 0: return 1;
    case 1: return 3;
    case 2: return 5;
    case 3: return 7;
    case 4: return 9;
    case 5: return 11;
    case 6: return 13;
    case 7: return 15;
    case 8: return 17;
    case 9: return 19;
    default: return 0;
    }
}

static unsigned char
p2a_stack_address_path (unsigned char n) __reentrant
{
  unsigned char local[3];
  unsigned char *p = local;

  local[0] = n;
  local[1] = n + 1;
  local[2] = n + 2;
  return p[0] + p[1] + p[2];
}

static unsigned char
p2a_generic_cast_path (__xdata unsigned char *p)
{
  void *generic = (void *)p;
  return *((__xdata unsigned char *)generic);
}

static unsigned char
p2a_pointer_compare_path (__xdata unsigned char *a,
                          __xdata unsigned char *b)
{
  return a < b && a != b;
}

/* Keep six pointer temporaries live through one comparison.  This fixes the
   historical double-spill shape where both comparison operands could be
   materialised through an invalid mixed register/stack address. */
static unsigned char
p2a_pointer_compare_pressure_path (void)
{
  __xdata unsigned char *p0 = p2a_source0;
  __xdata unsigned char *p1 = p2a_source1;
  __xdata unsigned char *p2 = p2a_source2;
  __xdata unsigned char *p3 = p2a_source3;
  __xdata unsigned char *p4 = p2a_source4;
  __xdata unsigned char *p5 = p2a_source5;
  unsigned char compared = p4 < p5 && p4 != p5;
  unsigned char total = *p0 + *p1 + *p2 + *p3 + *p4 + *p5;

  return compared && total == 0xee;
}

static unsigned char
p2a_pdata_path (unsigned char index)
{
  p2a_pbuf[index] = p2a_xbuf[index];
  return p2a_pbuf[index];
}

/* Keep five generic pointers live through a mixed-width expression.  The
   original gcc-torture mode-dependent-address shape exposed ralloc2 placing
   a 24-bit pointer across R0/R1 and R8..R15, a layout that generic-pointer
   lowering cannot materialise correctly.  The expected result is i with bit
   2 cleared, independently simple enough to make every bad pointer visible. */
static __xdata signed char p2a_layout_result[96];
static __xdata int p2a_layout_arg1[96];
static __xdata unsigned long p2a_layout_arg2[96];
static __xdata unsigned long long p2a_layout_arg3[96];
static __xdata unsigned char p2a_layout_arg4[96];
static volatile __xdata unsigned int p2a_carry_seed[7] = {
  1, 2, 3, 4, 5, 6, 7
};

static void
p2a_pointer_layout_path (signed char *result,
                         int * restrict arg1,
                         unsigned long * restrict arg2,
                         unsigned long long * restrict arg3,
                         unsigned char * restrict arg4)
{
  int index;

  for (index = 0; index < 96; ++index)
    result[index] = (((((((((((-27 + 2 + 1) >> 1) || arg4[index]) <
                         arg1[index]) ?
                        (((-27 + 2 + 1) >> 1) || arg4[index]) :
                        arg1[index]) >> (arg2[index] & 31)) ^ 1) - -32) >>
                      7) | -5) & arg3[index]);
}

/* Seven live words force the fifth input into R8..R15.  Its high byte is
   then added to the zero high byte of 0x20.  Production must keep this carry
   propagation in the encodable A:B form; the test-only guard-disabled build
   is required to fail assembly rather than emit ADDC A,R8..R15. */
static unsigned int
p2a_fixed_carry_path (void)
{
  unsigned int a0 = p2a_carry_seed[0];
  unsigned int a1 = p2a_carry_seed[1];
  unsigned int a2 = p2a_carry_seed[2];
  unsigned int a3 = p2a_carry_seed[3];
  unsigned int a4 = p2a_carry_seed[4];
  unsigned int a5 = p2a_carry_seed[5];
  unsigned int a6 = p2a_carry_seed[6];

  return (unsigned int)((a4 + 0x20) ^ a0 ^ a1 ^ a2 ^ a3 ^ a5 ^ a6);
}

void
main (void)
{
  int index;

  __asm
    nop
  __endasm;

  p2a_source0 = (__xdata unsigned char *)&p2a_xbuf[0];
  p2a_source1 = (__xdata unsigned char *)&p2a_xbuf[1];
  p2a_source2 = (__xdata unsigned char *)&p2a_xbuf[2];
  p2a_source3 = (__xdata unsigned char *)&p2a_xbuf[3];
  p2a_source4 = (__xdata unsigned char *)&p2a_xbuf[0];
  p2a_source5 = (__xdata unsigned char *)&p2a_xbuf[2];

  /* Check every formerly guarded shape independently so two wrong results
     cannot cancel in an aggregate checksum. */
  if (p2a_control_path (0x41) != 0x42)
    p2a_status = 0xe0;
  else if (p2a_switch_path (7) != 15)
    p2a_status = 0xe1;
  else if (p2a_stack_address_path (4) != 15)
    p2a_status = 0xe2;
  else if (p2a_generic_cast_path ((__xdata unsigned char *)&p2a_xbuf[1]) !=
           0x22)
    p2a_status = 0xe3;
  else if (!p2a_pointer_compare_path (
             (__xdata unsigned char *)&p2a_xbuf[0],
             (__xdata unsigned char *)&p2a_xbuf[2]))
    p2a_status = 0xe4;
  else if (!p2a_pointer_compare_pressure_path ())
    p2a_status = 0xe8;
  else if (p2a_pdata_path (3) != 0x44)
    p2a_status = 0xe5;
  else if (p2a_fixed_carry_path () != 0x0020)
    p2a_status = 0xe6;
  else
    {
      for (index = 0; index < 96; ++index)
        p2a_layout_arg3[index] = p2a_layout_arg2[index] =
          p2a_layout_arg1[index] = p2a_layout_arg4[index] = index;
      p2a_pointer_layout_path (p2a_layout_result, p2a_layout_arg1,
                               p2a_layout_arg2, p2a_layout_arg3,
                               p2a_layout_arg4);
      for (index = 0; index < 96; ++index)
        if (p2a_layout_result[index] != (signed char)(index & 0xfb))
          {
            p2a_status = 0xe7;
            break;
          }
      if (index == 96)
        p2a_status = 0x55;
    }
  for (;;)
    ;
}
