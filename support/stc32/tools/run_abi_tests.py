#!/usr/bin/env python3
"""run_abi_tests.py - Hardened MCS-251 ABI automated test suite and contract gate.

Features:
1. Exact Scope-Aware Stack Discipline & Pattern Contract Gate:
   - Direct call (ecall) and return (eret);
   - Indirect 24-bit call (mov dr28 + ecall @dr28);
   - Struct return via hidden destination pointer (@dr28) asserted per-function
     (_make_point, _pass_small_struct_by_value, _process_packet, _fill_block);
   - #pragma callee_saves: verifies exact prologue entry push and epilogue exit pop pairs around eret;
   - Scope-level Non-leaf ISR: verifies the full 24-register save set (bits, acc, b, dpl, dph,
     dpxl, dr20, dr24, dr28, psw, r8/r9/r12..r15, (0+0)..(0+7)) with strict 100% LIFO inverse pairing and final reti.
2. Cross-Language Hand-Written Assembly Boundaries:
   - Verifies 1-8 byte scalar slots (DPL, DPH:DPL, B:DPH:DPL, A:B:DPH:DPL, R4..R7);
   - Generated-code asmop-width gate: a _BitInt(56) call marshals its 7-byte
     argument through R6:R5:R4:A:B:DPH:DPL WITHOUT materializing R7, while the
     long long control MUST use R7 (an 8-register asmop regression is invisible
     to runtime tests because caller and callee agree on the extra byte);
   - Verifies __bit parameter passing in ordinary mode (BSEG _PARM_1) and reentrant mode (b0);
   - Verifies __bit return via CY flag;
   - Verifies ASM caller to C callee bidirectional interoperability.
3. 24-bit High-CODE Region Function Pointer Invocation:
   - Verifies indirect calls into Region 1 (0x010900) on uCsim.
4. Strict Negative Double-Precision Contract Gate (No Fallback):
   - Strict assertion on '--double-8' unknown option warning 117;
   - Strict assertion on 'double' / 'long double' type downgrade warning 93;
   - Verifies runtime sizeof(double) == 4 and float arithmetic on uCsim.
5. All tests run across memory models: model-small, model-large, stack-auto.
"""

import os
import sys
import subprocess
import tempfile
import re

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
SUPPORT_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
PROJECT_ROOT = os.path.abspath(os.path.join(SUPPORT_ROOT, "..", ".."))
TOOLCHAIN_ROOT = os.environ.get("STC32_TOOLCHAIN_ROOT",
                                os.path.join(PROJECT_ROOT, "build", "install"))
SDCC_BIN = os.environ.get("STC32_SDCC",
                          os.path.join(TOOLCHAIN_ROOT, "bin", "sdcc"))
SDAS_BIN = os.environ.get("STC32_SDAS251",
                          os.path.join(TOOLCHAIN_ROOT, "bin", "sdas251"))
SDLD_BIN = os.environ.get("STC32_SDLD",
                          os.path.join(TOOLCHAIN_ROOT, "bin", "sdld"))
UCSIM_BIN = os.environ.get("STC32_UCSIM",
                           os.path.join(TOOLCHAIN_ROOT, "bin", "ucsim_51"))
if os.name == "nt":  # Windows toolchain binaries carry an .exe suffix
    SDCC_BIN += ".exe"
    SDAS_BIN += ".exe"
    SDLD_BIN += ".exe"
    UCSIM_BIN += ".exe"
TESTS_DIR = os.path.join(SUPPORT_ROOT, "tests", "abi")
CONTROL_START = 0x30
CONTROL_END = 0x37
DATA_LOC = 0x38
EXPECTED_CONTROL_SYMBOLS = {
    "abi_test_status": 0x30,
    "abi_test_reserved_31": 0x31,
    "abi_test_fail_line": 0x32,
    "abi_test_extra": 0x34,
}

MODELS = [
    ("model-small", ["--model-small"]),
    ("model-large", ["--model-large"]),
    ("stack-auto", ["--stack-auto"]),
]


