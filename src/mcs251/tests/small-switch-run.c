/* Runtime counterpart of the genJumpTab region guard (ST-1): the
   small-table fast path dispatches via JMP @A+DPTR when the table is in
   region 0 and through the DPXL!=0 slow path (MOVC rebuild + EJMP @DR28)
   otherwise.  check-small-switch-run.py builds this source twice —
   default code location and --code-loc 0x010000 (the uCsim 251 model
   has ROM only up to 0x01ffff, so region FF: cannot be simulated; the
   guard triggers for any nonzero DPXL, region 01 included) — runs both
   images in uCsim and asserts every switch arm and the default arm
   produce identical, correct bytes.

   Output equivalence alone cannot prove WHICH path executed (uCsim's
   high-region JMP @A+DPTR also dispatches correctly), so the program
   self-reports the executed path: after the dispatching calls, the
   inline "mov 0x40,dr28" snapshots DR28 to IRAM 0x40..0x43.  DR28 is
   written only by the slow path's "mov dr28,dpx"; a correct region-01
   run leaves the region byte (IRAM 0x41) at 0x01, while a mutated
   build that always takes the fast path leaves all four bytes zero —
   and the region-0 build must stay all-zero (fast path taken).

   MOVC/JMP high-region semantics stay provisional until the FE:/FF:
   board verdict; this check proves E2 (simulator) behaviour only. */
volatile __data __at (0x30) unsigned char results[8];

static unsigned char
classify (unsigned char s)
{
  switch (s)
    {
    case 0: return 0x11;
    case 1: return 0x22;
    case 2: return 0x44;
    case 3: return 0x88;
    default: return 0xee;
    }
}

void
main (void)
{
  unsigned char i;
  for (i = 0; i < 6; i++)
    results[i] = classify (i);
  results[6] = classify (200);   /* default arm (no table dispatch) */
  __asm
    mov 0x40, dr28               ; DR28 snapshot: slow-path sentinel
    __endasm;
  results[7] = 0xa5;             /* completion marker */
  for (;;)
    ;
}
