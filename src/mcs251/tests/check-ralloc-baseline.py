#!/usr/bin/env python3
"""MT-1A legacy register-allocator baseline gate (behavioural + tripwires).

ralloc-baseline.c is executed in uCsim under model-small, model-large
and stack-auto.  Its own ASSERTions are the oracle: they were derived
from an independent host calculation, so any allocator change that is
not a correct re-allocation fails them.

The assembly-level checks below are TRIPWIRES on legacy allocator
invariants, not ABI requirements:

  T1  exactly three native unsigned 16x16->32 multiplies are emitted
      ("mul wr12,wr8"): the native path must stay reachable;
  T2  r10/r11 (B/ACC aliases) and their wr10/dr8 tuples are never
      allocated as operands;
  T3  the 16-word pressure scope produces static spill slots
      ("_spill_pressure_slocN_") - direct OSEG DATA in model-small,
      far (dptr,#) XDATA-class access in model-large; stack-auto
      spills go through SPX and need no static slot;
  T4  the non-leaf ISR prologue saves the raw temporaries dr20/dr24/
      dr28 and returns via reti.

A future allocator may pick different registers and still pass; the
tripwires only fire when a documented legacy invariant is violated.
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

MODELS = [
    ("model-small", ["--model-small"]),
    ("model-large", ["--model-large"]),
    ("stack-auto", ["--stack-auto"]),
]

FORBIDDEN_OPERANDS = re.compile(
    r"^(?![ \t]*;)[ \t]*\S+[ \t]+[^;]*?"
    r"(?<![\w.])(?:ar1[01]|r1[01]|wr10|dr8)(?![\w.])[^;]*$",
    re.I | re.M)

STATUS_AT = 0x30
STEPS = 1000000
# Observed exit statuses of uCsim for this console shape: 0/4 when the
# program parks in its PASS self-jump (or the console ends earlier), 19
# when it parks in an assertion-FAILURE loop.  Both were verified
# deterministic over repeated runs; the program's control-block verdict
# is authoritative and the exit code only cross-checks a PASS verdict.
UCsim_NORMAL_EXIT_CODES = (4,)


def build(sdcc, source, out_base, model_flags):
    asm_path = out_base.with_suffix(".asm")
    ihx_path = out_base.with_suffix(".ihx")
    for extra in (["-S"], []):
        out = asm_path if extra else ihx_path
        command = [
            str(sdcc), "-mmcs251", "--std=gnu17", "--data-loc", "0x38",
            *model_flags, *extra, "-o", str(out), str(source),
        ]
        completed = subprocess.run(
            command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, errors="replace",
        )
        if completed.returncode:
            raise AssertionError(
                f"build failed ({completed.returncode}): "
                f"{' '.join(command)}\n{completed.stdout}")
    return asm_path.read_text(), ihx_path


def check_asm_tripwires(model, asm):
    # T1: the three native multiplies must be present
    n_mul = len(re.findall(r"^[ \t]*mul[ \t]+wr12,wr8[ \t]*$",
                           asm, re.I | re.M))
    if n_mul != 3:
        raise AssertionError(
            f"{model}: expected 3 native 'mul wr12,wr8' emissions, "
            f"found {n_mul} - native 16x16->32 path unreachable")

    # T2: B/ACC byte aliases and their tuples stay unallocated
    hits = FORBIDDEN_OPERANDS.findall(asm)
    if hits:
        raise AssertionError(
            f"{model}: allocator used forbidden operand(s) "
            f"r10/r11/wr10/dr8: {hits[:3]}")

    # T3: static spill slots for the 16-word pressure scope
    if model != "stack-auto":
        if not re.search(r"^_spill_pressure_sloc[0-9]+_[0-9]+_[0-9]+:",
                         asm, re.M):
            raise AssertionError(
                f"{model}: no '_spill_pressure_slocN_' spill slot emitted "
                "- 16 simultaneously live words cannot fit the pool")
        if model == "model-small":
            if not re.search(
                    r"^[ \t]*mov[ \t]+\(_spill_pressure_sloc[0-9]+"
                    r"_[0-9]+_[0-9]+[ \t]*\+[ \t]*1\),a[ \t]*$",
                    asm, re.I | re.M):
                raise AssertionError(
                    "model-small: spill slot not accessed as direct DATA")
        else:
            if not re.search(
                    r"^[ \t]*mov[ \t]+dptr,#\(_spill_pressure_sloc",
                    asm, re.I | re.M):
                raise AssertionError(
                    "model-large: spill slot not accessed as far XDATA "
                    "(forced-DATA spill regression)")

    # T4: ISR raw-temporary saves and RETI, scoped to _isr_pressure so
    # pushes/pops from any other ISR or vector code cannot satisfy the
    # tripwire
    m_isr = re.search(
        r"^_isr_pressure:[ \t]*\n(.*?)"
        r"(?=^[ \t]*\.area|^_[A-Za-z0-9_]+:|\Z)",
        asm, re.S | re.M)
    if not m_isr:
        raise AssertionError(
            f"{model}: cannot scope the _isr_pressure function body")
    isr_body = m_isr.group(1)
    for reg in ("dr20", "dr24", "dr28"):
        if not re.search(rf"^[ \t]*push[ \t]+{reg}[ \t]*$",
                         isr_body, re.I | re.M):
            raise AssertionError(
                f"{model}: _isr_pressure prologue does not save raw "
                f"temporary {reg}")
    isr_pops = [f"dr{m.group(1)}"
                for m in re.finditer(
                    r"^[ \t]*pop[ \t]+dr(20|24|28)[ \t]*$",
                    isr_body, re.I | re.M)]
    if isr_pops != ["dr28", "dr24", "dr20"]:
        raise AssertionError(
            f"{model}: _isr_pressure must restore dr28,dr24,dr20 in "
            f"strict reverse order, saw {isr_pops}")
    isr_lines = [ln.strip() for ln in isr_body.splitlines()
                 if ln.strip() and not ln.strip().startswith(";")]
    if not isr_lines or not re.match(r"^reti\b", isr_lines[-1], re.I):
        raise AssertionError(
            f"{model}: _isr_pressure does not end at reti "
            f"(last line: {isr_lines[-1] if isr_lines else '<empty>'})")


def run_in_ucsim(s51, workspace, ihx_name):
    console = (
        "set error unknown_code on\n"
        "set opt selfjump_stop 0\n"
        f"step {STEPS} vclk\n"
        f"dump iram {STATUS_AT:#04x} {STATUS_AT + 7:#04x}\n"
        "quit\n"
    )
    completed = subprocess.run(
        [str(s51), "-t251", "-c", "-", "-m", "-S",
         f"in={os.devnull},out=-", ihx_name],
        input=console, cwd=workspace,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, errors="replace", timeout=120,
    )
    if completed.returncode < 0:
        raise AssertionError(
            f"{ihx_name}: uCsim was killed by signal "
            f"{-completed.returncode}; simulator output cannot be trusted")

    out = re.sub(r"\x1b\[[0-9;]*[a-zA-Z]|\x1b\[?[0-9]*[a-zA-Z]",
                 "", completed.stdout)
    if "unknown instruction code" in out or "Invalid instruction" in out:
        raise AssertionError(f"{ihx_name}: simulator hit an unknown opcode")
    m = re.search(rf"^[ \t]*0x{STATUS_AT:02x}[ \t]+"
                  r"((?:[0-9a-fA-F]{2}[ \t]+)+)",
                  out, re.M)
    if not m:
        raise AssertionError(
            f"{ihx_name}: control block not dumped "
            f"(exit {completed.returncode}, snippet: {out[-300:]})")
    status = int(m.group(1).split()[0], 16)
    line = None
    if len(m.group(1).split()) >= 4:
        line = (int(m.group(1).split()[2], 16) << 8) | \
            int(m.group(1).split()[3], 16)

    # The program's own verdict is authoritative when it is present and
    # parseable.  uCsim's exit status only cross-checks it: a passing
    # park exits 0 or 4 on this build, while a program parked in its
    # assertion-failure loop exits 19 (both verified deterministic over
    # repeated runs).  Anything else is treated as untrustworthy.
    if status == 0x55:
        if completed.returncode not in (0, *UCsim_NORMAL_EXIT_CODES):
            raise AssertionError(
                f"{ihx_name}: PASS verdict but uCsim exited with "
                f"unexpected status {completed.returncode}")
        return
    if status == 0xEE:
        raise AssertionError(
            f"{ihx_name}: baseline assertion failed "
            f"(source line {line})")
    raise AssertionError(
        f"{ihx_name}: control block status {status:#04x} is neither "
        f"PASS nor FAIL (exit {completed.returncode})")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sdcc", required=True, type=Path)
    ap.add_argument("--s51", required=True, type=Path)
    ap.add_argument("--source", required=True, type=Path)
    args = ap.parse_args()
    for tool in (args.sdcc, args.s51):
        if not tool.exists():
            raise AssertionError(f"missing tool: {tool}")
    args.sdcc = args.sdcc.resolve()
    args.s51 = args.s51.resolve()

    with tempfile.TemporaryDirectory(prefix="ralloc-baseline-") as td:
        tdp = Path(td)
        for model, flags in MODELS:
            stem = model.replace("model-", "").replace("stack-", "stack-")
            asm, ihx = build(args.sdcc, args.source,
                             tdp / f"{stem}.ihx", flags)
            check_asm_tripwires(model, asm)
            run_in_ucsim(args.s51, tdp, ihx.name)
            print(f"PASS ralloc-baseline [{model}] "
                  "(assertions + allocator tripwires)")
    print("PASS: legacy allocator baseline holds in all three models "
          "(S1-S9 behavioural + T1-T4 tripwires)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
