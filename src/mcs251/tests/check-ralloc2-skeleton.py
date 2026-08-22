#!/usr/bin/env python3
"""ralloc2 model and Phase-2B deletion gate.

The gate covers three boundaries:

1. The standalone ralloc2 model must pass without SDCC headers.
2. The production compiler must retain the frozen Phase-2A assembly hashes.
3. The port object/archive/compiler surface must contain ralloc2 only; the
   removed legacy allocator file, object, and callback must be absent.

The full compile/link/uCsim fixtures live in check-ralloc2-directed.py.
"""

import argparse
import hashlib
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path


SELECTED_BASELINES = {
    "probe-small": "60c67ced1b28443f8e9b7918f8446cefe7d9e21bf0cbb1e19f28a9aa0cbbb8b4",
    "probe-large": "323a63123adfa446043ded4fdf8f284c817307f36dcf42f8a2ab8984d0c0ab0e",
    "probe-stack-auto": "852d439d4d8d7cf374027f0393015d3e3f23e5b63b4007b08aabbd43a780e096",
    "probe-call24": "9ed02d84f26266b6232c5afa77cd6618a09224f90c27a95dfd4742ee281b69a4",
}

VERSION_LINE = re.compile(r"^; Version (\S+ \S+).*$", re.M)


def _host_env():
    env = os.environ.copy()
    env.pop("COMPILER_PATH", None)
    return env


def _host_tool(name):
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


def _host_binary(path):
    if os.name == "nt" and not path.exists() and Path(str(path) + ".exe").exists():
        return Path(str(path) + ".exe")
    return path


def run(command, timeout=120, **kwargs):
    try:
        completed = subprocess.run(command, capture_output=True, text=True,
                                   errors="replace", timeout=timeout, **kwargs)
    except subprocess.TimeoutExpired as exc:
        raise AssertionError(
            f"command timed out after {timeout}s: "
            f"{' '.join(map(str, command))}") from exc
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
    return VERSION_LINE.sub(r"; Version \1 <host elided>", text)


def compile_probe(sdcc, workdir, flags, source_name, out_name):
    run([str(sdcc), "-mmcs251", *flags, "-S", "-o", out_name,
         source_name], cwd=workdir)
    text = (workdir / out_name).read_text()
    return hashlib.sha256(normalize_asm(text).encode()).hexdigest()


