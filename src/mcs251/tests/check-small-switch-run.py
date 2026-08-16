#!/usr/bin/env python3
"""Runtime regression for the genJumpTab small-switch region guard.

check-wide-shift-jump-table.py verifies statically that the guard
assembles and never touches R24-R27; this check additionally EXECUTES
the DPXL!=0 slow path.  small-switch-run.c is built twice:

  * default code location  -> table in region 0 -> fast JMP @A+DPTR
  * --code-loc 0x010000    -> table in region 1 -> MOVC rebuild +
                              MOV DR28,DPX + EJMP @DR28

(The uCsim 251 memory model has ROM only up to 0x01ffff, so the FF:
APP region cannot be simulated; the guard triggers for ANY nonzero
DPXL, and region 01 exercises the identical slow path.)

Both images run in uCsim until the final self-jump and must produce
the same eight bytes in direct IRAM, derived from the C semantics —
an oracle independent of the codegen under test.

Output equivalence alone does not prove WHICH path ran (uCsim's
high-region JMP @A+DPTR also dispatches correctly, so a build whose
guard unconditionally branches to the fast path still produces the
right bytes).  The program therefore self-reports the executed path:
its epilogue snapshots DR28 to IRAM 0x40..0x43.  DR28 is written only
by the slow path's "mov dr28,dpx", so:

  * high build: IRAM 0x41 (bits 23:16 of the dispatch target) must
    be 0x01 — proving the MOVC rebuild and MOV DR28,DPX executed;
  * low build: the whole snapshot must stay zero — proving the guard
    took the fast JMP @A+DPTR branch.

Failures of the dispatch itself (wrong MOVC byte order, clobbered
B/ACC, bad DR28 target) show up as wrong or missing result bytes.
"""

import argparse
import re
import subprocess
import tempfile
from pathlib import Path

EXPECTED = [0x11, 0x22, 0x44, 0x88, 0xEE, 0xEE, 0xEE, 0xA5]
RESULTS_AT = 0x30
DR28_AT = 0x40        # program snapshots DR28 here (slow-path sentinel)
STEPS = 4000          # generous: layout holes march as NOPs first


def build(sdcc, source, out, extra_flags, want_ast):
    command = [
        str(sdcc), "-mmcs251", "--std=gnu17",
        "--model-large", "--opt-code-speed",
        *extra_flags,
        "-o", str(out), str(source),
    ]
    completed = subprocess.run(
        command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, errors="replace",
    )
    if completed.returncode:
        raise AssertionError(
            f"build failed ({completed.returncode}): "
            f"{' '.join(command)}\n{completed.stdout}"
        )
    ast = out.with_suffix(".asm").read_text()
    if want_ast:
        if not re.search(r"^[ \t]*mov[ \t]+dr28,dpx[ \t]*$",
                         ast, re.I | re.M):
            raise AssertionError(
                "switch did not compile to a guarded jump table "
                f"({out.stem}): no 'mov dr28,dpx' in the assembly — "
                "genJumpTab small-table path missing")
        if not re.search(r"^[ \t]*ejmp[ \t]+@dr28[ \t]*$",
                         ast, re.I | re.M):
            raise AssertionError(
                f"{out.stem}: guard present but no 'ejmp @dr28' "
                "slow-path dispatch")
    return out.with_suffix(".ihx").read_text()


def inject_boot_ejmp(ihx_text, code_loc):
    """Prepend a 0x000000 boot record 'EJMP code_loc' (8A hi mid lo)."""
    if not 0 < code_loc <= 0xFFFFFF:
        raise AssertionError(f"bad code_loc {code_loc:#x}")
    data = bytes([0x8A, (code_loc >> 16) & 0xFF,
                  (code_loc >> 8) & 0xFF, code_loc & 0xFF])
    rec = bytes([len(data), 0, 0, 0]) + data
    chk = (-sum(rec)) & 0xFF
    ela = ":020000040000FA"          # base 0x0000
    boot = f":{len(data):02X}000000{data.hex().upper()}{chk:02X}"
    return ela + "\n" + boot + "\n" + ihx_text


