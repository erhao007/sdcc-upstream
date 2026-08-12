#!/bin/sh
# Verify that the mcs251 uCsim model delivers Timer1, INT0, and UART
# interrupts end to end.  Each test configures the peripheral, enables the
# interrupt, and signals success by writing 0x55 to P0 (0xAA on failure).
# Usage: ./verify.sh <build-dir>   (build-dir contains bin/sdcc and sim/.../ucsim_51)
set -e
B="${1:-$(cd ../../../.. && pwd)}"   # default: repo build/ dir
SDCC="$B/bin/sdcc"
SIM="$B/sim/ucsim/src/sims/s51.src/ucsim_51"
HERE=$(dirname "$0")
pass=0; fail=0
for t in timer1 int0 uart; do
  "$SDCC" -mstc32 --model-small "$HERE/$t.c" -o "/tmp/irqv_$t.ihx"
  printf 's 8000\nd sfr 0x80 0x81\nquit\n' | "$SIM" -t 251 -c - "/tmp/irqv_$t.ihx" > "/tmp/irqv_$t.out" 2>/dev/null
  if grep -q '0x55' "/tmp/irqv_$t.out"; then echo "PASS $t"; pass=$((pass+1));
  else echo "FAIL $t"; fail=$((fail+1)); fi
done
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