def materialize_signed_asm(source: str, destination: str, model_flags: list) -> None:
    """Copy a hand-written source and bind it to the selected ABI variant."""
    model = "large" if "--model-large" in model_flags else "small"
    stack_auto = 1 if "--stack-auto" in model_flags else 0
    signature = (
        "stc32-mcs251 abi-major=1 abi-minor=0 target=mcs251 "
        f"model={model} stack-auto={stack_auto} xstack=0 "
        f"intlong-reent={stack_auto} float-reent={stack_auto} "
        "reg-params=1 all-callee-saves=0 sdcccall=2 "
        "regset=r0-r9,r12-r15 compiler-build=mcs251-abi1.0-r1"
    )

    with open(source, "r") as source_file:
        content = source_file.read()
    if ".optsdcc" in content:
        raise RuntimeError(f"hand-written assembly already contains .optsdcc: {source}")
    with open(destination, "w") as destination_file:
        destination_file.write(f"\t.optsdcc {signature}\n{content}")


def verify_asm_contract(c_file: str, asm_content: str) -> tuple:
    """Perform rigorous, scope-aware assembly contract checks."""
    if c_file == "test_abi_calling.c":
        if not re.search(r"\becall\b", asm_content):
            return False, "Missing ecall for direct C call"
        if not re.search(r"\beret\b", asm_content):
            return False, "Missing eret for function return"

        # Check #pragma callee_saves exact prologue/epilogue preservation
        m_cs = re.search(r"_func_callee_saves_calc:(.*?)(?:\n_[a-zA-Z0-9_]+:|\Z)", asm_content, re.S)
        if not m_cs:
            return False, "Could not find _func_callee_saves_calc scope"
        cs_body = m_cs.group(1).strip()

        # Find initial prologue pushes (before nested function calls or body)
        lines = [ln.strip() for ln in cs_body.splitlines() if ln.strip() and not ln.strip().startswith(";")]
        prologue_pushes = []
        for ln in lines:
            if ln.startswith("push\t") or ln.startswith("push "):
                prologue_pushes.append(ln.split()[1])
            else:
                break

        if not prologue_pushes:
            return False, "Callee-saves function must generate own prologue register pushes at entry"

        # Explicitly assert required registers ar7 and ar6 are saved by this fixture
        if not {"ar7", "ar6"}.issubset(set(prologue_pushes)):
            return False, f"Callee-saves prologue pushes {prologue_pushes} must include ar7 and ar6"

        # Find epilogue pops immediately preceding eret
        # Collect lines backwards up from eret
        epilogue_lines = []
        found_eret = False
        for ln in reversed(lines):
            if ln == "eret":
                found_eret = True
                continue
            if found_eret:
                if ln.startswith("pop\t") or ln.startswith("pop "):
                    epilogue_lines.append(ln.split()[1])
                else:
                    break

        # epilogue_lines was collected from bottom to top; reverse to get forward pop sequence
        epilogue_pops = list(reversed(epilogue_lines))

        if not epilogue_pops:
            return False, "Callee-saves function must generate own epilogue register pops before eret"

        if not {"ar7", "ar6"}.issubset(set(epilogue_pops)):
            return False, f"Callee-saves epilogue pops {epilogue_pops} must include ar7 and ar6"

        if epilogue_pops != list(reversed(prologue_pushes)):
            return False, f"Callee-saves prologue pushes {prologue_pushes} and epilogue pops {epilogue_pops} do not strictly invert"

    elif c_file == "test_abi_funcptr.c":
        if not re.search(r"mov\s+dr28,", asm_content):
            return False, "Missing mov dr28 for 24-bit function pointer load"
        if not re.search(r"ecall\s+@dr28", asm_content):
            return False, "Missing ecall @dr28 for indirect call"

    elif c_file == "test_abi_struct_return.c":
        # Check all struct functions individually for @dr28 store (including pass_small_struct_by_value)
        for func_name in ["_make_point", "_pass_small_struct_by_value", "_process_packet", "_fill_block"]:
            m_func = re.search(rf"{func_name}:(.*?)(?:\n_[a-zA-Z0-9_]+:|\Z)", asm_content, re.S)
            if not m_func:
                return False, f"Could not find function scope for {func_name}"
            func_body = m_func.group(1)
            if not re.search(r"mov\s+@dr28", func_body):
                return False, f"Struct function {func_name} must store aggregate return via @dr28 hidden pointer"

    elif c_file == "test_abi_isr.c":
        # 1. Extract _isr_leaf scope
        m_leaf = re.search(r"_isr_leaf:(.*?)(?:\n_[a-zA-Z0-9_]+:|\Z)", asm_content, re.S)
        if not m_leaf:
            return False, "Could not find _isr_leaf function scope"
        leaf_body = m_leaf.group(1).strip()
        leaf_lines = [ln.strip() for ln in leaf_body.splitlines() if ln.strip() and not ln.strip().startswith(";")]
        if not leaf_lines or leaf_lines[-1] != "reti":
            return False, f"Leaf ISR must end with reti as final instruction (got {leaf_lines[-1] if leaf_lines else 'empty'})"

        # 2. Extract _isr_non_leaf scope and verify complete 24-register save set & LIFO stack discipline
        m_nonleaf = re.search(r"_isr_non_leaf:(.*?)(?:\n_[a-zA-Z0-9_]+:|\Z)", asm_content, re.S)
        if not m_nonleaf:
            return False, "Could not find _isr_non_leaf function scope"
        nonleaf_body = m_nonleaf.group(1).strip()
        nonleaf_lines = [ln.strip() for ln in nonleaf_body.splitlines() if ln.strip() and not ln.strip().startswith(";")]
        if not nonleaf_lines or nonleaf_lines[-1] != "reti":
            return False, f"Non-leaf ISR must end with reti as final instruction (got {nonleaf_lines[-1] if nonleaf_lines else 'empty'})"

        # Extract all push and pop operands in order
        pushes = re.findall(r"\bpush\s+([^\s;]+)", nonleaf_body)
        pops = re.findall(r"\bpop\s+([^\s;]+)", nonleaf_body)

        # Full 24-register required set:
        # Fixed: bits, acc, b, dpl, dph, dpxl, dr20, dr24, dr28, psw
        # Working: (0+0)..(0+7) (or ar0..ar7)
        # Extended: r8, r9, r12, r13, r14, r15
        # (DR24/DR28: raw far-pointer/aggregate/indirect-call temporaries;
        #  DR20+DR24 also used by the runtime's __setjmp register dump)
        required_registers = {
            "bits", "acc", "b", "dpl", "dph", "dpxl",
            "dr20", "dr24", "dr28", "psw",
            "r8", "r9", "r12", "r13", "r14", "r15",
            "(0+0)", "(0+1)", "(0+2)", "(0+3)", "(0+4)", "(0+5)", "(0+6)", "(0+7)",
        }
        push_set = set(pushes)
        missing = required_registers - push_set
        if missing:
            return False, f"Non-leaf ISR missing required register save in push sequence: {missing}"

        # Verify strict LIFO inverse pairing
        expected_pops = list(reversed(pushes))
        if pops != expected_pops:
            return False, f"Non-leaf ISR pop sequence does not strictly invert push sequence:\nPushes: {pushes}\nPops:   {pops}"

    return True, "OK"


