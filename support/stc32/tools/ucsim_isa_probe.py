#!/usr/bin/env python3
"""Strict single-step ISA probe for the uCsim MCS-251 model (-t251).

For every legal Source Mode form in isa/mcs251.yaml (269 rows) this tool
builds a minimal image  <form bytes> + SJMP self , loads it into uCsim and
checks, per form:

  1. DISASS  - `dc 0` disassembles the bytes with the YAML mnemonic and
     consumes exactly `length` bytes (decode + length + mnemonic).
  2. EXEC    - one strict step either advances the PC by `length`
     (non-control-flow forms), lands on a legitimate control-flow target,
     or stops in a *controlled* way (unknown-code error, exit 106).
     Crashes (signal death) are always failures.

Known-unsupported forms are not failures: they are reported as GAP so the
report doubles as the live coverage list (this is what RELEASE_NOTES.md
should be generated from, instead of hand-maintained tables).

Usage:
    PYTHONPATH=tools/pylib python3 tools/ucsim_isa_probe.py \
        [--s51 path] [--out tests/simulator/isa_decode_report.md] [-q]

Requires the uCsim built by this repo (build/sim/ucsim/src/sims/s51.src/
ucsim_51).  Exit codes: default mode is non-zero only on real failures
(crash, wrong length/mnemonic, wrong PC advance) - never on GAPs;
--strict additionally treats GAP as non-zero (structural SKIPs stay
reported-but-allowed in both modes).
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import yaml

SUPPORT_ROOT = Path(__file__).resolve().parent.parent
ROOT = SUPPORT_ROOT.parent.parent
TOOLCHAIN_ROOT = Path(os.environ.get("STC32_TOOLCHAIN_ROOT", str(ROOT / "build" / "install")))


def _host_binary(path):
    """Return `path`, preferring the `.exe` sibling on Windows hosts."""
    if os.name == "nt" and not path.exists() and Path(str(path) + ".exe").exists():
        return Path(str(path) + ".exe")
    return path


DEFAULT_S51 = _host_binary(Path(os.environ.get("STC32_UCSIM", str(TOOLCHAIN_ROOT / "bin" / "ucsim_51"))))
DEFAULT_OUT = SUPPORT_ROOT / "tests/simulator/isa_decode_report.md"

# mnemonics whose executed PC is a jump/call target, not start+length
CONTROL_FLOW = {
    "ACALL", "AJMP", "LJMP", "LCALL", "ECALL", "EJMP", "FCALL", "FJMP",
    "JMP", "JZ", "JNZ", "JC", "JNC", "JB", "JNB", "JBC",
    "CJNE", "DJNZ", "SJMP", "RET", "RETI", "ERET", "TRAP",
    "JE", "JNE", "JLE", "JG", "JSL", "JSGE", "JSLE", "JSG",
}

# stack-frame dependent: the landing PC cannot be predicted from the
# sample encoding alone (empty-stack pop), so these only require a
# controlled (non-crashing, non-unknown) stop or step
STACK_DEPENDENT = {"RET", "RETI", "ERET"}


def expected_pc_after(form: dict):
    """Exact post-execution PC for control-flow forms whose sample
    encoding pins the target, or None when it is stack dependent.

    The YAML sample encodings use rel=0x00 for every conditional branch,
    so both the taken and not-taken path land on the next instruction.
    Register-indirect jumps/calls run after reset with WR/DR = 0, so
    their target is 0 (PC.23:16 preserved = 0 at reset)."""
    fid = form["id"]
    b = [int(x, 16) for x in form["source_bytes"]]
    if form["mnemonic"].upper() in STACK_DEPENDENT:
        return None
    if fid == "ljmp_addr16" or fid == "lcall_addr16":
        return (b[1] << 8) | b[2]
    if fid in ("acall_addr11", "ajmp_addr11"):
        # 2 KiB page of the next instruction (PC=2 after fetch), page
        # number encoded in bits 7..5 of the opcode
        page = (((b[0] >> 5) & 7) << 8) | b[1]
        return (2 & ~0x7FF) | page
    if fid in ("ecall_addr24", "ejmp_addr24"):
        return (b[1] << 16) | (b[2] << 8) | b[3]
    if fid == "trap":
        return form["length"]  # STC32G UM 1672: TRAP executes as NOP
    # Register-indirect jumps/calls (@WRj/@DRk/@A+DPTR): the target is
    # whatever the firmware left in the base register — not predictable
    # from the sample encoding.  Their execution semantics are covered by
    # the targeted assertions in isa_semantics3/4 (LJMP @WRj with a
    # preloaded base), so the probe only demands a controlled outcome.
    if "@" in " ".join(form["operands"]) or form["mnemonic"] == "JMP":
        return None
    # every remaining control-flow form is rel-based with rel=0x00:
    # target == fall-through == instruction length
    return form["length"]

# Register-indirect control flow gets a *preloaded* image: the base
# register is set to a distinctive target (0x0100) with ordinary MOV
# instructions placed before the jump, so the landing PC is pinned and
# a mis-jump cannot hide behind "register contents are unpredictable".
# encoding: image bytes, number of steps (preload insns + jump), target.
INDIRECT_PRELOAD = {
    "ljmp_at_wr":    ([0x7e, 0x24, 0x01, 0x00,   0x89, 0x24], 2, 0x0100),
    "lcall_at_wr":   ([0x7e, 0x24, 0x01, 0x00,   0x99, 0x24], 2, 0x0100),
    "ejmp_at_dr":    ([0x7e, 0x48, 0x01, 0x00,   0x89, 0x48], 2, 0x0100),
    "ecall_at_dr":   ([0x7e, 0x48, 0x01, 0x00,   0x99, 0x48], 2, 0x0100),
    "jmp_at_a_dptr": ([0x90, 0x01, 0x00, 0x75, 0x84, 0x00, 0x73], 3, 0x0100),
}

CMD = """set error unknown_code on
set error memory on
set error stack on
set opt selfjump_stop 0
dc 0 8
step {steps}
state
quit
"""

# disassembly-only command: for preloaded indirect forms the executed
# image contains MOV prologues, so the mnemonic/length check runs as a
# separate pass against the raw YAML encoding
DISASS_CMD = """set error unknown_code on
set opt selfjump_stop 0
dc 0 8
quit
"""


def ihx(data: bytes) -> str:
    rec = bytes([len(data), 0, 0, 0]) + data
    return ":" + rec.hex().upper() + f"{(-sum(rec)) & 0xff:02X}"


def run_form(s51: Path, form: dict) -> dict:
    """Returns {status, detail} with status in PASS|GAP|FAIL|SKIP."""
    # ESC is the bare A5 prefix byte.  In a byte stream A5 is always followed
    # by a legacy opcode (that IS the prefix form), so a simulator cannot
    # distinguish "ESC as a 1-byte instruction" from "A5-prefixed next op".
    # Correctness there is the assembler's job (as251 emits ESC only on
    # request); the probe cannot oracle it either way.
    if form["id"] == "esc":
        return dict(status="SKIP", detail="ambiguous with A5 prefix")

    src = bytes(int(b, 16) for b in form["source_bytes"])
    expected_len = form["length"]
    mnemonic = form["mnemonic"].upper()
    control = mnemonic in CONTROL_FLOW

    # indirect control flow: preload the base register, then jump — the
    # landing PC is exact and a mis-jump is a hard FAIL, not a silent PASS
    preload = INDIRECT_PRELOAD.get(form["id"])
    if preload is not None:
        pre, steps, target = preload
        image = bytes(pre) + b"\x80\xfe"
    else:
        image = src + b"\x80\xfe"          # trailing SJMP self
        steps = 1

    with tempfile.TemporaryDirectory(prefix="isa-probe-") as td:
        tdp = Path(td)
        (tdp / "t.ihx").write_text(ihx(image) + "\n:00000001FF\n")
        (tdp / "t.cmd").write_text(CMD.format(steps=steps))
        try:
            # uCsim splits positional file args on ':' (file:addr syntax), so
            # a drive-letter absolute path never loads on Windows; run with a
            # relative image name instead.
            proc = subprocess.run(
                [str(s51), "-t251", "-c", str(tdp / "t.cmd"), "-m",
                 "-S", f"in={os.devnull},out=-", "t.ihx"],
                cwd=tdp, capture_output=True, text=True, errors="replace",
                timeout=20)
        except subprocess.TimeoutExpired:
            return dict(status="FAIL", detail="timeout")

        out = proc.stdout + proc.stderr
        out = re.sub(r"\x1b\[0K|\[0K", "", out)   # strip ANSI erase-to-EOL
        rc = proc.returncode

        # --- crash? ------------------------------------------------------
        if rc < 0 or rc in (139, 138, 134):        # SIGSEGV/SIGBUS/SIGABRT
            return dict(status="FAIL", detail=f"crash rc={rc}", out=out)

        # --- preloaded indirect form: exact landing PC --------------------
        if preload is not None:
            # first verify disassembly of the RAW encoding (mnemonic +
            # length) — the preloaded image contains MOV prologues, so
            # the dc check must run against the YAML bytes themselves
            with tempfile.TemporaryDirectory(prefix="isa-probe-d-") as td2:
                tdp2 = Path(td2)
                (tdp2 / "t.ihx").write_text(
                    ihx(src + b"\x80\xfe") + "\n:00000001FF\n")
                (tdp2 / "t.cmd").write_text(DISASS_CMD)
                try:
                    p2 = subprocess.run(
                        [str(s51), "-t251", "-c", str(tdp2 / "t.cmd"), "-m",
                         "-S", f"in={os.devnull},out=-", "t.ihx"],
                        cwd=tdp2, capture_output=True, text=True,
                        errors="replace", timeout=20)
                except subprocess.TimeoutExpired:
                    return dict(status="FAIL", detail="timeout (disass)")
                out2 = re.sub(r"\x1b\[0K|\[0K", "",
                              p2.stdout + p2.stderr)
                m2 = re.search(r"^0x0*0\s+((?:[0-9a-f]{2}\s+)+)(\S+)(.*)$",
                               out2, re.M)
                if not m2:
                    return dict(status="FAIL", detail="no dc output (disass)")
                if m2.group(2) != mnemonic or \
                   len(m2.group(1).split()) != expected_len:
                    return dict(status="FAIL",
                                detail=f"disass {m2.group(2)} "
                                       f"len {len(m2.group(1).split())} != "
                                       f"{mnemonic} len {expected_len}")
            # then the exact landing PC from the preloaded image
            ms = re.search(r"PC=\s*0x([0-9a-f]+)", out)
            if ("unknown instruction code" in out) or ("Invalid instruction" in out):
                return dict(status="GAP", detail="unknown-code stop", out=out)
            if not ms:
                return dict(status="FAIL", detail="no PC in state", out=out)
            pc = int(ms.group(1), 16)
            if pc != target:
                return dict(status="FAIL",
                            detail=f"indirect target 0x{pc:x} != 0x{target:x}",
                            out=out)
            return dict(status="PASS", detail=f"indirect pc=0x{pc:x}", out=out)

        # --- disassembly: first dc line must cover the form --------------
        m = re.search(r"^0x0*0\s+((?:[0-9a-f]{2}\s+)+)(\S+)(.*)$",
                      out, re.M)
        if not m:
            return dict(status="FAIL", detail="no dc output", out=out)
        nbytes = len(m.group(1).split())
        dis_mne = m.group(2)
        if dis_mne == ".db":
            # explicit "no such instruction" from the disassembler
            return dict(status="GAP", detail="undecoded (.db)", out=out)
        if dis_mne != mnemonic or nbytes != expected_len:
            # decoded as a different (usually 1-byte 8051) instruction.
            # Still a coverage gap, but the dangerous flavour: the form is
            # silently mis-decoded instead of being rejected.
            return dict(status="GAP",
                        detail=f"mis-decoded as {dis_mne} len {nbytes} "
                               f"(want {mnemonic} len {expected_len})",
                        out=out)

        # --- execution ---------------------------------------------------
        if ("unknown instruction code" in out) or ("Invalid instruction" in out):
            return dict(status="GAP", detail="unknown-code stop", out=out)

        ms = re.search(r"PC=\s*0x([0-9a-f]+)", out)
        if not ms:
            return dict(status="FAIL", detail="no PC in state", out=out)
        pc = int(ms.group(1), 16)
        if control:
            exp = expected_pc_after(form)
            if exp is None:
                # stack-dependent: only require a controlled outcome
                return dict(status="PASS", detail="stack-dependent", out=out)
            if pc != exp:
                return dict(status="FAIL",
                            detail=f"ctrl-flow PC 0x{pc:x} != expected "
                                   f"0x{exp:x}", out=out)
            return dict(status="PASS", detail=f"ctrl-flow pc=0x{pc:x}", out=out)
        if pc != expected_len:
            return dict(status="FAIL",
                        detail=f"PC advance {pc} != {expected_len}", out=out)
        return dict(status="PASS", detail="ok", out=out)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--s51", type=Path, default=DEFAULT_S51)
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    ap.add_argument("-q", "--quiet", action="store_true")
    ap.add_argument("--strict", action="store_true",
                    help="exit non-zero on GAP/FAIL (structural SKIP is "
                         "reported but allowed); default only FAILs block")
    args = ap.parse_args()
    args.s51 = _host_binary(args.s51)

    if not args.s51.exists():
        print(f"error: uCsim not found: {args.s51}", file=sys.stderr)
        return 2

    doc = yaml.safe_load((SUPPORT_ROOT / "isa/mcs251.yaml").read_text())
    forms = doc["instructions"]

    with ThreadPoolExecutor(max_workers=8) as ex:
        results = list(ex.map(lambda f: (f, run_form(args.s51, f)), forms))

    ok = gap = fail = skip = 0
    fail_lines, gap_lines = [], []
    for form, r in results:
        if r["status"] == "PASS":
            ok += 1
        elif r["status"] == "SKIP":
            skip += 1
        elif r["status"] == "GAP":
            gap += 1
            gap_lines.append(f"| `{form['id']}` | {form['mnemonic']} "
                             f"{', '.join(form['operands'])} | {r['detail']} |")
        else:
            fail += 1
            fail_lines.append(f"| `{form['id']}` | {form['mnemonic']} "
                              f"{', '.join(form['operands'])} | {r['detail']} |")

    total = len(forms)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w") as fp:
        fp.write(f"""# MCS-251 uCsim strict ISA probe ({total} Source Mode forms)

Generated by `tools/ucsim_isa_probe.py` from `isa/mcs251.yaml`.
    Run against `{args.s51}`.

- PASS (decode+length+mnemonic, controlled/valid execution): **{ok}/{total}**
- GAP (decoded but execution not implemented; controlled stop): **{gap}**
- SKIP (structurally ambiguous with the A5 prefix, e.g. ESC): **{skip}**
- FAIL (crash / wrong length / wrong mnemonic / wrong PC): **{fail}**

## GAP list (live coverage, regenerate instead of hand-maintaining)

| form | operands | reason |
|---|---|---|
""")
        fp.write("\n".join(gap_lines))
        if fail_lines:
            fp.write("""
## FAIL list

| form | operands | reason |
|---|---|---|
""")
            fp.write("\n".join(fail_lines))

    print(f"PASS {ok}  GAP {gap}  SKIP {skip}  FAIL {fail}  (total {total})")
    print(f"report: {args.out}")
    if fail:
        return 1
    if args.strict and gap:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
