#!/usr/bin/env python3
"""MT-1E Phase 2B ralloc2 directed-path gate (retained fixtures).

Builds a temporary sdcc from the current build objects, rebuilding the
MCS-251 port callback, ralloc2.cc, and ralloc2 support from the current
sources (all other objects are the actual current build artifacts; no
user-visible option exists). The production sdcc must expose only the
ralloc2 callback.

The directed compiler then FULLY compiles the self-checking fixtures through
the MT-1E ralloc2 callback. Production routes every function through ralloc2:

  - ralloc-baseline.c (the MT-1A risk catalogue: byte/word/dword
    pressure, overlap, values live across calls, forced spilling,
    reentrant recursion, soft-float with live integers, a non-leaf ISR
    and native 16x16->32 multiplies), and
  - ralloc2-extras.c (setjmp/longjmp, pointer post-increment walk,
    switch, aggregate by-value/hidden return, bit scalars, shifts,
    and same-function R0/R1 pointer-scratch pressure)

The gate also compiles and executes ralloc2-mulreg.c with the production
compiler and a compiler rebuilt from the same ralloc2 source. Their
normalized assembly must match, proving that the default callback reaches the
actual ralloc2 implementation.  The same fixture proves that a pointer value
stays register-resident and keeps all 24 arithmetic bits.  A third
compiler rebuilt with the native-MUL exclusion disabled must place the product
in the forbidden R8..R15 fallback; this mutation keeps the allocation tripwire
live.

in model-small, model-large and stack-auto, links IHX images with the
real assembler/linker, and executes them in uCsim.  Every image must
report PASS (0x55) through the fixtures' control block; anything else
(fail verdict, missing verdict, simulator crash) fails the gate.

The Class 4 extension additionally checks that ordinary stack-auto,
__reentrant and non-leaf ISR functions emit test-only ralloc2 route markers,
and that independent disabled-capability mutations fail closed.
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
EXTRAS_STATUS_AT = 0x010100
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


def _host_env():
    """Keep SDCC's cc1 search path away from native GCC helper commands."""
    env = os.environ.copy()
    env.pop("COMPILER_PATH", None)
    return env


def _host_tool(name):
    """Use the native MSYS2 tool, not the MSYS compatibility wrapper."""
    if os.name == "nt":
        prefixes = []
        if os.environ.get("MSYSTEM_PREFIX"):
            prefixes.append(Path(os.environ["MSYSTEM_PREFIX"]) / "bin")
        prefixes.append(Path(os.environ.get("MSYS2_ROOT", r"C:\msys64")) /
                        "ucrt64" / "bin")
        for prefix in prefixes:
            candidate = prefix / f"{name}.exe"
            if candidate.exists():
                return str(candidate)
    return name


def defines_symbol(nm_text, symbol):
    return re.search(rf"\b[Tt]\s+_?{re.escape(symbol)}$",
                     nm_text, re.M) is not None


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _mirror(link_path, target):
    """ST-6B-W portability: symlink creation needs privileges on Windows;
    fall back to a file copy or a directory junction (no admin required)."""
    try:
        link_path.symlink_to(target)
        return
    except OSError:
        if os.name != "nt":
            raise
    if target.is_dir():
        subprocess.run(["cmd", "/c", "mklink", "/J", str(link_path), str(target)],
                       check=True, capture_output=True)
    else:
        shutil.copy2(target, link_path)


def build_directed_sdcc(ralloc2_source, sdcc, workdir,
                        ralloc2_defines="", gen_defines=""):
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
    gen_source = ralloc2_source.parent / "gen.c"
    support_source = ralloc2_source.parent / "ralloc2_support.c"
    for required in (build_src / "Makefile", port_archive, main_source,
                     gen_source, support_source):
        if not required.exists():
            raise AssertionError(f"missing directed-link input: {required}")

    directed_main = workdir / "main.o"
    directed_port = workdir / "mcs251-directed.a"
    directed_sdcc = workdir / "bin" / "sdcc"
    directed_sdcc.parent.mkdir(exist_ok=True)
    extra_makefile = workdir / "ralloc2-directed.mk"
    extra_makefile.write_text(
        ".PHONY: ralloc2-directed-main ralloc2-directed-ralloc2 "
        "ralloc2-directed-support ralloc2-directed-gen "
        "ralloc2-directed-link\n"
        "ralloc2-directed-main:\n"
        "\t$(CC) $(CFLAGS) $(CPPFLAGS) -Imcs251 "
        "-c $(MCS251_MAIN) -o $(DIRECTED_MAIN)\n"
        "ralloc2-directed-ralloc2:\n"
        "\t$(CXX) $(CXXFLAGS) $(CPPFLAGS) "
        "$(MCS251_RALLOC2_DEFINES) -c $(MCS251_RALLOC2) "
        "-o $(DIRECTED_RALLOC2)\n"
        "ralloc2-directed-support:\n"
        "\t$(CC) $(CFLAGS) $(CPPFLAGS) -c $(MCS251_SUPPORT) "
        "-o $(DIRECTED_SUPPORT)\n"
        "ralloc2-directed-gen:\n"
        "\t$(CC) $(CFLAGS) $(CPPFLAGS) $(MCS251_GEN_DEFINES) "
        "-c $(MCS251_GEN) -o $(DIRECTED_GEN)\n"
        "ralloc2-directed-link:\n"
        "\t$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $(DIRECTED_SDCC) "
        "$(SLIBOBJS) $(OBJECTS) "
        "$(filter-out mcs251/port.a,$(PORT_LIBS)) "
        "$(DIRECTED_PORT) $(LIBDIRS) $(LIBS)\n")

    make_base = ["make", "-s", "-f", "Makefile", "-f", str(extra_makefile)]
    directed_ralloc2 = workdir / "ralloc2.o"
    directed_support = workdir / "ralloc2_support.o"
    directed_gen = workdir / "gen.o"
    run_ok([*make_base, "ralloc2-directed-main",
            f"MCS251_MAIN={main_source}",
            f"DIRECTED_MAIN={directed_main}"], cwd=build_src,
            env=_host_env())
    run_ok([*make_base, "ralloc2-directed-ralloc2",
            f"MCS251_RALLOC2={ralloc2_source}",
            f"DIRECTED_RALLOC2={directed_ralloc2}",
            f"MCS251_RALLOC2_DEFINES={ralloc2_defines}"], cwd=build_src,
            env=_host_env())
    run_ok([*make_base, "ralloc2-directed-support",
            f"MCS251_SUPPORT={support_source}",
            f"DIRECTED_SUPPORT={directed_support}"], cwd=build_src,
           env=_host_env())
    if gen_defines:
        run_ok([*make_base, "ralloc2-directed-gen",
                f"MCS251_GEN={gen_source}",
                f"DIRECTED_GEN={directed_gen}",
                f"MCS251_GEN_DEFINES={gen_defines}"], cwd=build_src,
               env=_host_env())
    shutil.copy(port_archive, directed_port)
    ar = _host_tool("ar")
    run_ok([ar, "d", str(directed_port), "ralloc2.o"], env=_host_env())
    run_ok([ar, "d", str(directed_port), "ralloc2_support.o"],
           env=_host_env())
    if gen_defines:
        run_ok([ar, "d", str(directed_port), "gen.o"], env=_host_env())
    run_ok([ar, "rcs", str(directed_port), str(directed_main),
            str(directed_ralloc2), str(directed_support)], env=_host_env())
    if gen_defines:
        run_ok([ar, "rcs", str(directed_port), str(directed_gen)],
               env=_host_env())
    run_ok([*make_base, "ralloc2-directed-link",
            f"DIRECTED_PORT={directed_port}",
            f"DIRECTED_SDCC={directed_sdcc}"], cwd=build_src,
            env=_host_env())

    # ST-6B-W portability: the Windows linker appends .exe to -o targets.
    if not directed_sdcc.exists():
        exe_sibling = Path(str(directed_sdcc) + ".exe")
        if exe_sibling.exists():
            directed_sdcc = exe_sibling

    # SDCC discovers its sibling tools (sdcpp/sdas251/sdld) and its
    # library share relative to the binary, so mirror them.
    for tool in ("sdcpp", "sdas251", "sdld"):
        src = sdcc.parent / tool
        if not src.exists():
            if os.name == "nt" and Path(str(src) + ".exe").exists():
                src = Path(str(src) + ".exe")
            else:
                raise AssertionError(f"missing tool beside sdcc: {src}")
        _mirror(workdir / "bin" / tool, src)
    share = build_src.parent / "install" / "share"
    if not share.exists():
        raise AssertionError(f"missing install share tree: {share}")
    if not (workdir / "share").exists():
        _mirror(workdir / "share", share)

    nm_out = run_ok([_host_tool("nm"), "-g", str(directed_sdcc)],
                    env=_host_env())
    for symbol in ("mcs251_ralloc2_cc", "mcs251_ralloc2_assignRegisters"):
        if not defines_symbol(nm_out, symbol):
            raise AssertionError(
                f"directed sdcc did not link actual {symbol}")
    production_nm = run_ok([_host_tool("nm"), "-g", str(build_src / "sdcc")])
    for symbol in ("mcs251_ralloc2_cc", "mcs251_ralloc2_assignRegisters"):
        if not defines_symbol(production_nm, symbol):
            raise AssertionError(
                f"production sdcc does not define selected ralloc2 entry "
                f"{symbol}")
    if defines_symbol(nm_out, "mcs251_assignRegisters") or \
            defines_symbol(production_nm, "mcs251_assignRegisters"):
        raise AssertionError(
            "directed or production sdcc still defines the removed legacy "
            "allocator entry")
    print("PASS directed and production sdcc link the selected ralloc2 "
          "entry; legacy allocator is absent")
    return directed_sdcc


