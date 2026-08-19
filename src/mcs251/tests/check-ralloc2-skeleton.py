#!/usr/bin/env python3
"""ralloc2 model gate (MT-1D; MT-1B/MT-1C checks retained).

Three checks:

1. Standalone self test: ralloc2.cc is compiled with
   -DMCS251_RALLOC2_STANDALONE_TEST and executed.  This exercises the
   freestanding descriptor/constraint core (register pool, WR/DR
   overlap model, tuple costs, operand admission matrix, clobber
   classes) without any SDCC headers.

2. Selected-path and legacy comparison: probe programs are compiled by
   the CURRENT production sdcc (ralloc2) and by a temporary compiler whose
   port callback is switched back to the retained legacy allocator.  Each
   path has its own normalized assembly hash table.  The legacy table keeps
   the original MT-1B pre-ralloc2 hashes; the selected table is the MT-1D
   freeze.  The
   normalization elides the build-host OS from the "Version" header comment
   and compiles from copied, fixed-name sources so both comparisons are
   machine-independent.  Regenerate the tables with --regenerate only when
   the corresponding path intentionally changes and record why in the same
   change.

3. Object/boundary: the built ralloc2.o defines the entry and the
   production sdcc exposes the selected ralloc2 callback plus the
   retained legacy allocator entry for comparison.

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

# Original pre-ralloc2 legacy assembly baselines (MT-1B, 2026-08-18).  MT-1D
# must preserve these hashes: allocator selection does not authorize shared
# peephole or code-generator changes.
LEGACY_BASELINES = {
    "probe-small": "ba7f2e955782d3424875306b565225e77b05988f617ae8e7be4a4d62412872b7",
    "probe-large": "ace1ecf6df75478170a3187f5e92eab92bf843a5ca03bf2537a3180c171a6e71",
    "probe-stack-auto": "fb648442e8a134a138b33a037853a3783d215438b9ecb0d2e56573a94c963473",
    "probe-call24": "a460df410bfb6bdeed5e72c38d8058a850be0d3048f989aa5d99e5e3af78c660",
}

# Frozen selected callback assembly baselines (MT-1D, 2026-08-19).  These
# probes intentionally exercise the fail-closed legacy fallback while the
# directed native-MUL fixture covers the proven ralloc2 subset.
SELECTED_BASELINES = {
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


def build_legacy_sdcc(sdcc, workdir, source_main):
    """Link a temporary compiler with only the port callback reverted."""
    sdcc_abs = sdcc.resolve()
    candidates = [sdcc_abs.parents[1] / "src", sdcc_abs.parents[2] / "src"]
    build_src = next((p for p in candidates if (p / "Makefile").exists()), None)
    if build_src is None:
        raise AssertionError(f"cannot locate build src tree for {sdcc_abs}")
    port_archive = build_src / "mcs251" / "port.a"
    main_source = source_main.resolve()
    if not port_archive.exists() or not main_source.exists():
        raise AssertionError("missing current MCS-251 port archive or main.c")

    workdir.mkdir(parents=True, exist_ok=True)
    legacy_source = workdir / "main-legacy.c"
    marker = "mcs251_ralloc2_assignRegisters,"
    source_text = main_source.read_text()
    if source_text.count(marker) != 1:
        raise AssertionError("cannot identify the production allocator callback")
    legacy_source.write_text(source_text.replace(marker,
                                                  "mcs251_assignRegisters,"))

    legacy_main = workdir / "main.o"
    legacy_port = workdir / "mcs251-legacy.a"
    legacy_sdcc = workdir / "bin" / "sdcc"
    legacy_sdcc.parent.mkdir(exist_ok=True)
    makefile = workdir / "legacy.mk"
    makefile.write_text(
        ".PHONY: legacy-main legacy-link\n"
        "legacy-main:\n"
        "\t$(CC) $(CFLAGS) $(CPPFLAGS) -Imcs251 "
        "-I$(LEGACY_SOURCE_DIR) "
        "-c $(LEGACY_MAIN) -o $(DIRECTED_MAIN)\n"
        "legacy-link:\n"
        "\t$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $(DIRECTED_SDCC) "
        "$(SLIBOBJS) $(OBJECTS) "
        "$(filter-out mcs251/port.a,$(PORT_LIBS)) "
        "$(DIRECTED_PORT) $(LIBDIRS) $(LIBS)\n")
    make_base = ["make", "-s", "-f", "Makefile", "-f", str(makefile)]
    run([*make_base, "legacy-main", f"LEGACY_MAIN={legacy_source}",
         f"LEGACY_SOURCE_DIR={main_source.parent}",
         f"DIRECTED_MAIN={legacy_main}"], cwd=build_src)
    shutil.copy(port_archive, legacy_port)
    run(["ar", "d", str(legacy_port), "main.o"])
    run(["ar", "rcs", str(legacy_port), str(legacy_main)])
    run([*make_base, "legacy-link", f"DIRECTED_PORT={legacy_port}",
         f"DIRECTED_SDCC={legacy_sdcc}"], cwd=build_src)

    for tool in ("sdcpp", "sdas251", "sdld"):
        source = sdcc.parent / tool
        if not source.exists():
            raise AssertionError(f"missing tool beside sdcc: {source}")
        (legacy_sdcc.parent / tool).symlink_to(source)
    share = build_src.parent / "install" / "share"
    if not share.exists():
        raise AssertionError(f"missing install share tree: {share}")
    (legacy_sdcc.parent.parent / "share").symlink_to(share)
    return legacy_sdcc


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

    # 2. Freeze the selected path and independently verify the retained
    #    legacy callback against the MT-1B baselines.
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
        legacy_sdcc = build_legacy_sdcc(
            args.sdcc, tdp / "legacy", args.ralloc2_source.parent / "main.c")
        legacy_table = {}
        legacy_tdp = tdp / "legacy"
        shutil.copy(args.probe_source, legacy_tdp / "probe.c")
        shutil.copy(args.call_source, legacy_tdp / "callprobe.c")
        for name, flags, source in variants:
            legacy_table[name] = compile_probe(
                legacy_sdcc, legacy_tdp, flags, source, f"{name}.asm")
        if args.regenerate:
            print("selected ralloc2:")
            for name in sorted(table):
                print(f'    "{name}": "{table[name]}",')
            print("legacy:")
            for name in sorted(legacy_table):
                print(f'    "{name}": "{legacy_table[name]}",')
            return 0
        for name in sorted(table):
            if table[name] != SELECTED_BASELINES[name]:
                raise AssertionError(
                    f"selected-path freeze violated for {name}: "
                    f"ralloc2 asm hash {table[name]} != frozen "
                    f"{SELECTED_BASELINES[name]} (see --regenerate note)")
        for name in sorted(legacy_table):
            if legacy_table[name] != LEGACY_BASELINES[name]:
                raise AssertionError(
                    f"legacy comparison drifted for {name}: "
                    f"asm hash {legacy_table[name]} != frozen "
                    f"{LEGACY_BASELINES[name]}")
        print("PASS selected ralloc2 path freeze (4 probes)")
        print("PASS retained legacy callback comparison (4 MT-1B probes)")

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
    for symbol in ("mcs251_ralloc2_cc",
                   "mcs251_ralloc2_assignRegisters"):
        if not defines_symbol(production_nm, symbol):
            raise AssertionError(
                f"production sdcc does not define selected ralloc2 entry "
                f"{symbol}")
    if not defines_symbol(production_nm, "mcs251_assignRegisters"):
        raise AssertionError(
            "production sdcc does not define the legacy allocator entry")
    print("PASS production sdcc links selected ralloc2 and retained legacy "
          "allocator")

    print("PASS: ralloc2 model, selected path, and retained legacy "
          "comparison are present")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