def assert_no_legacy_surface(build_src, obj, linked_sdcc, source_dir):
    ar = _host_tool("ar")
    legacy_source = source_dir / "ralloc.c"
    legacy_object = obj.parent / "ralloc.o"
    if legacy_source.exists() or legacy_object.exists():
        raise AssertionError(
            "legacy allocator source/object must be absent: "
            f"source={legacy_source.exists()} object={legacy_object.exists()}")
    for source_name in ("main.c", "ralloc.h", "ralloc2.cc", "ralloc2_support.c"):
        source = source_dir / source_name
        if not source.exists():
            raise AssertionError(f"missing production source: {source}")
        text = source.read_text(errors="replace")
        if "mcs251_assignRegisters" in text:
            raise AssertionError(f"{source_name} still names legacy callback")
        if source_name == "ralloc2.cc" and "MCS251_RALLOC2_FORCE" in text:
            raise AssertionError("production ralloc2.cc still contains force selector")
    port_archive = obj.parent / "port.a"
    if not port_archive.exists():
        raise AssertionError(f"missing MCS-251 port archive: {port_archive}")
    members = run([ar, "t", str(port_archive)], env=_host_env()).splitlines()
    if "ralloc2_support.o" not in members or "ralloc.o" in members:
        raise AssertionError(
            "MCS-251 archive must contain ralloc2_support.o and no ralloc.o: "
            f"{members}")

    nm_obj = run([_host_tool("nm"), "-g", str(obj)], env=_host_env())
    nm_support = run([_host_tool("nm"), "-g",
                      str(obj.parent / "ralloc2_support.o")],
                     env=_host_env())
    for surface, nm_text in (("ralloc2.o", nm_obj),
                             ("ralloc2_support.o", nm_support)):
        if "mcs251_assignRegisters" in nm_text:
            raise AssertionError(f"{surface} still exposes legacy allocator")
    production_nm = run([_host_tool("nm"), "-g", str(linked_sdcc)],
                         env=_host_env())
    if defines_symbol(production_nm, "mcs251_assignRegisters"):
        raise AssertionError("production sdcc still exposes legacy allocator")
    print("PASS Phase-2B legacy surface removed "
          "(source/object/archive/compiler)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sdcc", required=True, type=Path)
    ap.add_argument("--ralloc2-source", required=True, type=Path)
    ap.add_argument("--probe-source", required=True, type=Path)
    ap.add_argument("--call-source", required=True, type=Path)
    ap.add_argument("--regenerate", action="store_true",
                    help="print a fresh Phase-2A selected-path table")
    args = ap.parse_args()

    for tool in (args.sdcc, args.ralloc2_source, args.probe_source,
                 args.call_source):
        if not tool.exists():
            raise AssertionError(f"missing input: {tool}")
    args.sdcc = args.sdcc.resolve()
    args.ralloc2_source = args.ralloc2_source.resolve()

    cxx = os.environ.get("CXX", "c++")
    with tempfile.TemporaryDirectory(prefix="ralloc2-skel-") as td:
        selftest = Path(td) / "ralloc2-selftest"
        run([cxx, "-std=c++11", "-Wall",
             "-DMCS251_RALLOC2_STANDALONE_TEST",
             "-o", str(selftest), str(args.ralloc2_source)],
            env=_host_env())
        out = run([str(_host_binary(selftest))], env=_host_env())
        if "PASS" not in out:
            raise AssertionError(f"self test did not pass: {out}")
        print("PASS ralloc2 model standalone self test "
              "(pool/WR-DR model/costs/admission/clobbers)")

    with tempfile.TemporaryDirectory(prefix="ralloc2-freeze-") as td:
        tdp = Path(td)
        (tdp / "probe.c").write_text(args.probe_source.read_text())
        (tdp / "callprobe.c").write_text(args.call_source.read_text())
        variants = [
            ("probe-small", ["--model-small", "--data-loc", "0x38"],
             "probe.c"),
            ("probe-large", ["--model-large", "--data-loc", "0x38"],
             "probe.c"),
            ("probe-stack-auto", ["--stack-auto", "--data-loc", "0x38"],
             "probe.c"),
            ("probe-call24", [], "callprobe.c"),
        ]
        table = {
            name: compile_probe(args.sdcc, tdp, flags, source, f"{name}.asm")
            for name, flags, source in variants
        }
        if args.regenerate:
            print("selected ralloc2:")
            for name in sorted(table):
                print(f'    "{name}": "{table[name]}",')
            return 0
        for name in sorted(table):
            if table[name] != SELECTED_BASELINES[name]:
                raise AssertionError(
                    f"Phase-2A selected-path freeze violated for {name}: "
                    f"{table[name]} != {SELECTED_BASELINES[name]}")
        print("PASS selected ralloc2 Phase-2A freeze (4 probes)")

    sdcc_abs = args.sdcc.resolve()
    candidates = [
        sdcc_abs.parents[1] / "src" / "mcs251" / "ralloc2.o",
        sdcc_abs.parents[2] / "src" / "mcs251" / "ralloc2.o",
    ]
    obj = next((p for p in candidates if p.exists()), None)
    if obj is None:
        raise AssertionError(f"ralloc2 object not built: {candidates}")
    nm_out = run([_host_tool("nm"), "-g", str(obj)], env=_host_env())
    for symbol in ("mcs251_ralloc2_cc", "mcs251_ralloc2_assignRegisters"):
        if not defines_symbol(nm_out, symbol):
            raise AssertionError(f"ralloc2.o does not define {symbol}")
    print("PASS ralloc2.o defines the production callback")

    build_src = obj.parents[1]
    linked_sdcc = _host_binary(build_src / "sdcc")
    if not linked_sdcc.exists():
        raise AssertionError(f"missing unstripped production link: {linked_sdcc}")
    assert_no_legacy_surface(build_src, obj, linked_sdcc,
                              args.ralloc2_source.parent)
    print("PASS: ralloc2 model, Phase-2A freeze, and Phase-2B deletion "
          "surface are present")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
