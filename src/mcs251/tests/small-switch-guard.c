/* Exercises genJumpTab's small-table path (count <= 7), which emits the
   provisional JMP @A+DPTR region guard.  The build must assemble (-c):
   the guard's slow path reconstructs the target through DPL/DPH/DPXL +
   "mov dr28,dpx" — R24-R27 are not byte-addressable and any direct use
   fails assembly. */
unsigned char
mcs251_small_switch (unsigned char selector)
{
  switch (selector)
    {
    case 0: return 10;
    case 1: return 21;
    case 2: return 32;
    case 3: return 43;
    case 4: return 54;
    default: return 99;
    }
}
