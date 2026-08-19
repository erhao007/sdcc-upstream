#!/usr/bin/env python3
"""MT-1D ralloc2 directed-path gate (MT-1C fixtures).

Builds a temporary sdcc from the current build objects, rebuilding the
MCS-251 port callback and ralloc2.cc from the current sources (all other
objects are the actual current build artifacts; no user-visible option
exists). The production sdcc must expose both the selected ralloc2 entry
and the retained legacy comparison entry.

The directed compiler then FULLY compiles two self-checking fixtures through
the MT-1D selected callback; the callback uses ralloc2 for the proven subset
and fail-closed legacy allocation for paths whose invariants are not closed:

  - ralloc-baseline.c (the MT-1A risk catalogue: byte/word/dword
    pressure, overlap, values live across calls, forced spilling,
    reentrant recursion, soft-float with live integers, a non-leaf ISR
    and native 16x16->32 multiplies), and
  - ralloc2-extras.c (setjmp/longjmp, pointer post-increment walk,
    switch, aggregate by-value/hidden return, bit scalars, shifts,
    and same-function R0/R1 pointer-scratch pressure)

The gate also compiles and executes ralloc2-mulreg.c with the production
compiler and a compiler rebuilt with MCS251_RALLOC2_FORCE.  Their normalized
assembly must match, proving that the default callback reaches the actual
ralloc2 subset.  The same fixture proves that a pointer value spills and keeps
all 24 arithmetic bits without triggering the legacy fallback.  A third
compiler rebuilt with the native-MUL exclusion disabled must place the product
in the forbidden R8..R15 fallback; this mutation keeps the allocation tripwire
live.

in model-small, model-large and stack-auto, links IHX images with the
real assembler/linker, and executes them in uCsim.  Every image must
report PASS (0x55) through the fixtures' control block; anything else
(fail verdict, missing verdict, simulator crash) fails the gate.
"""

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

STATUS_AT = 0x30
STEPS = 1000000


def run(command, **kwargs):
    completed = subprocess.run(command, capture_output=True, text=True,
                               errors="replace", **kwargs)
    return completed


def run_ok(command, **kwargs):
    completed = run(command, **kwargs)
    if completed.returncode != 0:
        raise AssertionError(
            f"command failed ({completed.returncode}): "
            f"{' '.join(map(str, command))}\n"
            f"{completed.stdout[-500:]}{completed.stderr[-500:]}")
    return completed.stdout