def run_ucsim_sim(ihx_path: str, timeout_steps=100000,
                  memory_type="iram", control_start=CONTROL_START,
                  control_end=CONTROL_END) -> tuple:
    """Run an IHX file in uCsim and parse the IRAM control block."""
    ucsim_cmd = [
        UCSIM_BIN,
        "-t251",
        "-c", "-",
        "-m",
        "-S", f"in={os.devnull},out=-",
        # uCsim splits positional file args on ':' (file:addr), so
        # drive-letter absolute paths never load on Windows; use a
        # relative name from the file's own directory.
        os.path.basename(ihx_path),
    ]

    script_input = (
        "set error unknown_code on\n"
        "set opt selfjump_stop 0\n"
        f"step {timeout_steps} vclk\n"
        f"dump {memory_type} 0x{control_start:x} 0x{control_end:x}\n"
        "quit\n"
    )

    proc = subprocess.run(ucsim_cmd, input=script_input,
                          cwd=os.path.dirname(os.path.abspath(ihx_path)),
                          capture_output=True, text=True, errors="replace", timeout=20)
    output = proc.stdout + proc.stderr

    for line in output.splitlines():
        line_str = line.strip()
        address = f"0x{control_start:x}"
        if line_str.lower().startswith(address):
            parts = line_str.split()
            if len(parts) >= 2:
                status_byte = parts[1].lower()
                if status_byte == "55":
                    return True, "PASS"
                elif status_byte == "ee":
                    # TEST_FAIL_LINE lives at 0x32..0x33, big-endian;
                    # parts[1..8] are the bytes dumped from 0x30 upward.
                    fail_line = (f"0x{parts[3]}{parts[4]}"
                                 if len(parts) >= 5 else "unknown")
                    return False, f"FAIL at line {fail_line}"

    return False, f"FAIL (Status byte not 0x55, snippet: {output[-300:]})"