def simulate(s51, ihx_path, expected_xdata=None, require_status=True,
             status_at=STATUS_AT, status_space="iram"):
    xdata_dump = ""
    if expected_xdata is not None:
        address, expected = expected_xdata
        address_space = "xdata" if address >= 0x10000 else "xram"
        xdata_dump = (f"dump {address_space} {address:#07x} "
                      f"{address + len(expected) - 1:#07x}\n")
    status_dump = (f"dump {status_space} {status_at:#07x} "
                   f"{status_at + 7:#07x}\n"
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
        m = re.search(rf"^[ \t]*0x0*{status_at:x}[ \t]+"
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
    ap.add_argument("--aggregate-source", required=True, type=Path)
    ap.add_argument("--call-source", required=True, type=Path)
    ap.add_argument("--pressure-source", required=True, type=Path)
    ap.add_argument("--wide-source", required=True, type=Path)
    ap.add_argument("--phase2a-source", required=True, type=Path)
    ap.add_argument("--spill-copy-source", required=True, type=Path)
    ap.add_argument("--ckd-add-source", required=True, type=Path)
    ap.add_argument("--printf-large-source", required=True, type=Path)
    args = ap.parse_args()

    for tool in (args.sdcc, args.s51, args.ralloc2_source,
                 args.baseline_source, args.extras_source,
                 args.mulreg_source, args.aggregate_source,
                 args.call_source, args.pressure_source, args.wide_source,
                 args.phase2a_source, args.spill_copy_source,
                 args.ckd_add_source,
                 args.printf_large_source):
        if not tool.exists():
            raise AssertionError(f"missing input: {tool}")
    args.sdcc = args.sdcc.resolve()
    args.s51 = args.s51.resolve()
    args.ralloc2_source = args.ralloc2_source.resolve()
    args.baseline_source = args.baseline_source.resolve()
    args.extras_source = args.extras_source.resolve()
    args.mulreg_source = args.mulreg_source.resolve()
    args.aggregate_source = args.aggregate_source.resolve()
    args.call_source = args.call_source.resolve()
    args.pressure_source = args.pressure_source.resolve()
    args.wide_source = args.wide_source.resolve()
    args.phase2a_source = args.phase2a_source.resolve()
    args.spill_copy_source = args.spill_copy_source.resolve()
    args.ckd_add_source = args.ckd_add_source.resolve()
    args.printf_large_source = args.printf_large_source.resolve()

    variants = [
        ("model-small", ["--model-small"]),
        ("model-large", ["--model-large"]),
        ("stack-auto", ["--stack-auto"]),
    ]
    fixtures = [
        ("baseline", args.baseline_source, "0x38", STATUS_AT, "iram"),
        # extras carries nine independent stress shapes.  Keep its verdict in
        # XDATA so the small model can use the complete post-bit-bank DATA
        # window; reserving 0x30..0x37 forced a structural OSEG overflow once
        # the four libraries were correctly rebuilt with the current backend.
        ("extras", args.extras_source, "0x30", EXTRAS_STATUS_AT, "xdata"),
        ("aggregate", args.aggregate_source, "0x38", STATUS_AT, "iram"),
    ]

    with tempfile.TemporaryDirectory(prefix="ralloc2-directed-") as td:
        tdp = Path(td)
        directed = build_directed_sdcc(args.ralloc2_source, args.sdcc, tdp)

        for name, source, data_loc, status_at, status_space in fixtures:
            stem = f"fx-{name}"
            shutil.copy(source, tdp / f"{stem}.c")
            for model, flags in variants:
                ihx = tdp / f"{stem}-{model}.ihx"
                completed = run(
                    [str(directed), "-mmcs251", *flags,
                     "--data-loc", data_loc, "-o", str(ihx),
                     str(tdp / f"{stem}.c")], cwd=tdp / "bin")
                output = completed.stdout + completed.stderr
                if completed.returncode != 0 or \
                        re.search(r"error|Undefined", output, re.I):
                    raise AssertionError(
                        f"{name}/{model}: directed compilation failed"
                        f"({completed.returncode}):\n{output[-800:]}")
                status, status_bytes = simulate(
                    args.s51, ihx, status_at=status_at,
                    status_space=status_space)
                if status != 0x55:
                    raise AssertionError(
                        f"{name}/{model}: directed ralloc2 image did not "
                        f"pass its self checks (status {status:#04x}, "
                        f"control block {' '.join(status_bytes)})")
                print(f"PASS {name} [{model}] via MT-1E Phase 2A ralloc2 callback "
                      "(compile+link+ucsim, "
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
                [str(compiler), "-mmcs251", "--model-small", *extra_flags,
                 "--data-loc", "0x38", "-S",
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
                    f"pointer-register probe ({suffix}) link failed:\n"
                    f"{output[-800:]}")
            pointer_address = map_symbol_address(ihx.with_suffix(".map"),
                                                 "g_pointer_sink")
            simulate(args.s51, ihx,
                     expected_xdata=(pointer_address, [0x01, 0x03, 0x05]),
                     require_status=False)
            digest = sha256(ihx)
            print(f"PASS pointer-register [{suffix}] actual ralloc2 "
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

        # Compile the same scalar/native-MUL probe from the current ralloc2
        # source as an independent production comparison.
        actual_workdir = tdp / "actual"
        actual_workdir.mkdir()
        actual = build_directed_sdcc(
            args.ralloc2_source, args.sdcc, actual_workdir)
        trace_workdir = tdp / "trace"
        trace_workdir.mkdir()
        trace = build_directed_sdcc(
            args.ralloc2_source, args.sdcc, trace_workdir,
            ralloc2_defines="-DMCS251_RALLOC2_TRACE_SELECTION")
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
                    f"{suffix} ralloc2 pointer value did not take the "
                    "register path")
            if re.search(r"^;sloc\d+\s+Allocated with name "
                         r"'_main_sloc\d+_\d+_\d+'\s*$",
                         assembly, re.M):
                raise AssertionError(
                    f"{suffix} ralloc2 pointer value unexpectedly has a "
                    "spill slot")
            for reg in ("r7", "r6", "r5"):
                if not re.search(rf"^\s*mov\s+{reg},a\s*$", assembly,
                                 re.I | re.M):
                    raise AssertionError(
                        f"{suffix} ralloc2 pointer value did not materialise "
                        f"its 24-bit register tuple ({reg} missing)")
        if normalize_asm(production_pointer_asm) != \
                normalize_asm(forced_pointer_asm):
            raise AssertionError(
                "production and forced-ralloc2 pointer-register assembly "
                "differ; "
                "the production probe did not reach actual ralloc2")
        production_pointer_ihx = execute_pointer(
            args.sdcc, "pointer-production")
        forced_pointer_ihx = execute_pointer(actual, "pointer-forced")
        if production_pointer_ihx != forced_pointer_ihx:
            raise AssertionError(
                "production and forced-ralloc2 pointer-register IHX differ: "
                f"{production_pointer_ihx} != {forced_pointer_ihx}")

        # MT-1E: aggregate and bit-valued live ranges no longer
        # force the legacy fallback in production.  For each of the
        # five probe modes (struct/bitfield/bitptr/ptrifx/ptrvar) the
        # production and forced compilers must
        # emit identical assembly and IHX in model-small and model-large;
        # stack-auto/reentrant/ISR are now covered by the Class 4 route gate;
        # both
        # images must self-check 0x55 in uCsim and leave the mode's
        # sink value in XDATA.
        agg_stem = "fx-aggregate"
        shutil.copy(args.aggregate_source, tdp / f"{agg_stem}.c")
        agg_modes = (
            ("struct", (), [0x00, 0x00, 0x12, 0xAE]),
            ("bitfield", ("-DMCS251_RALLOC2_AGG_BITFIELD",),
             [0x00, 0x0A, 0xBC, 0x0D]),
            ("bitptr", ("-DMCS251_RALLOC2_AGG_BITPTR",),
             [0x00, 0x00, 0x48, 0x65]),
            ("ptrifx", ("-DMCS251_RALLOC2_AGG_PTRIFX",),
             [0x00, 0x00, 0x00, 0x24]),
            ("ptrvar", ("-DMCS251_RALLOC2_AGG_PTRVAR",),
             [0x00, 0x00, 0x00, 0x5A]),
        )

        def compile_probe(compiler, stem, suffix, flags=(),
                          expected_routes=None, forbidden_routes=None,
                          expect_failure=False):
            asm_path = tdp / f"{stem}-{suffix}.asm"
            completed = subprocess.run(
                [str(compiler), "-mmcs251", *flags, "-S",
                 "-o", str(asm_path), str(tdp / f"{stem}.c")],
                cwd=tdp / "bin", capture_output=True, text=True,
                errors="replace")
            output = completed.stdout + completed.stderr
            if expect_failure:
                if completed.returncode == 0:
                    raise AssertionError(
                        f"{stem} ({suffix}) mutation unexpectedly compiled")
                print(f"PASS {stem} ({suffix}) mutation fails closed "
                      f"({output[-180:].strip()})")
                return ""
            if completed.returncode != 0:
                raise AssertionError(
                    f"{stem} ({suffix}) compilation failed:\n"
                    f"{output[-500:]}")
            if expected_routes is not None or forbidden_routes is not None:
                selected = {
                    line.split(":", 1)[1]
                    for line in output.splitlines()
                    if line.startswith("MCS251_RALLOC2_SELECTED:")
                }

            if expected_routes is not None:
                if isinstance(expected_routes, str):
                    expected_routes = (expected_routes,)
                missing = sorted(set(expected_routes) - selected)
                if missing:
                    raise AssertionError(
                        f"{stem} ({suffix}) did not select ralloc2 for "
                        f"{', '.join(missing)}; route marker missing")

            if forbidden_routes is not None:
                forbidden = sorted(set(forbidden_routes) & selected)
                if forbidden:
                    raise AssertionError(
                        f"{stem} ({suffix}) unexpectedly selected ralloc2 for "
                        f"{', '.join(forbidden)}; mutation is not live")
            return asm_path.read_text()

        # MT-1E Class 5: pressure beyond the former fail-closed boundary must
        # select the real ralloc2 callback.  Compare production with the
        # explicit-force compiler in all three memory models and execute both
        # images through the real assembler/linker/uCsim path.
        pressure_stem = "fx-class5-pressure"
        wide_stem = "fx-class5-wide"
        shutil.copy(args.pressure_source, tdp / f"{pressure_stem}.c")
        shutil.copy(args.wide_source, tdp / f"{wide_stem}.c")

        def execute_class5(compiler, stem, suffix, flags, sink_symbol,
                           sink_bytes):
            ihx = tdp / f"{stem}-{suffix}.ihx"
            completed = run(
                [str(compiler), "-mmcs251", *flags,
                 "--data-loc", "0x38", "-o", str(ihx),
                 str(tdp / f"{stem}.c")], cwd=tdp / "bin")
            output = completed.stdout + completed.stderr
            if completed.returncode != 0 or \
                    re.search(r"error|Undefined", output, re.I):
                raise AssertionError(
                    f"class-5 probe ({stem}/{suffix}) link failed:\n"
                    f"{output[-800:]}")
            sink_address = map_symbol_address(ihx.with_suffix(".map"),
                                              sink_symbol)
            status, _ = simulate(args.s51, ihx,
                                 expected_xdata=(sink_address, sink_bytes))
            if status != 0x55:
                raise AssertionError(
                    f"class-5 probe ({stem}/{suffix}) did not self-check "
                    f"(status {status:#04x})")
            return sha256(ihx)

        class5_modes = (
            ("pressure", pressure_stem, "c5_sink",
             [0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88],
             ("main",), ("model-small", "model-large", "stack-auto")),
            ("wide", wide_stem, "c5w_sink",
             [0x31, 0x32, 0x33, 0x34],
             ("c5w_step", "main"), ("model-small", "model-large")),
        )
        for class5_name, class5_fixture, sink_symbol, sink_bytes, routes, \
                class5_models \
                in class5_modes:
            for class5_model in class5_models:
                class5_flags = (f"--{class5_model}",)
                production_class5_asm = compile_probe(
                    args.sdcc, class5_fixture,
                    f"production-{class5_name}-{class5_model}",
                    class5_flags)
                forced_class5_asm = compile_probe(
                    actual, class5_fixture,
                    f"forced-{class5_name}-{class5_model}",
                    class5_flags)
                compile_probe(
                    trace, class5_fixture,
                    f"trace-{class5_name}-{class5_model}",
                    class5_flags, expected_routes=routes)
                if normalize_asm(production_class5_asm) != \
                        normalize_asm(forced_class5_asm):
                    raise AssertionError(
                        f"class-5 {class5_name}/{class5_model}: production "
                        "and forced-ralloc2 assembly differ")
                production_class5_ihx = execute_class5(
                    args.sdcc, class5_fixture,
                    f"production-{class5_name}-{class5_model}",
                    class5_flags, sink_symbol, sink_bytes)
                forced_class5_ihx = execute_class5(
                    actual, class5_fixture,
                    f"forced-{class5_name}-{class5_model}",
                    class5_flags, sink_symbol, sink_bytes)
                if production_class5_ihx != forced_class5_ihx:
                    raise AssertionError(
                        f"class-5 {class5_name}/{class5_model}: production "
                        "and forced-ralloc2 IHX differ")
                print(f"PASS class-5 [{class5_name}/{class5_model}] "
                      "production ralloc2 (compile+link+ucsim, 0x55; "
                      f"sha256={production_class5_ihx})")

        pressure_mut_workdir = tdp / "pressure-mut"
        pressure_mut_workdir.mkdir()
        pressure_mut = build_directed_sdcc(
            args.ralloc2_source, args.sdcc, pressure_mut_workdir,
            ralloc2_defines=("-DMCS251_RALLOC2_TRACE_SELECTION "
                             "-DMCS251_RALLOC2_DISABLE_PRESSURE_SUPPORT"))
        compile_probe(
            pressure_mut, pressure_stem, "pressure-fallback-mutation",
            ("--model-small",), expect_failure=True)
        print("PASS class-5 pressure mutation is rejected without a "
              "legacy fallback")

        # MT-1E Phase 2A: every formerly retained production fallback must
        # now select ralloc2.  One fixture keeps the retained code shapes
        # named, including a separate pressure variant of pointer compare,
        # so independent mutations can prove each route assertion is live.
        phase2a_stem = "fx-phase2a"
        shutil.copy(args.phase2a_source, tdp / f"{phase2a_stem}.c")
        phase2a_routes = (
            "p2a_control_path",
            "p2a_switch_path",
            "p2a_stack_address_path",
            "p2a_generic_cast_path",
            "p2a_pointer_compare_path",
            "p2a_pointer_compare_pressure_path",
            "p2a_pdata_path",
            "p2a_pointer_layout_path",
            "main",
        )
        phase2a_control_routes = ("p2a_control_path",)
        phase2a_modes = (
            ("combined", ("--model-large", "--stack-auto",
                          "--data-loc", "0x38")),
            ("low-data", ("--model-small", "--data-loc", "0x10")),
            ("nostdinc", ("--model-small", "--nostdinc")),
        )

        def execute_phase2a(compiler, suffix, flags):
            ihx = tdp / f"{phase2a_stem}-{suffix}.ihx"
            completed = run(
                [str(compiler), "-mmcs251", *flags, "-o", str(ihx),
                 str(tdp / f"{phase2a_stem}.c")], cwd=tdp / "bin")
            output = completed.stdout + completed.stderr
            if completed.returncode != 0 or \
                    re.search(r"error|Undefined", output, re.I):
                raise AssertionError(
                    f"phase-2A probe ({suffix}) link failed:\n"
                    f"{output[-800:]}")
            status, _ = simulate(args.s51, ihx)
            if status != 0x55:
                raise AssertionError(
                    f"phase-2A probe ({suffix}) did not self-check "
                    f"(status {status:#04x})")
            return sha256(ihx)

        def assert_pointer_compare_spill_shape(assembly, suffix):
            """Keep the regression tied to two spilled compare operands."""
            allocation = re.search(
                r";Allocation info for local variables in function "
                r"'p2a_pointer_compare_pressure_path'\n"
                r"(?P<allocation>.*?)(?=\n;[^\n]*"
                r"p2a_pointer_compare_pressure_path \(void\))",
                assembly, re.S)
            if not allocation:
                raise AssertionError(
                    f"phase-2A {suffix}: pointer-pressure allocation block "
                    "is missing")
            allocation = allocation.group("allocation")
            spilled = [name for name in ("p2", "p3", "p4", "p5")
                       if re.search(
                           rf"^;{name}\s+Allocated (?:to stack|with name)",
                           allocation, re.M)]
            if not all(name in spilled for name in ("p4", "p5")) or \
                    len(spilled) < 2:
                raise AssertionError(
                    f"phase-2A {suffix}: expected at least two spilled "
                    f"pointer temporaries including p4/p5, got {spilled}")

        for phase2a_mode, phase2a_flags in phase2a_modes:
            production_phase2a_asm = compile_probe(
                args.sdcc, phase2a_stem,
                f"production-{phase2a_mode}", phase2a_flags)
            forced_phase2a_asm = compile_probe(
                actual, phase2a_stem,
                f"forced-{phase2a_mode}", phase2a_flags)
            assert_pointer_compare_spill_shape(
                production_phase2a_asm, f"production-{phase2a_mode}")
            assert_pointer_compare_spill_shape(
                forced_phase2a_asm, f"forced-{phase2a_mode}")
            compile_probe(
                trace, phase2a_stem, f"trace-{phase2a_mode}",
                phase2a_flags, expected_routes=phase2a_routes)
            if normalize_asm(production_phase2a_asm) != \
                    normalize_asm(forced_phase2a_asm):
                raise AssertionError(
                    f"phase-2A {phase2a_mode}: production and forced-ralloc2 "
                    "assembly differ")
            production_phase2a_ihx = execute_phase2a(
                args.sdcc, f"production-{phase2a_mode}", phase2a_flags)
            forced_phase2a_ihx = execute_phase2a(
                actual, f"forced-{phase2a_mode}", phase2a_flags)
            if production_phase2a_ihx != forced_phase2a_ihx:
                raise AssertionError(
                    f"phase-2A {phase2a_mode}: production and forced-ralloc2 "
                    "IHX differ")
            print(f"PASS phase-2A [{phase2a_mode}] production ralloc2 "
                  "(compile+link+ucsim, 0x55; "
                  f"sha256={production_phase2a_ihx})")

        # Seven live words force p2a_fixed_carry_path's addend into the fixed
        # R8..R15 bank.  Production routes that byte through B; disabling
        # only the fixed-register accumulator lowering must restore the
        # inherited peephole's unencodable ADDC A,R8..R15 form and fail
        # assembly.
        fixedcarry_mut_workdir = tdp / "phase2a-fixed-carry-mut"
        fixedcarry_mut_workdir.mkdir()
        fixedcarry_mut = build_directed_sdcc(
            args.ralloc2_source, args.sdcc, fixedcarry_mut_workdir,
            gen_defines=("-DMCS251_RALLOC2_DISABLE_FIXED_REGISTER_"
                         "ACCUMULATOR_LOWERING"))
        fixedcarry_rel = tdp / f"{phase2a_stem}-fixed-carry-mutation.rel"
        completed = run(
            [str(fixedcarry_mut), "-mmcs251", *phase2a_modes[0][1],
             "-c", "-o", str(fixedcarry_rel),
             str(tdp / f"{phase2a_stem}.c")], cwd=tdp / "bin")
        fixedcarry_output = completed.stdout + completed.stderr
        if completed.returncode == 0 or not re.search(
                r"ADDC/SUBB register source must be R0 through R7",
                fixedcarry_output):
            raise AssertionError(
                "fixed-register accumulator mutation did not restore the unencodable "
                "ADDC A,R8..R15 form:\n"
                f"{fixedcarry_output[-800:]}")
        print("PASS Phase 2A fixed-register accumulator tripwire "
              "(lowering-disabled high-byte carry is rejected by the assembler)")

        phase2a_mutations = (
            ("jumptable", "MCS251_RALLOC2_DISABLE_JUMPTABLE_SUPPORT",
             phase2a_modes[0][1], ("p2a_switch_path",)),
            ("inlineasm", "MCS251_RALLOC2_DISABLE_INLINEASM_SUPPORT",
             phase2a_modes[0][1], ("main",)),
            ("pdata", "MCS251_RALLOC2_DISABLE_PDATA_SUPPORT",
             phase2a_modes[0][1], ("p2a_pdata_path",)),
            ("stack-address", "MCS251_RALLOC2_DISABLE_STACK_ADDRESS_SUPPORT",
             phase2a_modes[0][1], ("p2a_stack_address_path",)),
            ("generic-cast", "MCS251_RALLOC2_DISABLE_GENERIC_CAST_SUPPORT",
             phase2a_modes[0][1], ("p2a_generic_cast_path",)),
            ("pointer-compare",
             "MCS251_RALLOC2_DISABLE_POINTER_COMPARE_SUPPORT",
             phase2a_modes[0][1],
             ("p2a_pointer_compare_path",
              "p2a_pointer_compare_pressure_path")),
        )
        for mutation_name, mutation_macro, mutation_flags, forbidden in \
                phase2a_mutations:
            mutation_workdir = tdp / f"phase2a-{mutation_name}-mut"
            mutation_workdir.mkdir()
            mutation_compiler = build_directed_sdcc(
                args.ralloc2_source, args.sdcc, mutation_workdir,
                ralloc2_defines=("-DMCS251_RALLOC2_TRACE_SELECTION "
                                 f"-D{mutation_macro}"))
            compile_probe(
                mutation_compiler, phase2a_stem,
                f"phase2a-{mutation_name}-mutation", mutation_flags,
                expect_failure=True)

        low_data_mut_workdir = tdp / "phase2a-low-data-mut"
        low_data_mut_workdir.mkdir()
        low_data_mut = build_directed_sdcc(
            args.ralloc2_source, args.sdcc, low_data_mut_workdir,
            ralloc2_defines=("-DMCS251_RALLOC2_TRACE_SELECTION "
                             "-DMCS251_RALLOC2_DISABLE_LOW_DATA_SUPPORT"))
        # The low-DATA mutation is rejected only for the low-data/nostdinc
        # modes; the combined mode remains a positive control.
        compile_probe(
            low_data_mut, phase2a_stem, "phase2a-low-data-control",
            phase2a_modes[0][1], expected_routes=phase2a_routes)
        for mode_name, mode_flags in phase2a_modes[1:]:
            compile_probe(
                low_data_mut, phase2a_stem,
                f"phase2a-{mode_name}-mutation", mode_flags,
                expect_failure=True)
        print("PASS Phase-2A disabled-capability mutations fail closed "
              "without legacy fallback")

        # The large-model gcc-torture pointer-pressure shape keeps five
        # generic pointers live across mixed-width arithmetic.  MCS-251
        # pointer triples may not cross the R0/R1-to-R8 boundary.  Disable
        # only that allocator placement rule: the same self-check must then
        # observe a bad result, proving the constraint is executable rather
        # than merely a source-level preference.
        pointer_layout_flags = ("--model-large", "--data-loc", "0x38")
        production_layout_asm = compile_probe(
            args.sdcc, phase2a_stem, "pointer-layout-production",
            pointer_layout_flags)
        forced_layout_asm = compile_probe(
            actual, phase2a_stem, "pointer-layout-forced",
            pointer_layout_flags)
        compile_probe(
            trace, phase2a_stem, "pointer-layout-trace",
            pointer_layout_flags, expected_routes=phase2a_routes)
        if normalize_asm(production_layout_asm) != \
                normalize_asm(forced_layout_asm):
            raise AssertionError(
                "pointer-layout production and forced-ralloc2 assembly "
                "differ")
        production_layout_ihx = execute_phase2a(
            args.sdcc, "pointer-layout-production", pointer_layout_flags)
        forced_layout_ihx = execute_phase2a(
            actual, "pointer-layout-forced", pointer_layout_flags)
        if production_layout_ihx != forced_layout_ihx:
            raise AssertionError(
                "pointer-layout production and forced-ralloc2 IHX differ")
        layout_mut_workdir = tdp / "phase2a-pointer-layout-mut"
        layout_mut_workdir.mkdir()
        layout_mut = build_directed_sdcc(
            args.ralloc2_source, args.sdcc, layout_mut_workdir,
            ralloc2_defines=("-DMCS251_RALLOC2_TRACE_SELECTION "
                             "-DMCS251_RALLOC2_DISABLE_POINTER_BANK_LAYOUT"))
        compile_probe(
            layout_mut, phase2a_stem, "phase2a-pointer-layout-mutation",
            pointer_layout_flags, expected_routes=phase2a_routes)
        layout_mut_ihx = tdp / f"{phase2a_stem}-pointer-layout-mutation.ihx"
        completed = run(
            [str(layout_mut), "-mmcs251", *pointer_layout_flags,
             "-o", str(layout_mut_ihx), str(tdp / f"{phase2a_stem}.c")],
            cwd=tdp / "bin")
        layout_mut_output = completed.stdout + completed.stderr
        if completed.returncode != 0 or \
                re.search(r"error|Undefined", layout_mut_output, re.I):
            raise AssertionError(
                "pointer-layout mutation did not compile/link:\n"
                f"{layout_mut_output[-800:]}")
        layout_mut_status, _ = simulate(args.s51, layout_mut_ihx)
        if layout_mut_status == 0x55:
            raise AssertionError(
                "pointer-layout mutation still self-checks 0x55: the "
                "cross-bank generic-pointer shape was not exercised")
        print("PASS Phase 2A pointer-bank-layout tripwire "
              "(cross-bank pointer mutation breaks the large-model "
              f"self-check, status {layout_mut_status:#04x})")

        # MT-1E Class 4 route gate.  The existing baseline fixture contains
        # independent ordinary, __reentrant and non-leaf ISR functions.  The
        # trace compiler must select the actual ralloc2 callback for each
        # shape, while three test-only mutations must fail closed.
        class4_stem = "fx-class4"
        shutil.copy(args.baseline_source, tdp / f"{class4_stem}.c")
        class4_routes = ("helper_mix", "fib_r", "fact_r", "isr_pressure")
        compile_probe(
            trace, class4_stem, "trace-model-small",
            ("--model-small",), expected_routes=class4_routes)
        compile_probe(
            trace, class4_stem, "trace-model-large",
            ("--model-large",), expected_routes=class4_routes)
        compile_probe(
            trace, class4_stem, "trace-stack-auto",
            ("--stack-auto",), expected_routes=class4_routes)

        stackmut_workdir = tdp / "stackmut"
        stackmut_workdir.mkdir()
        stackmut = build_directed_sdcc(
            args.ralloc2_source, args.sdcc, stackmut_workdir,
            ralloc2_defines=("-DMCS251_RALLOC2_TRACE_SELECTION "
                             "-DMCS251_RALLOC2_DISABLE_STACK_AUTO_SUPPORT"))
        compile_probe(
            stackmut, class4_stem, "stack-auto-mutation",
            ("--stack-auto",), expect_failure=True)

        rentmut_workdir = tdp / "reentrantmut"
        rentmut_workdir.mkdir()
        rentmut = build_directed_sdcc(
            args.ralloc2_source, args.sdcc, rentmut_workdir,
            ralloc2_defines=("-DMCS251_RALLOC2_TRACE_SELECTION "
                             "-DMCS251_RALLOC2_DISABLE_REENTRANT_SUPPORT"))
        compile_probe(
            rentmut, class4_stem, "reentrant-mutation",
            ("--model-small",), expect_failure=True)

        isrmut_workdir = tdp / "isrmut"
        isrmut_workdir.mkdir()
        isrmut = build_directed_sdcc(
            args.ralloc2_source, args.sdcc, isrmut_workdir,
            ralloc2_defines=("-DMCS251_RALLOC2_TRACE_SELECTION "
                             "-DMCS251_RALLOC2_DISABLE_ISR_SUPPORT"))
        compile_probe(
            isrmut, class4_stem, "isr-mutation",
            ("--model-small",), expect_failure=True)
        print("PASS class-4 stack-auto/reentrant/ISR mutations fail closed")

        def execute_aggregate(compiler, suffix, flags, sink_bytes):
            ihx = tdp / f"{agg_stem}-{suffix}.ihx"
            completed = run(
                [str(compiler), "-mmcs251", *flags,
                 "--data-loc", "0x38", "-o", str(ihx),
                 str(tdp / f"{agg_stem}.c")], cwd=tdp / "bin")
            output = completed.stdout + completed.stderr
            if completed.returncode != 0 or \
                    re.search(r"error|Undefined", output, re.I):
                raise AssertionError(
                    f"aggregate probe ({suffix}) link failed:\n"
                    f"{output[-800:]}")
            sink_address = map_symbol_address(ihx.with_suffix(".map"),
                                              "g_sink")
            status, _ = simulate(args.s51, ihx,
                                 expected_xdata=(sink_address,
                                                 sink_bytes))
            if status != 0x55:
                raise AssertionError(
                    f"aggregate probe ({suffix}) did not self-check "
                    f"(status {status:#04x})")
            return sha256(ihx)

        for agg_mode, agg_defs, agg_sink in agg_modes:
            for agg_model in ("model-small", "model-large"):
                agg_flags = (*agg_defs, f"--{agg_model}")
                production_agg_asm = compile_probe(
                    args.sdcc, agg_stem,
                    f"production-{agg_mode}-{agg_model}", agg_flags)
                forced_agg_asm = compile_probe(
                    actual, agg_stem,
                    f"forced-{agg_mode}-{agg_model}", agg_flags)
                if normalize_asm(production_agg_asm) != \
                        normalize_asm(forced_agg_asm):
                    raise AssertionError(
                        f"aggregate/bit probe [{agg_mode}/{agg_model}]: "
                        "production and forced-ralloc2 assembly differ; "
                        "the production callback still falls back for "
                        "aggregate/bit shapes")
                production_agg_ihx = execute_aggregate(
                    args.sdcc, f"production-{agg_mode}-{agg_model}",
                    agg_flags, agg_sink)
                forced_agg_ihx = execute_aggregate(
                    actual, f"forced-{agg_mode}-{agg_model}",
                    agg_flags, agg_sink)
                if production_agg_ihx != forced_agg_ihx:
                    raise AssertionError(
                        f"aggregate/bit probe [{agg_mode}/{agg_model}]: "
                        f"production and forced-ralloc2 IHX differ: "
                        f"{production_agg_ihx} != {forced_agg_ihx}")
                print(f"PASS aggregate/bit [{agg_mode}/{agg_model}] "
                      "production ralloc2 (compile+link+ucsim, 0x55; "
                      f"sha256={production_agg_ihx})")

        # MT-1E class 3: production must match the forced allocator for
        # direct/indirect calls under caller-save pressure, an eight-byte
        # scalar SEND/RECEIVE/return, and aggregate by-value/hidden return.
        # Each mode is intentionally small so the Class-5 pressure fixtures
        # exercise the newly admitted boundary independently.
        call_stem = "fx-call"
        shutil.copy(args.call_source, tdp / f"{call_stem}.c")
        call_modes = (
            ("call", (), "c3_call_sink", [0x12, 0x24, 0x36, 0x48],
             ("main", "c3_direct_step")),
            ("indirect", ("-DMCS251_RALLOC2_CLASS3_INDIRECT",),
             "c3_call_sink", [0xB4, 0x87, 0x69, 0x44],
             ("main", "c3_indirect_step", "c3_pointer_step")),
            ("wide", ("-DMCS251_RALLOC2_CLASS3_WIDE",),
             "c3_wide_sink", [0x11, 0x13, 0x11, 0x17,
                              0x11, 0x13, 0x11, 0x1F],
             ("main", "c3_wide_value")),
            ("aggregate-param",
             ("-DMCS251_RALLOC2_CLASS3_AGGREGATE_PARAM",),
             "c3_aggregate_sink", [0x00, 0x00, 0x00, 0x12],
             ("main", "c3_pair_first")),
            ("aggregate-return",
             ("-DMCS251_RALLOC2_CLASS3_AGGREGATE_RETURN",),
             "c3_aggregate_sink", [0x00, 0x00, 0x00, 0x12],
             ("main", "c3_make_pair")),
            ("aggregate-wide",
             ("-DMCS251_RALLOC2_CLASS3_AGGREGATE_WIDE",),
             "c3_aggregate_sink", [0x31, 0x32, 0x33, 0x34],
             ("main", "c3_step_triple")),
        )

        def execute_call(compiler, suffix, flags, sink_symbol, sink_bytes):
            ihx = tdp / f"{call_stem}-{suffix}.ihx"
            completed = run(
                [str(compiler), "-mmcs251", *flags,
                 "--data-loc", "0x38", "-o", str(ihx),
                 str(tdp / f"{call_stem}.c")], cwd=tdp / "bin")
            output = completed.stdout + completed.stderr
            if completed.returncode != 0 or \
                    re.search(r"error|Undefined", output, re.I):
                raise AssertionError(
                    f"class-3 probe ({suffix}) link failed:\n{output[-800:]}")
            sink_address = map_symbol_address(ihx.with_suffix(".map"),
                                              sink_symbol)
            status, _ = simulate(
                args.s51, ihx,
                expected_xdata=(sink_address, sink_bytes))
            if status != 0x55:
                raise AssertionError(
                    f"class-3 probe ({suffix}) did not self-check "
                    f"(status {status:#04x})")
            return sha256(ihx)

        for call_mode, call_defs, sink_symbol, sink_bytes, expected_routes \
                in call_modes:
            call_models = ("model-small", "model-large")
            if call_mode == "indirect":
                # Exercise a generic-pointer call result and its subsequent
                # post-increment while stack-auto forces the local pointer
                # storage protocol.
                call_models += ("stack-auto",)
            for call_model in call_models:
                call_flags = (*call_defs, f"--{call_model}")
                production_call_asm = compile_probe(
                    args.sdcc, call_stem,
                    f"production-{call_mode}-{call_model}", call_flags)
                forced_call_asm = compile_probe(
                    actual, call_stem,
                    f"forced-{call_mode}-{call_model}", call_flags)
                compile_probe(
                    trace, call_stem,
                    f"trace-{call_mode}-{call_model}", call_flags,
                    expected_routes=expected_routes)
                if normalize_asm(production_call_asm) != \
                        normalize_asm(forced_call_asm):
                    raise AssertionError(
                        f"class-3 probe [{call_mode}/{call_model}]: "
                        "production and forced-ralloc2 assembly differ")
                production_call_ihx = execute_call(
                    args.sdcc, f"production-{call_mode}-{call_model}",
                    call_flags, sink_symbol, sink_bytes)
                forced_call_ihx = execute_call(
                    actual, f"forced-{call_mode}-{call_model}",
                    call_flags, sink_symbol, sink_bytes)
                if production_call_ihx != forced_call_ihx:
                    raise AssertionError(
                        f"class-3 probe [{call_mode}/{call_model}]: "
                        f"production and forced-ralloc2 IHX differ: "
                        f"{production_call_ihx} != {forced_call_ihx}")
                print(f"PASS class-3 [{call_mode}/{call_model}] production "
                      "ralloc2 (compile+link+ucsim, 0x55; "
                      f"sha256={production_call_ihx})")

        # Causal route mutation: disabling RECEIVE support must fail closed
        # because no legacy callee route remains.
        receive_mut_workdir = tdp / "receive-mut"
        receive_mut_workdir.mkdir()
        receive_mut = build_directed_sdcc(
            args.ralloc2_source, args.sdcc, receive_mut_workdir,
            ralloc2_defines=("-DMCS251_RALLOC2_TRACE_SELECTION "
                             "-DMCS251_RALLOC2_DISABLE_RECEIVE_SUPPORT"))
        receive_mut_flags = (*call_modes[0][1], "--model-small")
        compile_probe(
            receive_mut, call_stem, "receive-mutation",
            receive_mut_flags, expect_failure=True)
        print("PASS class-3 RECEIVE mutation fails closed")

        # Two overlapping eight-byte returns need 16 bytes while the plain
        # pool has 14.  Class 5 now spills the ordinary return safely and
        # captures each CALL result before caller-save restoration; production
        # and the explicit-force compiler must therefore be identical and
        # both images must self-check.
        overlap_defs = ("-DMCS251_RALLOC2_CLASS3_WIDE_OVERLAP",)
        overlap_sink = [0x02, 0x04, 0x06, 0x08,
                        0x0A, 0x0C, 0x0E, 0x33]
        for call_model in ("model-small", "model-large"):
            overlap_flags = (*overlap_defs, f"--{call_model}")
            production_overlap_asm = compile_probe(
                args.sdcc, call_stem,
                f"production-wide-overlap-{call_model}", overlap_flags)
            forced_overlap_asm = compile_probe(
                actual, call_stem,
                f"forced-wide-overlap-{call_model}", overlap_flags)
            if normalize_asm(production_overlap_asm) != \
                    normalize_asm(forced_overlap_asm):
                raise AssertionError(
                    f"wide-overlap/{call_model}: production did not "
                    "exercise the Class-5 ralloc2 capacity path")
            production_overlap_ihx = execute_call(
                args.sdcc, f"production-wide-overlap-{call_model}",
                overlap_flags, "c3_wide_sink", overlap_sink)
            forced_overlap_ihx = execute_call(
                actual, f"forced-wide-overlap-{call_model}",
                overlap_flags, "c3_wide_sink", overlap_sink)
            if production_overlap_ihx != forced_overlap_ihx:
                raise AssertionError(
                    f"wide-overlap/{call_model}: production and forced "
                    "ralloc2 IHX differ")
            print(f"PASS class-5 [wide-overlap/{call_model}] "
                  "14-byte call-result capacity with spill materialization "
                  f"(production ralloc2 0x55; sha256={production_overlap_ihx})")

        # IDATA/near-pointer access reserves R0/R1, reducing the effective
        # pool from 14 to 12 bytes.  Three overlapping results totalling 13
        # bytes must spill one result while preserving the R0/R1 pointer pair.
        capacity_defs = ("-DMCS251_RALLOC2_CLASS3_PTR_CAPACITY",)
        for call_model in ("model-small", "model-large"):
            capacity_flags = (*capacity_defs, f"--{call_model}")
            production_capacity_asm = compile_probe(
                args.sdcc, call_stem,
                f"production-ptr-capacity-{call_model}", capacity_flags)
            forced_capacity_asm = compile_probe(
                actual, call_stem,
                f"forced-ptr-capacity-{call_model}", capacity_flags)
            if normalize_asm(production_capacity_asm) != \
                    normalize_asm(forced_capacity_asm):
                raise AssertionError(
                    f"ptr-capacity/{call_model}: production did not "
                    "exercise the Class-5 R0/R1 capacity path")
            production_capacity_ihx = execute_call(
                args.sdcc, f"production-ptr-capacity-{call_model}",
                capacity_flags, "c3_capacity_sink1", [0x21])
            forced_capacity_ihx = execute_call(
                actual, f"forced-ptr-capacity-{call_model}",
                capacity_flags, "c3_capacity_sink1", [0x21])
            if production_capacity_ihx != forced_capacity_ihx:
                raise AssertionError(
                    f"ptr-capacity/{call_model}: production and forced "
                    "ralloc2 IHX differ")
            print(f"PASS class-5 [ptr-capacity/{call_model}] "
                  "12-byte effective pool with R0/R1 reservation "
                  f"(production ralloc2 0x55; sha256={production_capacity_ihx})")

        # Mutation tripwire for the caller-save failure that originally
        # clobbered __mullong results: disable the new genCall/genPcall
        # spill materialisation and the over-capacity call result must lose
        # its return value before the caller-save restore.  The mutation
        # rebuilds only gen.c; production never defines this macro.
        callmut_workdir = tdp / "callmut"
        callmut_workdir.mkdir()
        callmut = build_directed_sdcc(
            args.ralloc2_source, args.sdcc, callmut_workdir,
            gen_defines="-DMCS251_RALLOC2_DISABLE_CALL_RESULT_SPILL_MATERIALIZATION")
        callmut_ihx = tdp / "fx-call-mutated.ihx"
        completed = run(
            [str(callmut), "-mmcs251", "--model-small",
             "-DMCS251_RALLOC2_CLASS3_WIDE_OVERLAP",
             "--data-loc", "0x38", "-o", str(callmut_ihx),
             str(tdp / f"{call_stem}.c")], cwd=tdp / "bin")
        callmut_output = completed.stdout + completed.stderr
        if completed.returncode != 0 or \
                re.search(r"error|Undefined", callmut_output, re.I):
            raise AssertionError(
                f"call-priority mutation compile failed:\n"
                f"{callmut_output[-500:]}")
        callmut_status, _ = simulate(args.s51, callmut_ihx)
        if callmut_status == 0x55:
            raise AssertionError(
                "call-result spill-materialization mutation still "
                "self-checks 0x55: the over-capacity return was not "
                "captured through the new genCall/genPcall path")
        print("PASS class-5 mutation tripwire (disabling call-result "
              f"spill materialization breaks the capacity probe, status "
              f"{callmut_status:#04x})")

        # ABI-sized ordinary scalars must also remain eligible for the pool.
        # Without five-to-eight-byte admission, ckd_add's long-long helpers
        # expand into repeated spill reloads and exceed the MCS-251 short
        # branch range.  Compile the real library source with production and
        # deliberately mutated allocators so this is a causal tripwire, not
        # merely a one-time library-build observation.
        ckd_include = args.ckd_add_source.parents[1] / "include"

        def compile_ckd_add(compiler, suffix):
            rel_path = tdp / f"ckd-add-{suffix}.rel"
            return run(
                [str(compiler), "-mmcs251", "--model-large",
                 f"-I{ckd_include}", f"-I{ckd_include / 'mcs51'}",
                 "--nostdinc", "--std-c23", "-c",
                 str(args.ckd_add_source), "-o", str(rel_path)],
                cwd=tdp / "bin")

        production_ckd = compile_ckd_add(args.sdcc, "production")
        production_ckd_output = production_ckd.stdout + production_ckd.stderr
        if production_ckd.returncode != 0 or \
                re.search(r"error|Undefined", production_ckd_output, re.I):
            raise AssertionError(
                "production allocator cannot assemble ckd_add/model-large:\n"
                f"{production_ckd_output[-800:]}")

        widemut_workdir = tdp / "widemut"
        widemut_workdir.mkdir()
        widemut = build_directed_sdcc(
            args.ralloc2_source, args.sdcc, widemut_workdir,
            ralloc2_defines="-DMCS251_RALLOC2_DISABLE_WIDE_SCALAR_ADMISSION")
        mutated_ckd = compile_ckd_add(widemut, "mutated")
        mutated_ckd_output = mutated_ckd.stdout + mutated_ckd.stderr
        if mutated_ckd.returncode == 0 or not re.search(
                r"Branching range exceeded", mutated_ckd_output):
            raise AssertionError(
                "wide-scalar mutation did not reproduce ckd_add's branch-"
                "range failure; the admission gate is not proven live:\n"
                f"{mutated_ckd_output[-800:]}")
        print("PASS class-3 wide-scalar mutation tripwire (production "
              "assembles ckd_add/model-large; disabling 5..8-byte scalar "
              "admission reproduces the branch-range failure)")

        # Phase 2A low-DATA closure depends on reusing spill slots whose
        # byte-level live ranges have no conflict edge.  printf_large is the
        # real library shape that previously required 86 consecutive DSEG
        # bytes and made the small runtime un-linkable.  The production
        # allocator must collapse its seventeen independent slots; disabling
        # only reuse must restore the old footprint.
        printf_include = args.printf_large_source.parents[1] / "include"

        def compile_printf_large(compiler, suffix):
            asm_path = tdp / f"printf-large-{suffix}.asm"
            completed = run(
                [str(compiler), "-mmcs251", "--model-small",
                 f"-I{printf_include}", f"-I{printf_include / 'mcs51'}",
                 "--nostdinc", "--std-c23", "-S",
                 str(args.printf_large_source), "-o", str(asm_path)],
                cwd=tdp / "bin")
            output = completed.stdout + completed.stderr
            if completed.returncode != 0 or re.search(
                    r"error|Undefined", output, re.I):
                raise AssertionError(
                    f"printf_large ({suffix}) compilation failed:\n"
                    f"{output[-800:]}")
            assembly = asm_path.read_text()
            slots = set(re.findall(
                r"^(__print_format_sloc\d+_\d+_\d+):\s*$",
                assembly, re.M))
            return len(slots)

        production_slots = compile_printf_large(args.sdcc, "production")
        reusemut_workdir = tdp / "reusemut"
        reusemut_workdir.mkdir()
        reusemut = build_directed_sdcc(
            args.ralloc2_source, args.sdcc, reusemut_workdir,
            ralloc2_defines="-DMCS251_RALLOC2_DISABLE_SPILL_SLOT_REUSE")
        mutated_slots = compile_printf_large(reusemut, "mutated")
        if production_slots > 4 or mutated_slots < 12 or \
                mutated_slots <= production_slots:
            raise AssertionError(
                "spill-slot reuse mutation is not causal for printf_large: "
                f"production={production_slots}, mutation={mutated_slots}")
        print("PASS Phase 2A spill-slot reuse tripwire "
              f"(printf_large production={production_slots} slots; "
              f"reuse-disabled mutation={mutated_slots})")

        # A narrowing 32-bit-integer -> 24-bit-pointer CAST can reuse its
        # dying input's spill slot.  On the native big-endian layout a plain
        # low-to-high copy then has destructive physical overlap.  Production
        # must snapshot the source; disabling only that generator guard must
        # make the executable self-check fail.
        spill_copy_stem = "fx-spill-copy"
        shutil.copy(args.spill_copy_source,
                    tdp / f"{spill_copy_stem}.c")

        def execute_spill_copy(compiler, suffix):
            ihx = tdp / f"{spill_copy_stem}-{suffix}.ihx"
            completed = run(
                [str(compiler), "-mmcs251", "--model-small",
                 "-o", str(ihx), str(tdp / f"{spill_copy_stem}.c")],
                cwd=tdp / "bin")
            output = completed.stdout + completed.stderr
            if completed.returncode != 0 or re.search(
                    r"error|Undefined", output, re.I):
                raise AssertionError(
                    f"spill-copy probe ({suffix}) link failed:\n"
                    f"{output[-800:]}")
            status, _ = simulate(args.s51, ihx)
            return status

        production_spill_copy = execute_spill_copy(args.sdcc, "production")
        if production_spill_copy != 0x55:
            raise AssertionError(
                "production spill-copy overlap probe did not self-check "
                f"(status {production_spill_copy:#04x})")
        copymut_workdir = tdp / "spill-copy-mut"
        copymut_workdir.mkdir()
        copymut = build_directed_sdcc(
            args.ralloc2_source, args.sdcc, copymut_workdir,
            gen_defines="-DMCS251_RALLOC2_DISABLE_SPILL_COPY_OVERLAP")
        mutated_spill_copy = execute_spill_copy(copymut, "mutated")
        if mutated_spill_copy == 0x55:
            raise AssertionError(
                "spill-copy overlap mutation still self-checks 0x55: "
                "the destructive narrowing CAST was not exercised")
        print("PASS Phase 2A spill-copy overlap tripwire "
              f"(production=0x55; guard-disabled mutation="
              f"{mutated_spill_copy:#04x})")

        # MT-1E review-round-2 mutation tripwire: restore the old pointer-
        # spill policy, then disable adoption of packRegisters' pre-assigned
        # (sir) storage.  The runtime-copied local pointer is left
        # unmaterialised, so the ptrvar probe dereferences address 0 and its
        # self check must fail.  Restoring pointer spill is necessary in
        # Phase 2A because the production allocator now keeps target pointers
        # register-resident.
        # (The earlier bitfield-spill mutation is superseded: its kill
        # mechanism was this same sir-materialisation bug, now fixed,
        # so the mutated compiler self-checks 0x55 and proves nothing.)
        sirmut_workdir = tdp / "sirmut"
        sirmut_workdir.mkdir()
        sirmut = build_directed_sdcc(
            args.ralloc2_source, args.sdcc, sirmut_workdir,
            ralloc2_defines=("-DMCS251_RALLOC2_DISABLE_POINTER_ADMISSION "
                             "-DMCS251_RALLOC2_NO_SIR_ADOPT"))
        sirmut_ihx = tdp / "fx-aggregate-sirmut.ihx"
        shutil.copy(args.aggregate_source, tdp / "fx-aggregate-sirmut.c")
        completed = run(
            [str(sirmut), "-mmcs251", "--model-small",
             "-DMCS251_RALLOC2_AGG_PTRVAR",
             "--data-loc", "0x38", "-o", str(sirmut_ihx),
             str(tdp / "fx-aggregate-sirmut.c")], cwd=tdp / "bin")
        sirmut_output = completed.stdout + completed.stderr
        if completed.returncode != 0 or \
                re.search(r"error|Undefined", sirmut_output, re.I):
            raise AssertionError(
                f"sir-adoption mutation compile failed:\n"
                f"{sirmut_output[-500:]}")
        sirmut_status, _ = simulate(args.s51, sirmut_ihx)
        if sirmut_status == 0x55:
            raise AssertionError(
                "sir-adoption mutation still self-checks 0x55: the "
                "unmaterialised local pointer did not break the probe, "
                "so the adoption path is not proven live")
        print("PASS ptrvar mutation tripwire (skipping sir adoption "
              f"breaks the probe self-check, status "
              f"{sirmut_status:#04x})")

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

    print("PASS: all retained directed-path fixtures execute correctly through "
          "the MT-1E Phase 2B production ralloc2 callback in three memory "
          "models")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
