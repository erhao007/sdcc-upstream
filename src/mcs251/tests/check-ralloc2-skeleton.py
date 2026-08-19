#!/usr/bin/env python3
"""ralloc2 model gate (MT-1B skeleton checks retained by MT-1C).

Three checks:

1. Standalone self test: ralloc2.cc is compiled with
   -DMCS251_RALLOC2_STANDALONE_TEST and executed.  This exercises the
   freestanding descriptor/constraint core (register pool, WR/DR
   overlap model, tuple costs, operand admission matrix, clobber
   classes) without any SDCC headers.

2. Default-path freeze: probe programs are compiled by the CURRENT
   production sdcc and their (normalized) assembly is hash-compared
   against the frozen pre-ralloc2 baselines below.  The ralloc2 work
   must not change legacy code generation by a single byte.  The
   normalization elides the build-host OS from the "Version" header
   comment and compiles from copied, fixed-name sources so the
   comparison is machine-independent.  Regenerate the table with
   --regenerate ONLY when legacy codegen is intentionally changed by
   an approved later step, and record why in the same change.

3. Object/boundary: the built ralloc2.o defines the entry and the
   directed adapter with C linkage, and the production sdcc links the
   legacy allocator only (the ralloc2 archive member stays
   unreferenced).

The full directed compilation and execution behaviour (temporary
allocator-swapped compiler running the MT-1A/MT-1C fixtures in three
memory models under uCsim) lives in check-ralloc2-directed.py.
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

# Frozen pre-ralloc2 legacy assembly baselines (MT-1B, 2026-08-18).
FROZEN_BASELINES = {
    "probe-small": "ba7f2e955782d3424875306b565225e77b05988f617ae8e7be4a4d62412872b7",
    "probe-large": "ace1ecf6df75478170a3187f5e92eab92bf843a5ca03bf2537a3180c171a6e71",
    "probe-stack-auto": "fb648442e8a134a138b33a037853a3783d215438b9ecb0d2e56573a94c963473",
    "probe-call24": "a460df410bfb6bdeed5e72c38d8058a850be0d3048f989aa5d99e5e3af78c660",
}

VERSION_LINE = re.compile(r"^; Version (\S+ \S+).*$", re.M)


def run(command, **kwargs):
    completed = subprocess.run(command, capture_output=True, text=True,
                               errors="replace", **kwargs)
    if completed.returncode != 0:
        raise AssertionError(
            f"command failed ({completed.returncode}): "
            f"{' '.join(map(str, command))}\n"
            f"{completed.stdout[-500:]}{completed.stderr[-500:]}")
    return completed.stdout


def defines_symbol(nm_text, symbol):
    return re.search(rf"\b[Tt]\s+_?{re.escape(symbol)}$",
                     nm_text, re.M) is not None


def normalize_asm(text):
    # Elide the build-host OS from the version header; everything else
    # in the output is deterministic for a given source and flags.
    return VERSION_LINE.sub(r"; Version \1 <host elided>", text)


def compile_probe(sdcc, workdir, flags, source_name, out_name):
    run([str(sdcc), "-mmcs251", *flags, "-S", "-o", out_name,
         source_name], cwd=workdir)
    text = (workdir / out_name).read_text()
    return hashlib.sha256(normalize_asm(text).encode()).hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sdcc", required=True, type=Path)
    ap.add_argument("--ralloc2-source", required=True, type=Path)
    ap.add_argument("--probe-source", required=True, type=Path)
    ap.add_argument("--call-source", required=True, type=Path)
    ap.add_argument("--regenerate", action="store_true",
                    help="print a fresh baseline table instead of "
                         "verifying (use only when legacy codegen "
                         "changes intentionally)")
    args = ap.parse_args()

    for tool in (args.sdcc, args.ralloc2_source, args.probe_source,
                 args.call_source):
        if not tool.exists():
            raise AssertionError(f"missing input: {tool}")
    args.sdcc = args.sdcc.resolve()
    args.ralloc2_source = args.ralloc2_source.resolve()

    # 1. Standalone self test of the model core.
    cxx = os.environ.get("CXX", "c++")
    with tempfile.TemporaryDirectory(prefix="ralloc2-skel-") as td:
        tdp = Path(td)
        selftest = tdp / "ralloc2-selftest"
        run([cxx, "-std=c++11", "-Wall",
             "-DMCS251_RALLOC2_STANDALONE_TEST",
             "-o", str(selftest), str(args.ralloc2_source)])
        out = run([str(selftest)])
        if "PASS" not in out:
            raise AssertionError(f"self test did not pass: {out}")
        print("PASS ralloc2 model standalone self test "
              "(pool/WR-DR model/costs/admission/clobbers)")

    # 2. Default-path freeze against the pre-ralloc2 baselines.
    with tempfile.TemporaryDirectory(prefix="ralloc2-freeze-") as td:
        tdp = Path(td)
        shutil.copy(args.probe_source, tdp / "probe.c")
        shutil.copy(args.call_source, tdp / "callprobe.c")
        variants = [
            ("probe-small",
             ["--model-small", "--data-loc", "0x38"], "probe.c"),
            ("probe-large",
             ["--model-large", "--data-loc", "0x38"], "probe.c"),
            ("probe-stack-auto",
             ["--stack-auto", "--data-loc", "0x38"], "probe.c"),
            ("probe-call24", [], "callprobe.c"),
        ]
        table = {}
        for name, flags, source in variants:
            table[name] = compile_probe(args.sdcc, tdp, flags,
                                        source, f"{name}.asm")
        if args.regenerate:
            for name in sorted(table):
                print(f'    "{name}": "{table[name]}",')
            return 0
        for name in sorted(table):
            if table[name] != FROZEN_BASELINES[name]:
                raise AssertionError(
                    f"default-path freeze violated for {name}: "
                    f"legacy asm hash {table[name]} != frozen "
                    f"{FROZEN_BASELINES[name]} (legacy codegen changed "
                    f"or baselines stale; see --regenerate note)")
        print("PASS default-path freeze (legacy asm byte-identical to "
              "pre-ralloc2 baselines, 4 probes)")

    # 3. Object surface and production boundary.
    sdcc_abs = args.sdcc.resolve()
    candidates = [
        sdcc_abs.parents[1] / "src" / "mcs251" / "ralloc2.o",
        sdcc_abs.parents[2] / "src" / "mcs251" / "ralloc2.o",
    ]
    obj = next((p for p in candidates if p.exists()), None)
    if obj is None:
        raise AssertionError(
            "ralloc2 object not built at expected locations: "
            f"{candidates}")
    nm_out = run(["nm", "-g", str(obj)])
    for symbol in ("mcs251_ralloc2_cc",
                   "mcs251_ralloc2_assignRegisters"):
        if not defines_symbol(nm_out, symbol):
            raise AssertionError(
                f"ralloc2.o does not define {symbol} with C linkage")
    print("PASS ralloc2.o defines the entry and directed adapter")

    build_src = obj.parents[1]
    linked_sdcc = build_src / "sdcc"
    if not linked_sdcc.exists():
        raise AssertionError(
            f"missing unstripped production link: {linked_sdcc}")
    production_nm = run(["nm", "-g", str(linked_sdcc)])
    if (defines_symbol(production_nm, "mcs251_ralloc2_cc") or
            defines_symbol(production_nm,
                           "mcs251_ralloc2_assignRegisters")):
        raise AssertionError(
            "production sdcc unexpectedly links the ralloc2 path")
    if not defines_symbol(production_nm, "mcs251_assignRegisters"):
        raise AssertionError(
            "production sdcc does not define the legacy allocator entry")
    print("PASS production sdcc links legacy allocator only")

    print("PASS: ralloc2 model self-tests and the legacy default path "
          "remain byte-identical")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