def check_control_area_map(ihx_path: str) -> tuple:
    """Gate the exact control layout and keep ordinary DSEG above it."""
    map_path = ihx_path[:-4] + ".map"
    if not os.path.exists(map_path):
        return False, f"map file missing: {map_path}"
    found = {}
    bad = []
    map_content = open(map_path).read()
    for line in map_content.splitlines():
        # data symbol rows: "     0032  _g_dr_bad   module" (bare address,
        # no prefix, name prefixed with _); ignore area headers and the
        # "C:" code rows
        m = re.match(r"^\s*([0-9A-Fa-f]{4,8})\s+(_\S+)", line)
        if m:
            addr = int(m.group(1), 16)
            name = m.group(2).lstrip("_")
            if name in EXPECTED_CONTROL_SYMBOLS:
                found[name] = addr
            elif CONTROL_START <= addr <= CONTROL_END:
                bad.append(f"{name}@0x{addr:02x}")
    layout_errors = [
        f"{name}@{('missing' if name not in found else f'0x{found[name]:02x}')}"
        f" (expected 0x{expected:02x})"
        for name, expected in EXPECTED_CONTROL_SYMBOLS.items()
        if found.get(name) != expected
    ]
    if layout_errors:
        return False, f"control symbols have wrong layout: {layout_errors}"
    if bad:
        return False, (f"data symbols start inside the 0x30..0x37 control block: "
                       f"{bad}")
    if not re.search(rf"^DSEG\s*=\s*0x{DATA_LOC:04x}\b", map_content,
                     re.MULTILINE | re.IGNORECASE):
        return False, f"DSEG origin is not 0x{DATA_LOC:04x}"
    return True, ""


def run_c_test(c_file: str, model_name: str, model_flags: list) -> tuple:
    """Compile C test, verify ASM patterns, and execute in uCsim."""
    with tempfile.TemporaryDirectory() as tmpdir:
        src_path = os.path.join(TESTS_DIR, c_file)
        base_name = os.path.splitext(c_file)[0]
        asm_path = os.path.join(tmpdir, f"{base_name}.asm")
        ihx_path = os.path.join(tmpdir, f"{base_name}.ihx")

        # 1. Compile to ASM (-S)
        cmd_asm = [
            SDCC_BIN,
            "-mmcs251",
            "-S",
            "--data-loc", f"0x{DATA_LOC:02x}",
            "-I", TESTS_DIR,
            "-o", asm_path,
            src_path,
        ] + model_flags

        res_asm = subprocess.run(cmd_asm, capture_output=True, text=True, errors="replace")
        if res_asm.returncode != 0:
            return False, f"Compile (-S) error: {res_asm.stderr}"

        # 2. Gate assembly patterns
        asm_content = open(asm_path, "r").read()
        ok_asm, msg_asm = verify_asm_contract(c_file, asm_content)
        if not ok_asm:
            return False, f"ASM Gate violation: {msg_asm}"

        # 3. Compile to IHX
        cmd_ihx = [
            SDCC_BIN,
            "-mmcs251",
            "--data-loc", f"0x{DATA_LOC:02x}",
            "-I", TESTS_DIR,
            "-o", ihx_path,
            src_path,
        ] + model_flags

        res_ihx = subprocess.run(cmd_ihx, capture_output=True, text=True, errors="replace")
        if res_ihx.returncode != 0:
            return False, f"Compile error: {res_ihx.stderr}"

        ok_map, msg_map = check_control_area_map(ihx_path)
        if not ok_map:
            return False, f"Control-area gate: {msg_map}"

        return run_ucsim_sim(ihx_path)


