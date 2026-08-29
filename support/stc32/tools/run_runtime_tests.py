#!/usr/bin/env python3
"""ST-3 MCS-251 runtime/libc execution gate."""

import os
import re
import subprocess
import tempfile

import run_abi_tests as abi


RUNTIME_DIR = os.path.join(abi.SUPPORT_ROOT, "tests", "runtime")
TESTS = [
    "test_runtime_startup_headers.c",
    "test_runtime_arith8_16.c",
    "test_runtime_arith32.c",
    "test_runtime_arith64.c",
    "test_runtime_string.c",
    "test_runtime_ctype.c",
    "test_runtime_abs_atoi.c",
    "test_runtime_strtol.c",
    "test_runtime_stdlib_algo.c",
    "test_runtime_pointer_truth.c",
    "test_runtime_sprintf.c",
    "test_runtime_vsprintf.c",
    "test_runtime_output.c",
]
CONTROL_START = 0xFF00
CONTROL_END = 0xFF07
EXPECTED_CONTROL_SYMBOLS = {
    "abi_test_status": 0xFF00,
    "abi_test_reserved_31": 0xFF01,
    "abi_test_fail_line": 0xFF02,
    "abi_test_extra": 0xFF04,
}
# model-small DSEG/OSEG pressure per test: arith64 needs the whole
# 0x10..0x7F direct window (0x10..0x1F is unused register-bank 2/3 space,
# the control block lives in XRAM); strtol needs 0x21
DATA_LOCS = {
    "test_runtime_arith64.c": 0x10,
    "test_runtime_strtol.c": 0x21,
}
REQUIRED_SYMBOLS = {
    "test_runtime_startup_headers.c": {
        "__sdcc_gsinit_startup", "__mcs51_genRAMCLEAR",
        "__mcs51_genXRAMCLEAR", "__mcs51_genXINIT",
    },
    "test_runtime_arith8_16.c": {
        "__mulint", "__divuint", "__moduint", "__divsint", "__modsint",
    },
    "test_runtime_arith32.c": {
        "__mullong", "__divulong", "__modulong", "__divslong",
        "__modslong",
    },
    "test_runtime_arith64.c": {
        "__mullonglong", "__divulonglong", "__modulonglong",
        "__divslonglong", "__modslonglong",
    },
    "test_runtime_string.c": {
        "___memcpy", "_memcmp", "_memmove", "_memset", "_strlen", "_strcpy",
        "_strncpy", "_strcmp", "_strncmp", "_strchr", "_strrchr",
    },
    "test_runtime_ctype.c": {
        "_isalnum", "_isalpha", "_iscntrl", "_isgraph", "_isprint",
        "_ispunct", "_isspace", "_isxdigit", "_tolower", "_toupper",
    },
    "test_runtime_abs_atoi.c": {"_abs", "_atoi"},
    "test_runtime_strtol.c": {"_strtol", "_strtoul"},
    "test_runtime_stdlib_algo.c": {"_qsort", "_bsearch"},
    "test_runtime_sprintf.c": {"_sprintf", "_vsprintf", "__print_format"},
    "test_runtime_vsprintf.c": {"_vsprintf", "__print_format"},
    "test_runtime_output.c": {"_printf", "_puts", "_putchar", "__print_format"},
}


def check_runtime_map(ihx: str, data_loc: int,
                      source_name: str) -> tuple[bool, str]:
    map_path = ihx.removesuffix(".ihx") + ".map"
    content = open(map_path).read()
    found = {}
    for line in content.splitlines():
        match = re.match(r"^\s*(?:D:\s+)?([0-9A-Fa-f]{4,8})\s+(_\S+)", line)
        if match:
            name = match.group(2).lstrip("_")
            if name in EXPECTED_CONTROL_SYMBOLS:
                found[name] = int(match.group(1), 16)
    bad = [
        f"{name}@{found.get(name, 'missing')}"
        for name, expected in EXPECTED_CONTROL_SYMBOLS.items()
        if found.get(name) != expected
    ]
    if bad:
        return False, f"XRAM control symbols have wrong layout: {bad}"
    if not re.search(rf"^DSEG\s*=\s*0x{data_loc:04x}\b", content,
                     re.MULTILINE | re.IGNORECASE):
        return False, f"DSEG origin is not 0x{data_loc:04x}"
    linked = set(re.findall(
        r"^\s*[CD]:\s+[0-9A-Fa-f]{4,8}\s+(_\S+)",
        content, re.MULTILINE))
    missing = sorted(REQUIRED_SYMBOLS.get(source_name, set()) - linked)
    if missing:
        return False, f"required target-library symbols are not linked: {missing}"
    bit_area = re.search(
        r"^BSEG_BYTES\s+([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{8})\b",
        content, re.MULTILINE)
    if bit_area:
        bit_end = int(bit_area.group(1), 16) + int(bit_area.group(2), 16)
        if bit_end > data_loc:
            return False, (f"BSEG byte storage ends at 0x{bit_end:02x}, "
                           f"overlapping DSEG origin 0x{data_loc:02x}")
    return True, ""