def defines_symbol(nm_text, symbol):
    return re.search(rf"\b[Tt]\s+_?{re.escape(symbol)}$",
                     nm_text, re.M) is not None


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build_directed_sdcc(ralloc2_source, sdcc, workdir,
                        ralloc2_defines=""):
    sdcc_abs = sdcc.resolve()
    candidates = [sdcc_abs.parents[1] / "src", sdcc_abs.parents[2] / "src"]
    build_src = next((p for p in candidates if (p / "Makefile").exists()),
                     None)
    if build_src is None:
        raise AssertionError(
            f"cannot locate the build src tree for {sdcc_abs}: "
            f"{candidates}")
    port_archive = build_src / "mcs251" / "port.a"
    main_source = ralloc2_source.parent / "main.c"
    for required in (build_src / "Makefile", port_archive, main_source):
        if not required.exists():
            raise AssertionError(f"missing directed-link input: {required}")

    directed_main = workdir / "main.o"
    directed_port = workdir / "mcs251-directed.a"
    directed_sdcc = workdir / "bin" / "sdcc"
    directed_sdcc.parent.mkdir(exist_ok=True)
    extra_makefile = workdir / "ralloc2-directed.mk"
    extra_makefile.write_text(
        ".PHONY: ralloc2-directed-main ralloc2-directed-ralloc2 "
        "ralloc2-directed-link\n"
        "ralloc2-directed-main:\n"
        "\t$(CC) $(CFLAGS) $(CPPFLAGS) -Imcs251 "
        "-c $(MCS251_MAIN) -o $(DIRECTED_MAIN)\n"
        "ralloc2-directed-ralloc2:\n"
        "\t$(CXX) $(CXXFLAGS) $(CPPFLAGS) "
        "$(MCS251_RALLOC2_DEFINES) -c $(MCS251_RALLOC2) "
        "-o $(DIRECTED_RALLOC2)\n"
        "ralloc2-directed-link:\n"
        "\t$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $(DIRECTED_SDCC) "
        "$(SLIBOBJS) $(OBJECTS) "
        "$(filter-out mcs251/port.a,$(PORT_LIBS)) "
        "$(DIRECTED_PORT) $(LIBDIRS) $(LIBS)\n")

    make_base = ["make", "-s", "-f", "Makefile", "-f", str(extra_makefile)]
    directed_ralloc2 = workdir / "ralloc2.o"
    run_ok([*make_base, "ralloc2-directed-main",
            f"MCS251_MAIN={main_source}",
            f"DIRECTED_MAIN={directed_main}"], cwd=build_src)
    run_ok([*make_base, "ralloc2-directed-ralloc2",
            f"MCS251_RALLOC2={ralloc2_source}",
            f"DIRECTED_RALLOC2={directed_ralloc2}",
            f"MCS251_RALLOC2_DEFINES={ralloc2_defines}"], cwd=build_src)
    shutil.copy(port_archive, directed_port)
    run_ok(["ar", "d", str(directed_port), "ralloc2.o"])
    run_ok(["ar", "rcs", str(directed_port), str(directed_main),
            str(directed_ralloc2)])
    run_ok([*make_base, "ralloc2-directed-link",
            f"DIRECTED_PORT={directed_port}",
            f"DIRECTED_SDCC={directed_sdcc}"], cwd=build_src)

    # SDCC discovers its sibling tools (sdcpp/sdas251/sdld) and its
    # library share relative to the binary, so mirror them.
    for tool in ("sdcpp", "sdas251", "sdld"):
        src = sdcc.parent / tool
        if not src.exists():
            raise AssertionError(f"missing tool beside sdcc: {src}")
        (workdir / "bin" / tool).symlink_to(src)
    share = build_src.parent / "install" / "share"
    if not share.exists():
        raise AssertionError(f"missing install share tree: {share}")
    if not (workdir / "share").exists():
        (workdir / "share").symlink_to(share)

    nm_out = run_ok(["nm", "-g", str(directed_sdcc)])
    for symbol in ("mcs251_ralloc2_cc", "mcs251_ralloc2_assignRegisters"):
        if not defines_symbol(nm_out, symbol):
            raise AssertionError(
                f"directed sdcc did not link actual {symbol}")
    production_nm = run_ok(["nm", "-g", str(build_src / "sdcc")])
    for symbol in ("mcs251_ralloc2_cc", "mcs251_ralloc2_assignRegisters"):
        if not defines_symbol(production_nm, symbol):
            raise AssertionError(
                f"production sdcc does not define selected ralloc2 entry "
                f"{symbol}")
    if not defines_symbol(production_nm, "mcs251_assignRegisters"):
        raise AssertionError(
            "production sdcc does not define the legacy allocator entry")
    print("PASS directed and production sdcc link the selected ralloc2 "
          "entry; legacy allocator remains linkable")
    return directed_sdcc


def simulate(s51, ihx_path, expected_xdata=None, require_status=True):
    xdata_dump = ""
    if expected_xdata is not None:
        address, expected = expected_xdata
        address_space = "xdata" if address >= 0x10000 else "xram"
        xdata_dump = (f"dump {address_space} {address:#07x} "
                      f"{address + len(expected) - 1:#07x}\n")
    status_dump = (f"dump iram {STATUS_AT:#04x} {STATUS_AT + 7:#04x}\n"
                   if require_status else "")
    console = (
        "set error unknown_code on\n"
        "set opt selfjump_stop 0\n"
        f"step {STEPS} vclk\n"
        f"{status_dump}"
        f"{xdata_dump}"
        "quit\n"
    )
    completed = run([str(s51), "-t251", "-c", "-", "-m", "-S",
                     f"in={os.devnull},out=-", ihx_path.name],
                    input=console, cwd=ihx_path.parent, timeout=180)
    if completed.returncode < 0:
        raise AssertionError(
            f"{ihx_path.name}: uCsim killed by signal "
            f"{-completed.returncode}")
    out = re.sub(r"\x1b\[[0-9;]*[a-zA-Z]|\x1b\[?[0-9]*[a-zA-Z]",
                 "", completed.stdout + completed.stderr)
    if "unknown instruction code" in out or "Invalid instruction" in out:
        raise AssertionError(
            f"{ihx_path.name}: simulator hit an unknown opcode")
    bytes_dump = []
    if require_status:
        m = re.search(rf"^[ \t]*0x{STATUS_AT:02x}[ \t]+"
                      r"((?:[0-9a-fA-F]{2}[ \t]+)+)", out, re.M)
        if not m:
            raise AssertionError(
                f"{ihx_path.name}: control block not dumped "
                f"(exit {completed.returncode}, snippet: {out[-300:]})")
        bytes_dump = m.group(1).split()
    if expected_xdata is not None:
        address, expected = expected_xdata
        xm = re.search(rf"^[ \t]*0x0*{address:x}[ \t]+"
                       r"((?:[0-9a-fA-F]{2}[ \t]+)+)", out, re.M)
        if not xm:
            raise AssertionError(
                f"{ihx_path.name}: XDATA result at {address:#06x} not dumped")
        actual = [int(value, 16) for value in xm.group(1).split()]
        if actual[:len(expected)] != expected:
            raise AssertionError(
                f"{ihx_path.name}: XDATA result "
                f"{' '.join(f'{value:02x}' for value in actual[:len(expected)])} "
                f"!= expected {' '.join(f'{value:02x}' for value in expected)}")
    status = int(bytes_dump[0], 16) if require_status else None
    return status, bytes_dump