def run_cross_asm_test(model_name: str, model_flags: list) -> tuple:
    """Assemble hand-written asm stubs, compile C caller, link and execute."""
    with tempfile.TemporaryDirectory() as tmpdir:
        # 1. Assemble abi_asm_stubs.s with sdas251
        asm_stub_src = os.path.join(TESTS_DIR, "abi_asm_stubs.s")
        asm_stub_rel = os.path.join(tmpdir, "abi_asm_stubs.rel")
        materialize_signed_asm(
            asm_stub_src, os.path.join(tmpdir, "abi_asm_stubs.s"), model_flags
        )

        res_as = subprocess.run([SDAS_BIN, "-plosg", "abi_asm_stubs.s"], cwd=tmpdir, capture_output=True, text=True, errors="replace")
        if res_as.returncode != 0 or not os.path.exists(asm_stub_rel):
            return False, f"sdas251 error on stubs: {res_as.stderr}"

        # 2. Compile test_abi_cross_asm.c to .rel
        c_src = os.path.join(TESTS_DIR, "test_abi_cross_asm.c")
        c_rel = os.path.join(tmpdir, "test_abi_cross_asm.rel")
        cmd_c = [
            SDCC_BIN,
            "-mmcs251",
            "-c",
            "-I", TESTS_DIR,
            "-o", c_rel,
            c_src,
        ] + model_flags
        res_c = subprocess.run(cmd_c, capture_output=True, text=True, errors="replace")
        if res_c.returncode != 0:
            return False, f"Compile test_abi_cross_asm.c error: {res_c.stderr}"

        # 3. Link with sdcc
        ihx_path = os.path.join(tmpdir, "cross_asm.ihx")
        cmd_lk = [
            SDCC_BIN,
            "-mmcs251",
            "--data-loc", f"0x{DATA_LOC:02x}",
            "-o", ihx_path,
            c_rel,
            asm_stub_rel,
        ] + model_flags
        res_lk = subprocess.run(cmd_lk, capture_output=True, text=True, errors="replace")
        if res_lk.returncode != 0 or not os.path.exists(ihx_path):
            return False, f"Link error: {res_lk.stderr}"

        ok_map, msg_map = check_control_area_map(ihx_path)
        if not ok_map:
            return False, f"Control-area gate: {msg_map}"

        return run_ucsim_sim(ihx_path)


def run_high_code_test(model_name: str, model_flags: list) -> tuple:
    """Assemble high_code_callee.s (at 0x010900), compile test_abi_high_code.c, link and execute."""
    with tempfile.TemporaryDirectory() as tmpdir:
        # 1. Assemble high_code_callee.s
        materialize_signed_asm(
            os.path.join(TESTS_DIR, "high_code_callee.s"),
            os.path.join(tmpdir, "high_code_callee.s"),
            model_flags,
        )
        res_as = subprocess.run([SDAS_BIN, "-plosg", "high_code_callee.s"], cwd=tmpdir, capture_output=True, text=True, errors="replace")
        if res_as.returncode != 0:
            return False, f"sdas251 error on high_code_callee: {res_as.stderr}"

        # 2. Compile test_abi_high_code.c to .rel
        c_src = os.path.join(TESTS_DIR, "test_abi_high_code.c")
        c_rel = os.path.join(tmpdir, "test_abi_high_code.rel")
        cmd_c = [
            SDCC_BIN,
            "-mmcs251",
            "-c",
            "-I", TESTS_DIR,
            "-o", c_rel,
            c_src,
        ] + model_flags
        res_c = subprocess.run(cmd_c, capture_output=True, text=True, errors="replace")
        if res_c.returncode != 0:
            return False, f"Compile test_abi_high_code.c error: {res_c.stderr}"

        # 3. Link with -Wl-b,MCS251REG1=0x010900
        ihx_path = os.path.join(tmpdir, "high_code.ihx")
        high_rel = os.path.join(tmpdir, "high_code_callee.rel")
        cmd_lk = [
            SDCC_BIN,
            "-mmcs251",
            "--data-loc", f"0x{DATA_LOC:02x}",
            "-Wl-bMCS251REG1=0x010900",
            "-o", ihx_path,
            c_rel,
            high_rel,
        ] + model_flags
        res_lk = subprocess.run(cmd_lk, capture_output=True, text=True, errors="replace")
        if res_lk.returncode != 0 or not os.path.exists(ihx_path):
            return False, f"Link error with MCS251REG1: {res_lk.stderr}"

        ok_map, msg_map = check_control_area_map(ihx_path)
        if not ok_map:
            return False, f"Control-area gate: {msg_map}"

        return run_ucsim_sim(ihx_path)