def run_test(source_name: str, model_flags: list[str]) -> tuple[bool, str]:
    with tempfile.TemporaryDirectory() as tmpdir:
        data_loc = DATA_LOCS.get(source_name, 0x30)
        ihx = os.path.join(tmpdir, source_name.removesuffix(".c") + ".ihx")
        command = [
            abi.SDCC_BIN,
            "-mmcs251",
            "--nostdlibcall",
            "-DABI_TEST_XDATA_CONTROL",
            "--data-loc", f"0x{data_loc:02x}",
            "-I", os.path.join(abi.SUPPORT_ROOT, "tests", "abi"),
            "-I", RUNTIME_DIR,
            "-o", ihx,
            os.path.join(RUNTIME_DIR, source_name),
        ] + model_flags
        result = subprocess.run(command, capture_output=True, text=True, errors="replace")
        if result.returncode != 0:
            return False, result.stdout + result.stderr
        ok, message = check_runtime_map(ihx, data_loc, source_name)
        if not ok:
            return False, f"control-area gate: {message}"
        return abi.run_ucsim_sim(
            ihx, timeout_steps=3000000, memory_type="xram",
            control_start=CONTROL_START, control_end=CONTROL_END)


def run_missing_symbol_gate() -> tuple[bool, str]:
    """Prove that the map gate fails closed when a required libc symbol is absent."""
    with tempfile.TemporaryDirectory() as tmpdir:
        ihx = os.path.join(tmpdir, "output.ihx")
        command = [
            abi.SDCC_BIN,
            "-mmcs251",
            "--nostdlibcall",
            "-DABI_TEST_XDATA_CONTROL",
            "--data-loc", "0x30",
            "-I", os.path.join(abi.SUPPORT_ROOT, "tests", "abi"),
            "-I", RUNTIME_DIR,
            "-o", ihx,
            os.path.join(RUNTIME_DIR, "test_runtime_output.c"),
            "--model-small",
        ]
        result = subprocess.run(command, capture_output=True, text=True, errors="replace")
        if result.returncode != 0:
            return False, result.stdout + result.stderr
        ok, message = check_runtime_map(ihx, 0x30, "test_runtime_output.c")
        if not ok:
            return False, f"positive control unexpectedly failed: {message}"

        map_path = ihx.removesuffix(".ihx") + ".map"
        content = open(map_path).read()
        mutated, count = re.subn(
            r"^\s*C:\s+[0-9A-Fa-f]{4,8}\s+_printf\b.*$", "", content,
            count=1, flags=re.MULTILINE)
        if count != 1:
            return False, "could not remove exactly one _printf definition"
        mutation_ihx = os.path.join(tmpdir, "missing-symbol.ihx")
        with open(mutation_ihx.removesuffix(".ihx") + ".map", "w") as stream:
            stream.write(mutated)
        ok, message = check_runtime_map(
            mutation_ihx, 0x30, "test_runtime_output.c")
        if ok or "_printf" not in message:
            return False, f"missing _printf was not rejected: {message}"
        return True, ""


def main() -> int:
    failures = []
    total = len(TESTS) * len(abi.MODELS) + 1
    passed = 0
    ok, message = run_missing_symbol_gate()
    print(f"{'PASS' if ok else 'FAIL'} runtime_map_missing_symbol_gate"
          f"{'' if ok else ': ' + message}")
    if ok:
        passed += 1
    else:
        failures.append("runtime_map_missing_symbol_gate")
    for source_name in TESTS:
        for model_name, model_flags in abi.MODELS:
            ok, message = run_test(source_name, model_flags)
            tag = f"{source_name} [{model_name}]"
            print(f"{'PASS' if ok else 'FAIL'} {tag}{'' if ok else ': ' + message}")
            if ok:
                passed += 1
            else:
                failures.append(tag)
    print(f"Summary: {passed}/{total} passed")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
