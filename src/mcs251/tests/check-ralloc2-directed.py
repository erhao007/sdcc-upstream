#!/usr/bin/env python3
"""MT-1C ralloc2 directed-path gate.

Builds a temporary sdcc from the current build objects, replacing only
the MCS-251 port table's allocator callback with
mcs251_ralloc2_assignRegisters (all other objects are the actual current
build artifacts; ralloc2.cc is rebuilt into the temporary archive for each
variant; no user-visible option exists).
The production sdcc must keep linking the legacy allocator only.

The directed compiler then FULLY compiles two self-checking fixtures:

  - ralloc-baseline.c (the MT-1A risk catalogue: byte/word/dword
    pressure, overlap, values live across calls, forced spilling,
    reentrant recursion, soft-float with live integers, a non-leaf ISR
    and native 16x16->32 multiplies), and
  - ralloc2-extras.c (setjmp/longjmp, pointer post-increment walk,
    switch, aggregate by-value/hidden return, bit scalars, shifts,
    and same-function R0/R1 pointer-scratch pressure)

The gate also compiles ralloc2-mulreg.c to assembly twice: the normal
allocator must keep the product out of R8..R15, while a compiler rebuilt
with MCS251_RALLOC2_DISABLE_NATIVE_MUL_EXCLUSION must place that same
product in the forbidden high-byte fallback.  This mutation pair prevents
the exclusion check from becoming an allocation-comment false positive.

in model-small, model-large and stack-auto, links IHX images with the
real assembler/linker, and executes them in uCsim.  Every image must
report PASS (0x55) through the fixtures' control block; anything else
(fail verdict, missing verdict, simulator crash) fails the gate.
"""

import argparse
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
        "-Dmcs251_assignRegisters=mcs251_ralloc2_assignRegisters "
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
    if (defines_symbol(production_nm, "mcs251_ralloc2_cc") or
            defines_symbol(production_nm,
                           "mcs251_ralloc2_assignRegisters")):
        raise AssertionError(
            "production sdcc unexpectedly links the ralloc2 path")
    if not defines_symbol(production_nm, "mcs251_assignRegisters"):
        raise AssertionError(
            "production sdcc does not define the legacy allocator entry")
    print("PASS directed sdcc links real ralloc2; production sdcc keeps "
          "legacy allocator only")
    return directed_sdcc


def simulate(s51, ihx_path):
    console = (
        "set error unknown_code on\n"
        "set opt selfjump_stop 0\n"
        f"step {STEPS} vclk\n"
        f"dump iram {STATUS_AT:#04x} {STATUS_AT + 7:#04x}\n"
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
    m = re.search(rf"^[ \t]*0x{STATUS_AT:02x}[ \t]+"
                  r"((?:[0-9a-fA-F]{2}[ \t]+)+)", out, re.M)
    if not m:
        raise AssertionError(
            f"{ihx_path.name}: control block not dumped "
            f"(exit {completed.returncode}, snippet: {out[-300:]})")
    bytes_dump = m.group(1).split()
    return int(bytes_dump[0], 16), bytes_dump


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
                print(f"PASS {name} [{model}] via real ralloc2 "
                      "(compile+link+ucsim, 0x55)")

        # Native-MUL exclusion tripwire.  The probe keeps two live dwords
        # in the two bank DR tuples.  With exclusion active, the product
        # must not take an all-R8..R15 allocation.  A second compiler is built
        # with the exclusion deliberately disabled; that mutation must
        # move the product into the high-byte fallback, proving the gate is live
        # rather than merely checking an incidental allocation.
        stem = "fx-mulreg"
        shutil.copy(args.mulreg_source, tdp / f"{stem}.c")
        def compile_mulreg(compiler, suffix):
            asm_path = tdp / f"{stem}-{suffix}.asm"
            completed = run(
                [str(compiler), "-mmcs251", "--model-small", "-S",
                 "-o", str(asm_path), str(tdp / f"{stem}.c")],
                cwd=tdp / "bin")
            if completed.returncode != 0:
                raise AssertionError(
                    f"mulreg probe ({suffix}) compilation failed:\n"
                    f"{(completed.stdout + completed.stderr)[-500:]}")
            return asm_path.read_text()

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

        active_regs = allocation_for(compile_mulreg(directed, "active"), "p")
        active_high = [r for r in active_regs
                       if 8 <= int(r[1:]) <= 15]
        if len(active_regs) == 4 and len(active_high) == 4:
            raise AssertionError(
                "native-MUL exclusion violated: product 'p' allocated "
                f"entirely in R8-R15 ({' '.join(active_regs)})")

        mutated_workdir = tdp / "mutated"
        mutated_workdir.mkdir()
        mutated = build_directed_sdcc(
            args.ralloc2_source, args.sdcc, mutated_workdir,
            ralloc2_defines="-DMCS251_RALLOC2_DISABLE_NATIVE_MUL_EXCLUSION")
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

    print("PASS: all MT-1C directed-path fixtures execute correctly "
          "through the real ralloc2 allocator in three memory models")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