def run_in_ucsim(s51, workspace, ihx_name):
    console = (
        "set error unknown_code on\n"
        "set opt selfjump_stop 0\n"
        f"step {STEPS}\n"
        f"dump iram {RESULTS_AT:#04x} {DR28_AT + 3:#04x}\n"
        "quit\n"
    )
    completed = subprocess.run(
        [str(s51), "-t251", "-c", "-", "-m", "-S",
         "in=/dev/null,out=-", ihx_name],
        input=console, cwd=workspace,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, errors="replace", timeout=120,
    )
    out = re.sub(r"\x1b\[[0-9;]*[a-zA-Z]|\x1b\[?[0-9]*[a-zA-Z]",
                 "", completed.stdout)
    if "unknown instruction code" in out or "Invalid instruction" in out:
        raise AssertionError(f"{ihx_name}: simulator hit an unknown opcode")
    mem = {}
    for m in re.finditer(
            r"^0x([0-9a-f]{2,4})\s+((?:[0-9a-f]{2,4}[ \t]+)+)", out, re.M):
        base = int(m.group(1), 16)
        for i, by in enumerate(m.group(2).split()):
            mem[base + i] = int(by, 16)
    got = [mem.get(RESULTS_AT + i) for i in range(8)]
    dr28 = [mem.get(DR28_AT + i) for i in range(4)]
    return got, dr28


def check_image(label, got, dr28, expect_slow):
    if got != EXPECTED:
        raise AssertionError(
            f"{label}: results "
            f"{' '.join('??' if b is None else f'{b:02x}' for b in got)} "
            f"!= expected {' '.join(f'{b:02x}' for b in EXPECTED)} "
            "(switch arm or default dispatched incorrectly)")
    # a missing/unparsable snapshot byte must fail closed, never
    # count as zero evidence
    if any(b is None for b in dr28):
        raise AssertionError(
            f"{label}: DR28 snapshot incomplete "
            f"({' '.join('??' if b is None else f'{b:02x}' for b in dr28)}) "
            "— sentinel evidence missing")
    if expect_slow:
        # the slow path's MOV DR28,DPX must have executed: the snapshot
        # region byte equals the table's nonzero region (0x01 here)
        if dr28[1] != 0x01:
            raise AssertionError(
                f"{label}: DR28 snapshot "
                f"{' '.join(f'{b:02x}' for b in dr28)} "
                "shows the slow path (mov dr28,dpx / ejmp @dr28) never "
                "executed — output equivalence cannot substitute for it")
    else:
        # region-0 build: the guard must take the fast JMP @A+DPTR;
        # require the exact all-zero snapshot
        if dr28 != [0x00, 0x00, 0x00, 0x00]:
            raise AssertionError(
                f"{label}: DR28 snapshot "
                f"{' '.join(f'{b:02x}' for b in dr28)} "
                "unexpectedly nonzero — fast path not taken")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sdcc", required=True, type=Path)
    ap.add_argument("--s51", required=True, type=Path)
    ap.add_argument("--source", required=True, type=Path)
    args = ap.parse_args()
    for tool in (args.sdcc, args.s51):
        tool = tool.resolve()
        if not tool.exists():
            raise AssertionError(f"missing tool: {tool}")
    args.sdcc = args.sdcc.resolve()
    args.s51 = args.s51.resolve()

    with tempfile.TemporaryDirectory(prefix="swrun-") as td:
        tdp = Path(td)

        low_ihx = build(args.sdcc, args.source, tdp / "low.ihx", (), False)
        high_loc = 0x010000
        high_ihx = build(args.sdcc, args.source, tdp / "high.ihx",
                         ("--code-loc", f"{high_loc:#x}"), True)
        boot_ihx = inject_boot_ejmp(high_ihx, high_loc)
        (tdp / "high_boot.ihx").write_text(boot_ihx)

        got, dr28 = run_in_ucsim(args.s51, tdp, "low.ihx")
        check_image("low (region 0, fast path)", got, dr28, False)
        got, dr28 = run_in_ucsim(args.s51, tdp, "high_boot.ihx")
        check_image(f"high ({high_loc:#x}, slow path)", got, dr28, True)
    print("PASS: small-switch fast and DPXL!=0 slow paths dispatch "
          "identically in uCsim (DR28 sentinel confirms both paths)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
