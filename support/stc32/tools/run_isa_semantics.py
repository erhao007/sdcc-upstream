#!/usr/bin/env python3
"""Gate for the ISA semantics programs (tests/simulator/isa_semantics*.asm).

Assembles, links, simulates and *asserts* the memory image after the run —
unlike a bare `dump` the expected bytes are compared automatically, so any
semantic regression fails CI.  Expectations are derived from the Intel
MCS-251 and STC32G manuals (worked examples) and pinned here on purpose:
this file is the independent oracle, deliberately NOT generated from the
simulator.

All generated `.asm`, `.lk` and `.ihx` files stay inside per-case
`TemporaryDirectory` instances.  The sources under `tests/simulator` are
read-only inputs and are never removed by this runner.

Usage:
    PYTHONPATH=tools/pylib python3 tools/run_isa_semantics.py [--s51 PATH]
Exit code: 0 = all assertions pass, 1 = mismatch/build failure.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

SUPPORT_ROOT = Path(__file__).resolve().parent.parent
ROOT = SUPPORT_ROOT.parent.parent
TOOLCHAIN_ROOT = Path(os.environ.get("STC32_TOOLCHAIN_ROOT", str(ROOT / "build" / "install")))


def _host_binary(path):
    """Return `path`, preferring the `.exe` sibling on Windows hosts."""
    if os.name == "nt" and not path.exists() and Path(str(path) + ".exe").exists():
        return Path(str(path) + ".exe")
    return path


SDAS = _host_binary(Path(os.environ.get("STC32_SDAS251", str(TOOLCHAIN_ROOT / "bin" / "sdas251"))))
SDLD = _host_binary(Path(os.environ.get("STC32_SDLD", str(TOOLCHAIN_ROOT / "bin" / "sdld"))))
DEFAULT_S51 = _host_binary(Path(os.environ.get("STC32_UCSIM", str(TOOLCHAIN_ROOT / "bin" / "ucsim_51"))))
SIMDIR = SUPPORT_ROOT / "tests/simulator"

# name -> (asm file, step count, {start: expected bytes})
CASES = {
    "isa_semantics3": (
        "isa_semantics3.asm", 500,
        {0x50: "90 00 03 ab 55 aa 00 01 a5 f8 01 01 01 01 10 01 "
               "be ef 0f ab cd 12 34 12 34 56 78"}),
    # 0x6d is AC after 0xF0+0x10: low nibbles 0+0 carry nothing -> 0x00.
    # 0x73 INC-A Z-path, 0x74 RL-A N-path, 0x6f stays 0 (no fail path).
    # 0x75.. = MUL WR high word 00 01 / low word 5f 90, DIV rem/quot.
    "isa_semantics4": (
        "isa_semantics4.asm", 900,
        {0x30: "00 00 00 01 12 34 00 01 00 00 00 00 00 00 00 00 "
               "01 01 01 01 01 20 20 00 12 34",
         0x60: "00 32 00 01 00 e1 0d 11 04 1c 00 01 01 00 01 00 "
               "01 01 01 01 01 00 01 5f 90 00 06 00 8e 01 01 01"}),
    # Indexed MOV effective-address computation, all eight A5 forms.
    # DRk family (0x29/0x69/0x39/0x79): EA in the 24-bit space, 64 KiB
    # carry at 0x36.  WRj family (0x09/0x49/0x19/0x59): EA in the
    # 16-bit edata window with wrap coverage (0x3d = base+dis wrap,
    # 0x3e/0x3f = 0x59 word-tail wrap, 0x41/0x42 = 0x59 base-addition
    # overflow; correct targets AND wrong aliases preset for a fully
    # deterministic oracle).  Read-backs go through the non-indexed
    # @DRk forms; 0x00 = wrapped 0x59 low byte (positive assertion),
    # 0x02 = wrong-base canary.
    "isa_semantics5": (
        "isa_semantics5.asm", 500,
        {0x00: "77", 0x02: "aa",
         0x30: "5a ab cd 3c be ef 77 31 55 66 77 88 99 93 cc 77",
         0x41: "aa bb"}),
    # ST-1S-A register/accumulator transforms: RLC/RR/RRC/SWAP (result,
    # full PSW1 byte, independent PSW-side CY witness) and MOVS/MOVZ
    # sign/zero extension (16-bit result, full PSW1 preservation).
    # PSW1 layout per STC32G p.553: CY=0x80 AC=0x40 N=0x20 RS=0x18
    # OV=0x04 Z=0x02; CY/AC/RS/OV mirror PSW.  Presets keep RS=0.
    "isa_semantics6": (
        "isa_semantics6.asm", 900,
        {0x50: "0b c4 01 aa 64 00 00 46 00 85 e4 01 43 44 00 "
               "00 46 00 c0 e4 01 00 c6 01 3c c4 01 00 c6 01 "
               "ff 80 e6 00 7f e6 00 ff e6 00 00 e6"}),
    # ST-1S-B CODE/XDATA access:
    # 1) MOVC A,@A+DPTR (A=0, A=2) & MOVC A,@A+PC (A=3)
    # 2) MOVX @DPTR,A / MOVX A,@DPTR in high XDATA (0x1234) & low XDATA (0x0050)
    # 3) MOVX @Ri,A / MOVX A,@Ri (R0=0x40, R1=0x41)
    # 4) Anti-aliasing / non-truncation verification:
    #    - XRAM[0x0034] == 0xEE (asserts 0x1234 is NOT truncated to 0x0034)
    #    - XRAM[0x0042] == 0xAA, XRAM[0x0043] == 0xBB (asserts R0/R1 don't spill/alias)
    #    - XRAM[0x0051] == 0xCC, XRAM[0x1235] == 0xDD (asserts DPTR exact targets)
    #    - XRAM target values 0x1234=0x6A, 0x0050=0x9C, 0x0040=0xB1, 0x0041=0xD2
    # 5) Space separation: IRAM[0x40] canary == 0x33 untouched
    # 6) Individual immediate PSW1 preservation (0xC4) captured after EVERY form.
    "isa_semantics7": (
        "isa_semantics7.asm", 500,
        {
            "iram": {
                0x50: "c4 6a c4 c4 9c c4 c4 b1 c4 c4 d2 c4 7b c4 9f c4 22 c4 33"
            },
            "xram": {
                0x0034: "ee",
                0x0040: "b1 d2 aa bb",
                0x0050: "9c cc",
                0x1234: "6a dd"
            }
        }),
    # ST-1S-C Calls, Returns & Unconditional Jumps (10 forms):
    # LCALL addr16 / LCALL @WRj, ECALL addr24 / ECALL @DRk, RET, ERET,
    # RETI (4-byte frame pop & PSW1 restore), AJMP addr11, EJMP addr24,
    # EJMP @DRk, multi-region (Region 1: 0x01xxxx), anti-fallthrough traps,
    # return frame byte-order inspections and standalone synthesized frames.
    "isa_semantics8": (
        "isa_semantics8.asm", 1500,
        {
            "iram": {
                0x30: "00 c4 02 02 1f 00 c4 00 02 c4 02 02 34 00 c4 00 02 c4 03 02 47 00 00 c4 00 02 c4 03 02 60 00 00 c4 00 02 11 c4 00 02 c4 02 02 53 09 c4 00 02 22 c4 00 02 33 c4 00 02 44 c4 00 02 55 c4 00 02 66 e4 00 02"
            },
            "xram": {
                0x0200: "55"
            }
        }),
    # ST-1S-D Conditional Jumps & Loops (9 forms):
    # JC, JNC, JNE, JSG, JSGE, JBC bit51, JBC bit, DJNZ Rn, DJNZ dir8.
    # Dual-branch verification (taken vs fallthrough), bit-clearing side-effects,
    # counter decrements, PSW1 preservation, SPX and XRAM boundary canary.
    "isa_semantics9": (
        "isa_semantics9.asm", 2000,
        {
            "iram": {
                0x10: "00",
                0x31: "11 c4 12 44 21 44 22 c4 31 44 32 46 41 40 42 24 43 02 44 20 45 04 51 40 52 24 53 02 54 20 55 04 61 a2 44 62 a2 44 71 45 44 72 45 44 81 01 44 82 00 44 91 01 44 92 00 44 00 02"
            },
            "xram": {
                0x0200: "55"
            }
        }),
    # ST-1S-E Special / Simple forms (3 forms):
    # NOP (1-byte no-op, PC advances, PSW1 & R1..R4 intact),
    # TRAP (1-byte STC32G NOP opcode 0xB9, PC advances, PSW1 & R1..R4 intact),
    # ESC (structural prefix N/A).
    "isa_semantics10": (
        "isa_semantics10.asm", 1500,
        {
            "iram": {
                0x10: "00",
                0x31: "aa c4 55 c4 12 34 56 78 bb 64 66 64 9a bc de f0 00 02"
            },
            "xram": {
                0x0200: "55"
            }
        }),
}

# Full-text disassembly assertions for the eight indexed MOV forms:
# the strict probe only checks the mnemonic, so a wrong base register
# in the printed operands (0x59 rendering @WR6 as @DR12) needs this
# separate operand-level gate.  uCsim prints register names uppercase.
IDX16_DISASS_EXPECT = [
    r"MOV\s+R10,@DR16\+0x0000",     # 0x29
    r"MOV\s+WR6,@DR16\+0x0000",     # 0x69
    r"MOV\s+@DR16\+0x0004,R3",      # 0x39
    r"MOV\s+@DR16\+0x0008,WR4",     # 0x79
    r"MOV\s+R10,@WR6\+0x0050",      # 0x09
    r"MOV\s+WR6,@WR6\+0x0051",      # 0x49
    r"MOV\s+@WR6\+0x0053,R3",       # 0x19
    r"MOV\s+@WR6\+0x0054,WR4",      # 0x59
]

# Hand-assembled image: JMP @A+DPTR is a 16-bit modular addition with
# PC.23:16 taken from DPXL (the SDCC genJumpTab contract: it loads
# "mov dpxl,#(label>>16)" before "jmp @a+dptr").  With DPX = 0x01FFFF
# and A = 1 the low 16 bits wrap to 0x0000, so the landing PC is
# 0x010000 — this single case distinguishes all three candidate
# implementations: 24-bit add -> 0x020000, keep-caller-region -> 0x000000,
# DPXL region -> 0x010000 (expected).
JMPMOD_RECORDS = [
    bytes([0x02, 0x01, 0x00]),    # 0x0000: ljmp 0x0100
    bytes([0x90, 0xff, 0xff,      # 0x0100: mov dptr,#0xffff
           0x75, 0x84, 0x01,      # 0x0103: mov dpxl,#0x01
           0x74, 0x01,            # 0x0106: mov a,#0x01
           0x73]),                # 0x0108: jmp @a+dptr -> PC=0x010000 (DPXL region, low wraps)
]
JMPMOD_STEPS = 5                  # ljmp + 3 setup + jmp
JMPMOD_EXPECT_PC = 0x010000


def run_case(s51: Path, name: str, asm: str, steps: int,
             expects: dict) -> bool:
    with tempfile.TemporaryDirectory(prefix="isasem-") as td:
        tdp = Path(td)
        # as251 cannot open absolute paths (asxxxx path handling), so
        # copy the source into the working directory and use its name
        (tdp / asm).write_bytes((SIMDIR / asm).read_bytes())
        r = subprocess.run([str(SDAS), "-plosg", asm], cwd=tdp,
                           capture_output=True, text=True, errors="replace")
        if r.returncode != 0:
            print(f"FAIL {name}: sdas251: {r.stdout}{r.stderr}")
            return False
        lk_lines = [f"-i {name}.ihx", "-b MCS251CODE = 0x0000"]
        if "MCS251REG1" in (SIMDIR / asm).read_text():
            lk_lines.append("-b MCS251REG1 = 0x010900")
        lk_lines.append(f"{name}.rel\n")
        (tdp / f"{name}.lk").write_text("\n".join(lk_lines))
        r = subprocess.run([str(SDLD), "-nf", f"{name}.lk"], cwd=tdp,
                           capture_output=True, text=True, errors="replace")
        if r.returncode != 0 or not (tdp / f"{name}.ihx").exists():
            print(f"FAIL {name}: sdld: {r.stdout}{r.stderr}")
            return False

        # Normalize expects to {space: {start: bytes_str}}
        if all(isinstance(k, int) for k in expects):
            norm_expects = {"iram": expects}
        else:
            norm_expects = expects

        dump_lines = []
        for space in ["iram", "xram"]:
            if space in norm_expects:
                ranges = norm_expects[space]
                lo = min(ranges)
                hi = max(k + len(v.split()) - 1 for k, v in ranges.items())
                dump_lines.append(f"dump {space} {lo:#04x} {hi:#04x}")
        cmd = f"set error unknown_code on\nset opt selfjump_stop 0\nstep {steps} vclk\n" + "\n".join(dump_lines) + "\nquit\n"

        r = subprocess.run([str(s51), "-t251", "-c", "-", "-m",
                            "-S", f"in={os.devnull},out=-", f"{name}.ihx"],
                           cwd=tdp, input=cmd, capture_output=True,
                           text=True, errors="replace", timeout=60)
        out = re.sub(r"\x1b\[0K|\[0K", "", r.stdout + r.stderr)
        if "unknown instruction code" in out or "Invalid instruction" in out:
            print(f"FAIL {name}: simulator hit an unknown opcode:\n{out}")
            return False

        mem = {space: {} for space in norm_expects}
        # Parse memory dump lines: IRAM uses 2-digit hex (0x00..0xff);
        # XRAM uses 4/6-digit hex (0x0000..0xffffff).
        for m in re.finditer(r"^0x([0-9a-fA-F]{2,6})\s+((?:[0-9a-fA-F]{2,4}[ \t]+)+)", out, re.M):
            addr_str = m.group(1)
            base = int(addr_str, 16)
            space = "iram" if len(addr_str) <= 2 else "xram"
            if space in mem:
                for i, by in enumerate(m.group(2).split()):
                    mem[space][base + i] = int(by, 16)

        ok = True
        total_asserts = 0
        for space, ranges in norm_expects.items():
            for start, exp in ranges.items():
                total_asserts += 1
                want = [int(b, 16) for b in exp.split()]
                got = [mem[space].get(start + i) for i in range(len(want))]
                if got != want:
                    ok = False
                    print(f"FAIL {name} {space}@{start:#04x}: "
                          f"want {' '.join(f'{b:02x}' for b in want)} "
                          f"got {' '.join('??' if b is None else f'{b:02x}' for b in got)}")
        if ok:
            print(f"PASS {name} ({total_asserts} asserted ranges)")
        return ok


def check_idx16_disassembly(s51: Path) -> bool:
    """Assemble isa_semantics5 and gate the FULL disassembly text of the
    eight indexed MOV forms (base register + displacement, not just the
    mnemonic)."""
    with tempfile.TemporaryDirectory(prefix="idx16dis-") as td:
        tdp = Path(td)
        asm = "isa_semantics5.asm"
        (tdp / asm).write_bytes((SIMDIR / asm).read_bytes())
        r = subprocess.run([str(SDAS), "-plosg", asm], cwd=tdp,
                           capture_output=True, text=True, errors="replace")
        if r.returncode != 0:
            print(f"FAIL idx16-disass: sdas251: {r.stdout}{r.stderr}")
            return False
        name = "isa_semantics5"
        (tdp / f"{name}.lk").write_text(
            f"-i {name}.ihx\n-b MCS251CODE = 0x0000\n{name}.rel\n")
        r = subprocess.run([str(SDLD), "-nf", f"{name}.lk"], cwd=tdp,
                           capture_output=True, text=True, errors="replace")
        if r.returncode != 0 or not (tdp / f"{name}.ihx").exists():
            print(f"FAIL idx16-disass: sdld: {r.stdout}{r.stderr}")
            return False
        r = subprocess.run(
            [str(s51), "-t251", "-c", "-", "-m", "-S",
             f"in={os.devnull},out=-", f"{name}.ihx"],
            cwd=tdp, input="dc 0x0000 0x0200\nquit\n",
            capture_output=True, text=True, errors="replace", timeout=60)
        out = re.sub(r"\x1b\[0K|\[0K", "", r.stdout + r.stderr)
        ok = True
        for pat in IDX16_DISASS_EXPECT:
            if not re.search(pat, out):
                ok = False
                print(f"FAIL idx16-disass: {pat} not found in "
                      "disassembly of the eight indexed MOV forms")
        if ok:
            print("PASS idx16-disass (8 indexed MOV operand texts)")
        return ok


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--s51", type=Path, default=DEFAULT_S51)
    args = ap.parse_args()
    args.s51 = _host_binary(args.s51)
    for tool in (SDAS, SDLD, args.s51):
        if not tool.exists():
            print(f"error: missing {tool} (build first)", file=sys.stderr)
            return 2
    ok = all(run_case(args.s51, n, *c) for n, c in CASES.items())
    ok = check_idx16_disassembly(args.s51) and ok

    # JMP @A+DPTR 16-bit modular-addition check (hand-assembled image)
    with tempfile.TemporaryDirectory(prefix="jmpmod-") as td:
        tdp = Path(td)
        def recs(blob, base):
            out = []
            for off in range(0, len(blob), 16):
                chunk = blob[off:off + 16]
                r = bytes([len(chunk), (base + off) >> 8 & 0xff,
                           (base + off) & 0xff, 0]) + chunk
                out.append(":" + r.hex().upper() + f"{(-sum(r)) & 0xff:02X}")
            return out
        lines = []
        for i, blob in enumerate(JMPMOD_RECORDS):
            lines += recs(blob, [0x0000, 0x0100][i])
        (tdp / "jmpmod.ihx").write_text(
            "\n".join(lines) + "\n:00000001FF\n")
        r = subprocess.run([str(args.s51), "-t251", "-c", "-", "-m",
                            "-S", f"in={os.devnull},out=-", "jmpmod.ihx"],
                           cwd=tdp,
                           input="set opt selfjump_stop 0\n"
                                 f"step {JMPMOD_STEPS}\nstate\nquit\n",
                           capture_output=True, text=True, errors="replace", timeout=60)
        out = re.sub(r"\x1b\[0K|\[0K", "", r.stdout + r.stderr)
        m = re.search(r"PC=\s*0x([0-9a-f]+)", out)
        if m and int(m.group(1), 16) == JMPMOD_EXPECT_PC:
            print("PASS jmpmod (JMP @A+DPTR 16-bit modular add)")
        else:
            got = m.group(1) if m else "??"
            print(f"FAIL jmpmod: PC 0x{got} != 0x{JMPMOD_EXPECT_PC:04x} "
                  f"(24-bit add gives 0x020000, keep-region gives 0x000000)")
            ok = False
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