def map_symbol_address(map_path, symbol):
    match = re.search(rf"^[^\n]*?([0-9A-Fa-f]{{8}})\s+_?{re.escape(symbol)}\s",
                      map_path.read_text(errors="replace"), re.M)
    if not match:
        raise AssertionError(
            f"{map_path.name}: linked address for {symbol} not found")
    return int(match.group(1), 16)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sdcc", required=True, type=Path)
    ap.add_argument("--s51", required=True, type=Path)
    ap.add_argument("--ralloc2-source", required=True, type=Path)
    ap.add_argument("--baseline-source", required=True, type=Path)
    ap.add_argument("--extras-source", required=True, type=Path)
    ap.add_argument("--mulreg-source", required=True, type=Path)
    args = ap.parse_args()

    for tool in (args.sdcc, args.s51, args.ralloc2_source,
                 args.baseline_source, args.extras_source,
                 args.mulreg_source):
        if not tool.exists():
            raise AssertionError(f"missing input: {tool}")
    args.sdcc = args.sdcc.resolve()
    args.s51 = args.s51.resolve()
    args.ralloc2_source = args.ralloc2_source.resolve()
    args.baseline_source = args.baseline_source.resolve()
    args.extras_source = args.extras_source.resolve()
    args.mulreg_source = args.mulreg_source.resolve()

    variants = [
        ("model-small", ["--model-small"]),
        ("model-large", ["--model-large"]),
        ("stack-auto", ["--stack-auto"]),
    ]
    fixtures = [("baseline", args.baseline_source),
                ("extras", args.extras_source)]

    with tempfile.TemporaryDirectory(prefix="ralloc2-directed-") as td:
        tdp = Path(td)
        directed = build_directed_sdcc(args.ralloc2_source, args.sdcc, tdp)

        for name, source in fixtures:
            stem = f"fx-{name}"
            shutil.copy(source, tdp / f"{stem}.c")
            for model, flags in variants:
                ihx = tdp / f"{stem}-{model}.ihx"
                completed = run(
                    [str(directed), "-mmcs251", *flags,
                     "--data-loc", "0x38", "-o", str(ihx),
                     str(tdp / f"{stem}.c")], cwd=tdp / "bin")
                output = completed.stdout + completed.stderr
                if completed.returncode != 0 or \
                        re.search(r"error|Undefined", output, re.I):
                    raise AssertionError(
                        f"{name}/{model}: directed compilation failed"
                        f"({completed.returncode}):\n{output[-800:]}")
                status, status_bytes = simulate(args.s51, ihx)
                if status != 0x55:
                    raise AssertionError(
                        f"{name}/{model}: directed ralloc2 image did not "
                        f"pass its self checks (status {status:#04x}, "
                        f"control block {' '.join(status_bytes)})")
                print(f"PASS {name} [{model}] via MT-1D selected callback "
                      "(ralloc2/fail-closed legacy; compile+link+ucsim, "
                      f"0x55; sha256={sha256(ihx)})")

        # Native-MUL exclusion tripwire.  The probe keeps two live dwords
        # in the two bank DR tuples.  With exclusion active, the product
        # must not take an all-R8..R15 allocation.  A second compiler is built
        # with the exclusion deliberately disabled; that mutation must
        # move the product into the high-byte fallback, proving the gate is live
        # rather than merely checking an incidental allocation.
        stem = "fx-mulreg"
        shutil.copy(args.mulreg_source, tdp / f"{stem}.c")
        def compile_mulreg(compiler, suffix, extra_flags=()):
            asm_path = tdp / f"{stem}-{suffix}.asm"
            completed = run(
                [str(compiler), "-mmcs251", "--model-small", *extra_flags, "-S",
                 "-o", str(asm_path), str(tdp / f"{stem}.c")],
                cwd=tdp / "bin")
            if completed.returncode != 0:
                raise AssertionError(
                    f"mulreg probe ({suffix}) compilation failed:\n"
                    f"{(completed.stdout + completed.stderr)[-500:]}")
            return asm_path.read_text()

        def execute_mulreg(compiler, suffix):
            ihx = tdp / f"{stem}-{suffix}.ihx"
            completed = run(
                [str(compiler), "-mmcs251", "--model-small",
                 "--data-loc", "0x38", "-o", str(ihx),
                 str(tdp / f"{stem}.c")], cwd=tdp / "bin")
            output = completed.stdout + completed.stderr
            if completed.returncode != 0 or \
                    re.search(r"error|Undefined", output, re.I):
                raise AssertionError(
                    f"mulreg probe ({suffix}) link failed:\n{output[-800:]}")
            sink_address = map_symbol_address(ihx.with_suffix(".map"),
                                              "g_sink")
            simulate(args.s51, ihx,
                     expected_xdata=(sink_address,
                                     [0xCB, 0x9C, 0x57, 0x50]),
                     require_status=False)
            digest = sha256(ihx)
            print(f"PASS mulreg [{suffix}] actual ralloc2 "
                  f"(compile+link+ucsim, g_sink=0xcb9c5750; "
                  f"sha256={digest})")
            return digest

        pointer_flags = ("-DMCS251_RALLOC2_POINTER_SPILL",)

        def execute_pointer(compiler, suffix):
            ihx = tdp / f"{stem}-{suffix}.ihx"
            completed = run(
                [str(compiler), "-mmcs251", "--model-small", *pointer_flags,
                 "--data-loc", "0x38", "-o", str(ihx),
                 str(tdp / f"{stem}.c")], cwd=tdp / "bin")
            output = completed.stdout + completed.stderr
            if completed.returncode != 0 or \
                    re.search(r"error|Undefined", output, re.I):
                raise AssertionError(
                    f"pointer-spill probe ({suffix}) link failed:\n"
                    f"{output[-800:]}")
            pointer_address = map_symbol_address(ihx.with_suffix(".map"),
                                                 "g_pointer_sink")
            simulate(args.s51, ihx,
                     expected_xdata=(pointer_address, [0x01, 0x03, 0x05]),
                     require_status=False)
            digest = sha256(ihx)
            print(f"PASS pointer-spill [{suffix}] actual ralloc2 "
                  f"(compile+link+ucsim, g_pointer_sink=0x010305; "
                  f"sha256={digest})")
            return digest

        def normalize_asm(asm_text):
            return re.sub(r"^; Version .*?$", "; Version <normalized>",
                          asm_text, flags=re.M)

        def allocation_for(asm_text, symbol):
            pattern = (rf";{re.escape(symbol)}\s+Allocated to "
                       r"registers ((?:r\d+ )+r\d+)\s*$")
            match = re.search(pattern, asm_text, re.M)
            if not match:
                if re.search(rf";{re.escape(symbol)}\s+Allocated to "
                              r"stack\s*$", asm_text, re.M):
                    return []
                if re.search(rf";{re.escape(symbol)}\s+Allocated with name "
                              r"\S+\s*$", asm_text, re.M):
                    return []
                line = next((line.strip() for line in asm_text.splitlines()
                             if line.startswith(f";{symbol}")), "<missing>")
                raise AssertionError(
                    f"mulreg probe has no allocation comment for '{symbol}' "
                    f"(line: {line})")
            return match.group(1).split()

        production_asm = compile_mulreg(args.sdcc, "production")
        active_regs = allocation_for(production_asm, "p")
        active_high = [r for r in active_regs
                       if 8 <= int(r[1:]) <= 15]
        if len(active_regs) == 4 and len(active_high) == 4:
            raise AssertionError(
                "native-MUL exclusion violated: product 'p' allocated "
                f"entirely in R8-R15 ({' '.join(active_regs)})")

        # Compile the same scalar/native-MUL probe with an explicit force
        # override.  This is the positive proof that the selected callback
        # reaches the real ralloc2 implementation; baseline/extras remain
        # hybrid fixtures because their documented calls/ISR/stack paths are
        # intentionally fail-closed to the legacy allocator.
        actual_workdir = tdp / "actual"
        actual_workdir.mkdir()
        actual = build_directed_sdcc(
            args.ralloc2_source, args.sdcc, actual_workdir,
            ralloc2_defines="-DMCS251_RALLOC2_FORCE")
        actual_asm = compile_mulreg(actual, "forced")
        actual_regs = allocation_for(actual_asm, "p")
        actual_high = [r for r in actual_regs
                       if 8 <= int(r[1:]) <= 15]
        if len(actual_regs) == 4 and len(actual_high) == 4:
            raise AssertionError(
                "forced ralloc2 native-MUL exclusion violated: product 'p' "
                f"allocated entirely in R8-R15 ({' '.join(actual_regs)})")
        if normalize_asm(production_asm) != normalize_asm(actual_asm):
            raise AssertionError(
                "production and forced-ralloc2 mulreg assembly differ; "
                "the default callback is not proven to select the actual "
                "ralloc2 subset")
        print("PASS production default matches forced actual ralloc2 "
              "assembly (native-MUL exclusion active)")
        production_ihx = execute_mulreg(args.sdcc, "production")
        forced_ihx = execute_mulreg(actual, "forced")
        if production_ihx != forced_ihx:
            raise AssertionError(
                "production and forced-ralloc2 mulreg IHX differ: "
                f"{production_ihx} != {forced_ihx}")

        production_pointer_asm = compile_mulreg(
            args.sdcc, "pointer-production", pointer_flags)
        forced_pointer_asm = compile_mulreg(
            actual, "pointer-forced", pointer_flags)
        for suffix, assembly in (("production", production_pointer_asm),
                                 ("forced", forced_pointer_asm)):
            if not re.search(r"^;pointer\s+Allocated to registers\s*$",
                             assembly, re.M):
                raise AssertionError(
                    f"{suffix} ralloc2 pointer value was not fail-closed spilled")
            if not re.search(r"^;sloc\d+\s+Allocated with name "
                             r"'_main_sloc\d+_\d+_\d+'\s*$",
                             assembly, re.M):
                raise AssertionError(
                    f"{suffix} ralloc2 pointer value has no spill slot")
        if normalize_asm(production_pointer_asm) != \
                normalize_asm(forced_pointer_asm):
            raise AssertionError(
                "production and forced-ralloc2 pointer-spill assembly differ; "
                "the production probe did not reach actual ralloc2")
        production_pointer_ihx = execute_pointer(
            args.sdcc, "pointer-production")
        forced_pointer_ihx = execute_pointer(actual, "pointer-forced")
        if production_pointer_ihx != forced_pointer_ihx:
            raise AssertionError(
                "production and forced-ralloc2 pointer-spill IHX differ: "
                f"{production_pointer_ihx} != {forced_pointer_ihx}")

        mutated_workdir = tdp / "mutated"
        mutated_workdir.mkdir()
        mutated = build_directed_sdcc(
            args.ralloc2_source, args.sdcc, mutated_workdir,
            ralloc2_defines=("-DMCS251_RALLOC2_FORCE "
                             "-DMCS251_RALLOC2_DISABLE_NATIVE_MUL_EXCLUSION"))
        mutated_regs = allocation_for(compile_mulreg(mutated, "mutated"),
                                      "p")
        mutated_high = [r for r in mutated_regs
                        if 8 <= int(r[1:]) <= 15]
        if not (len(mutated_regs) == 4 and len(mutated_high) == 4):
            raise AssertionError(
                "native-MUL mutation did not reach the forbidden high-byte "
                "fallback: "
                f"product 'p' allocation is {' '.join(mutated_regs) or 'stack'}")
        print("PASS native-MUL exclusion tripwire (active avoids R8-R15; "
              "disabled mutation allocates the forbidden high-byte fallback)")

    print("PASS: all MT-1C directed-path fixtures execute correctly through "
          "the MT-1D selected callback (ralloc2/fail-closed legacy) in three "
          "memory models")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