def run_asmop_width_gate(model_name: str, model_flags: list) -> tuple:
    """Generated-code gate: 7-byte asmop must not materialize R7; 8-byte must.

    The runtime cross-asm stubs cannot catch an 8-register 56-bit asmop
    (caller and callee agree on the extra byte), so this inspects the
    emitted marshalling of minimal constant-argument callers directly:
      - _call56 body must contain NO destination-write to r7
        (the "ar7 = 0x07" register-alias directives are not writes);
      - _call64 body MUST write r7 (positive control proving the gate
        observes R7 when the 8-byte asmop legitimately uses it).
    """
    src = (
        "extern void f56(unsigned _BitInt(56) x);\n"
        "extern void f64(unsigned long long x);\n"
        "void call56(void) { f56(0x11223344556677ULL); }\n"
        "void call64(void) { f64(0x1122334455667788ULL); }\n"
    )
    with tempfile.TemporaryDirectory() as tmpdir:
        src_path = os.path.join(tmpdir, "asmop_width.c")
        asm_path = os.path.join(tmpdir, "asmop_width.asm")
        with open(src_path, "w") as f:
            f.write(src)
        res = subprocess.run(
            [SDCC_BIN, "-mmcs251", "-S", "-o", asm_path, src_path] + model_flags,
            capture_output=True, text=True, errors="replace")
        if res.returncode != 0:
            return False, f"compile error: {res.stderr}"
        asm = open(asm_path).read()

    def body(name: str) -> str:
        m = re.search(rf"^{name}:(.*?)(?=^;-{{20,}}$|\Z)", asm,
                      re.MULTILINE | re.DOTALL)
        if not m:
            raise AssertionError(f"function {name} missing from generated asm")
        return m.group(1)

    r7_write = re.compile(r"^[ \t]*mov[ \t]+r7,", re.MULTILINE | re.IGNORECASE)
    b56, b64 = body("_call56"), body("_call64")
    if r7_write.search(b56):
        return False, "_BitInt(56) call materializes R7 (7-byte asmop regression)"
    if not r7_write.search(b64):
        return False, "long long call does not use R7 (positive control failed)"
    return True, ""


def run_caller_save_gate(model_name: str, model_flags: list) -> tuple:
    """Default caller-save path gate (allocator-managed registers).

    Two 16-bit locals held in registers across an external call must be
    pushed by the CALLER before the ecall and popped in strict LIFO
    order afterwards (per ic->rMask liveness, the default ABI contract;
    #pragma callee_saves is the per-function exception, gated by the
    ar7/ar6 entry/exit checks in verify_asm_contract).
    """
    src = (
        "extern void csink(void);\n"
        "unsigned int caller_save_path(unsigned char a, unsigned char b) {\n"
        "    unsigned int x = a + 1;\n"
        "    unsigned int y = b + 2;\n"
        "    csink();\n"
        "    return x + y;\n"
        "}\n"
    )
    with tempfile.TemporaryDirectory() as tmpdir:
        src_path = os.path.join(tmpdir, "caller_save.c")
        asm_path = os.path.join(tmpdir, "caller_save.asm")
        with open(src_path, "w") as f:
            f.write(src)
        res = subprocess.run(
            [SDCC_BIN, "-mmcs251", "-S", "-o", asm_path, src_path] + model_flags,
            capture_output=True, text=True, errors="replace")
        if res.returncode != 0:
            return False, f"compile error: {res.stderr}"
        asm = open(asm_path).read()

    m = re.search(r"^_caller_save_path:(.*?)(?=^;-{{20,}}$|\Z)", asm,
                  re.MULTILINE | re.DOTALL)
    if not m:
        return False, "function _caller_save_path missing from generated asm"
    body = m.group(1)
    lines = [ln.strip() for ln in body.splitlines()
             if ln.strip() and not ln.strip().startswith(";")]
    ecall_idx = next((i for i, ln in enumerate(lines)
                      if ln.split()[0].lower() == "ecall"), None)
    if ecall_idx is None:
        return False, "no ecall in _caller_save_path (test shape broken)"

    # Only the CONTIGUOUS push block immediately preceding the ecall and the
    # contiguous pop block immediately following it count as the caller-save
    # sequence; anything further away (prologue, argument or epilogue stack
    # traffic) must not satisfy this gate.
    pushes = []
    i = ecall_idx - 1
    while i >= 0 and lines[i].split()[0].lower() == "push":
        pushes.append(lines[i].split()[1])
        i -= 1
    pushes.reverse()
    pops = []
    i = ecall_idx + 1
    while i < len(lines) and lines[i].split()[0].lower() == "pop":
        pops.append(lines[i].split()[1])
        i += 1

    # The two live 16-bit locals x/y sit in ar7:ar6 and ar5:ar4 across the
    # call in every memory model - the pushed set must be exactly these.
    expected = {"ar7", "ar6", "ar5", "ar4"}
    if set(pushes) != expected:
        return False, (f"caller-save push set {sorted(set(pushes))} != expected "
                       f"live allocator registers {sorted(expected)} "
                       "(contiguous at the call site)")
    if pops != list(reversed(pushes)):
        return False, (f"caller-save pop sequence does not invert pushes: "
                       f"pushes={pushes} pops={pops}")
    return True, ""


