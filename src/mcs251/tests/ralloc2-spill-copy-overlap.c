/* MT-1E Phase 2A spill-copy overlap fixture.

   Reconstruct a 24-bit pointer through 32-bit integer bit operations.  Under
   DATA pressure ralloc2 colours the dying 32-bit value and the narrowing
   24-bit CAST result into the same spill slot.  The generator must snapshot
   the source before the big-endian low-to-high copy overwrites an unread
   byte. */

__data __at (0x30) volatile unsigned char spill_copy_status;
static int spill_copy_value = 1;

void
main (void)
{
  int *p = &spill_copy_value;
  unsigned long encoded = (unsigned long)p;
  unsigned long rebuilt = 0;
  unsigned long bit;
  unsigned char k;

  for (k = 0; k < sizeof (encoded) * 8; ++k)
    {
      bit = (encoded & (1ul << k)) >> k;
      if (bit == 1)
        rebuilt |= 1ul << k;
    }

  {
    int *q = (int *)rebuilt;
    *q = 11;
    spill_copy_status = (*p == 11 && *q == 11) ? 0x55 : 0xe1;
  }

  for (;;)
    ;
}