def test_negative_double8_gate() -> tuple:
    """Rigorous Negative Gate: assert exact warnings and 4-byte float downgrade semantics without fallbacks."""
    with tempfile.TemporaryDirectory() as tmpdir:
        test_src = os.path.join(tmpdir, "test_double_downgrade.c")
        code = (
            '#include "abi_test.h"\n'
            'volatile double g_d = 1.25;\n'
            'void main(void) {\n'
            '    test_init();\n'
            '    ASSERT(sizeof(double) == 4);\n'
            '    ASSERT(sizeof(long double) == 4);\n'
            '    double d2 = g_d + 2.5;\n'
            '    ASSERT(d2 > 3.74 && d2 < 3.76);\n'
            '    test_pass();\n'
            '}\n'
        )
        open(test_src, "w").write(code)

        # 1. Assert option '--double-8' produces warning 117
        cmd_opt = [
            SDCC_BIN,
            "-mmcs251",
            "--double-8",
            "--data-loc", f"0x{DATA_LOC:02x}",
            "-I", TESTS_DIR,
            "-o", os.path.join(tmpdir, "opt.ihx"),
            test_src,
        ]
        res_opt = subprocess.run(cmd_opt, capture_output=True, text=True, errors="replace")
        combined_opt = res_opt.stdout + res_opt.stderr
        if "warning 117" not in combined_opt or "unknown compiler option '--double-8' ignored" not in combined_opt:
            return False, f"Expected warning 117 for unknown --double-8 option, got:\n{combined_opt}"

        # 2. Assert 'double' type produces warning: "types 'double', 'long double' not supported. Assuming 'float'"
        if "warning 93" not in combined_opt or "types 'double', 'long double' not supported. Assuming 'float'" not in combined_opt:
            return False, f"Expected warning 93 double downgrade warning, got:\n{combined_opt}"

        # 3. Assert the control layout before executing the generated image.
        opt_ihx = os.path.join(tmpdir, "opt.ihx")
        ok_map, msg_map = check_control_area_map(opt_ihx)
        if not ok_map:
            return False, f"Control-area gate: {msg_map}"

        # 4. Assert generated IHX executes correctly with sizeof(double)==4 in uCsim
        ok_sim, msg_sim = run_ucsim_sim(opt_ihx)
        if not ok_sim:
            return False, f"Downgraded float semantics failed in uCsim: {msg_sim}"

        return True, "PASS (Strict negative option & type downgrade assertions verified)"


def main():
    if not os.path.exists(SDCC_BIN):
        print(f"Error: SDCC not found at {SDCC_BIN}", file=sys.stderr)
        sys.exit(1)
    if not os.path.exists(UCSIM_BIN):
        print(f"Error: uCsim not found at {UCSIM_BIN}", file=sys.stderr)
        sys.exit(1)

    print("==================================================")
    print("MCS-251 ABI Automated Test Suite & Gate (ST-2)")
    print("==================================================")

    total_tests = 0
    passed_tests = 0
    failures = []

    # 1. Standard C ABI Tests across models (9 files × 3 models = 27 tests)
    standard_c_tests = [
        "test_abi_types.c",
        "test_abi_pointers.c",
        "test_abi_calling.c",
        "test_abi_struct_return.c",
        "test_abi_reentrant.c",
        "test_abi_varargs.c",
        "test_abi_funcptr.c",
        "test_abi_setjmp.c",
        "test_abi_isr.c",
    ]

    for test_file in standard_c_tests:
        for model_name, model_flags in MODELS:
            total_tests += 1
            ok, msg = run_c_test(test_file, model_name, model_flags)
            test_tag = f"{test_file} [{model_name}]"
            if ok:
                passed_tests += 1
                print(f"PASS {test_tag}")
            else:
                print(f"FAIL {test_tag}: {msg}")
                failures.append((test_tag, msg))

    # 2. Cross-Language Hand-Written Assembly Boundaries (1-8B, Bit params & Bit ret) (3 models)
    for model_name, model_flags in MODELS:
        total_tests += 1
        ok, msg = run_cross_asm_test(model_name, model_flags)
        test_tag = f"test_abi_cross_asm (Hand-written ASM 1-8B & Bit Stubs) [{model_name}]"
        if ok:
            passed_tests += 1
            print(f"PASS {test_tag}")
        else:
            print(f"FAIL {test_tag}: {msg}")
            failures.append((test_tag, msg))

    # 2b. Generated-code asmop width gate: 56-bit no-R7 / 64-bit R7 control (3 models)
    for model_name, model_flags in MODELS:
        total_tests += 1
        ok, msg = run_asmop_width_gate(model_name, model_flags)
        test_tag = f"asmop_width_gate (56-bit no-R7 / 64-bit R7 control) [{model_name}]"
        if ok:
            passed_tests += 1
            print(f"PASS {test_tag}")
        else:
            print(f"FAIL {test_tag}: {msg}")
            failures.append((test_tag, msg))

    # 2c. Default caller-save path gate: live regs pushed at call site, LIFO (3 models)
    for model_name, model_flags in MODELS:
        total_tests += 1
        ok, msg = run_caller_save_gate(model_name, model_flags)
        test_tag = f"caller_save_gate (live regs at call site, LIFO) [{model_name}]"
        if ok:
            passed_tests += 1
            print(f"PASS {test_tag}")
        else:
            print(f"FAIL {test_tag}: {msg}")
            failures.append((test_tag, msg))

    # 3. 24-bit High-CODE Region Function Pointer Invocation (3 models)
    for model_name, model_flags in MODELS:
        total_tests += 1
        ok, msg = run_high_code_test(model_name, model_flags)
        test_tag = f"test_abi_high_code (24-bit Region 1 @0x010900) [{model_name}]"
        if ok:
            passed_tests += 1
            print(f"PASS {test_tag}")
        else:
            print(f"FAIL {test_tag}: {msg}")
            failures.append((test_tag, msg))

    # 4. Strict Negative Contract Gate
    total_tests += 1
    ok, msg = test_negative_double8_gate()
    if ok:
        passed_tests += 1
        print("PASS negative_double8_gate (Strict Option & Downgrade Warnings + Execution)")
    else:
        print(f"FAIL negative_double8_gate: {msg}")
        failures.append(("negative_double8_gate", msg))

    # 5. MT-1A legacy register-allocator baseline gate (3 models, run by
    #    upstream/src/mcs251/tests/check-ralloc-baseline.py): behavioural
    #    S1-S9 samples plus allocator-invariant tripwires.  This freezes
    #    legacy behaviour for the ralloc2 migration; it is NOT an ABI
    #    requirement (a different register choice may still pass).
    total_tests += 1
    baseline_script = os.path.join(
        PROJECT_ROOT, "src", "mcs251", "tests",
        "check-ralloc-baseline.py")
    baseline_source = os.path.join(
        PROJECT_ROOT, "src", "mcs251", "tests",
        "ralloc-baseline.c")
    baseline_cmd = [
        sys.executable, baseline_script,
        "--sdcc", SDCC_BIN, "--s51", UCSIM_BIN,
        "--source", baseline_source,
    ]
    res_baseline = subprocess.run(
        baseline_cmd, capture_output=True, text=True, errors="replace", timeout=300)
    if res_baseline.returncode == 0:
        passed_tests += 1
        print("PASS ralloc_baseline_gate (MT-1A legacy allocator freeze, 3 models)")
    else:
        print("FAIL ralloc_baseline_gate: "
              f"{(res_baseline.stdout + res_baseline.stderr).strip()[-400:]}")
        failures.append(("ralloc_baseline_gate",
                         (res_baseline.stdout + res_baseline.stderr)
                         .strip()[-400:]))

    print("--------------------------------------------------")
    print(f"Summary: {passed_tests}/{total_tests} passed")
    if failures:
        print(f"{len(failures)} failures encountered:")
        for tag, msg in failures:
            print(f"  - {tag}: {msg}")
        sys.exit(1)
    else:
        print(f"ALL ABI & HARDENED CONTRACT GATE TESTS PASSED ({passed_tests}/{total_tests})!")
        sys.exit(0)


if __name__ == "__main__":
    main()
