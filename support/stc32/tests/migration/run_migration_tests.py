#!/usr/bin/env python3
"""MT-4A source migration matrix tests.

The runner intentionally tests source-level treatment only.  It does not load
vendor packages and it never treats a compiler's current acceptance of a
known-gap construct as support.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile


SCRIPT_DIR = Path(__file__).resolve().parent
ROOT = SCRIPT_DIR.parents[3]
MANIFEST_PATH = SCRIPT_DIR / "manifest.json"
ABI_TEST_DIR = ROOT / "support" / "stc32" / "tests" / "abi"

MODEL_FLAGS = {
    "small": [],
    "large": ["--model-large"],
    "stack-auto": ["--stack-auto"],
}


def load_manifest() -> dict:
    with MANIFEST_PATH.open(encoding="utf-8") as stream:
        manifest = json.load(stream)
    provenance = ROOT / manifest["provenance"]
    if not provenance.is_file():
        raise RuntimeError(f"provenance file is missing: {provenance}")
    return manifest


def resolve_executable(path: Path) -> Path:
    """Resolve extensionless tool paths on Windows/MSYS2 installations."""
    if path.is_file():
        return path
    if path.suffix.lower() != ".exe":
        windows_path = path.with_name(path.name + ".exe")
        if windows_path.is_file():
            return windows_path
    return path


def default_toolchain_binary(name: str) -> Path:
    configured_root = os.environ.get("STC32_TOOLCHAIN_ROOT")
    if configured_root:
        return resolve_executable(Path(configured_root) / "bin" / name)
    return resolve_executable(ROOT / "build" / "install" / "bin" / name)


def run(command: list[str], *, cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=cwd,
        capture_output=True,
        text=True,
        errors="replace",
    )


def compiler_command(sdcc: Path, source: Path, output: Path,
                     model: str, *, assembly: bool = False,
                     include_abi: bool = False,
                     include_dirs: list[Path] | None = None,
                     link: bool = False) -> list[str]:
    command = [str(sdcc), "-mmcs251"]
    if assembly:
        command.append("-S")
    elif not link:
        command.append("-c")
    if include_abi:
        command.extend(["--data-loc", "0x38", "-I", str(ABI_TEST_DIR)])
    for include_dir in include_dirs or []:
        command.extend(["-I", str(include_dir)])
    command.extend(["-o", str(output), str(source)])
    command.extend(MODEL_FLAGS[model])
    return command


def output_tail(result: subprocess.CompletedProcess[str]) -> str:
    combined = (result.stdout + result.stderr).strip()
    return combined[-500:] if combined else "<no diagnostic>"


def compile_one(sdcc: Path, source: Path, output: Path, model: str,
                *, assembly: bool = False,
                include_abi: bool = False,
                include_dirs: list[Path] | None = None,
                link: bool = False) -> subprocess.CompletedProcess[str]:
    return run(compiler_command(
        sdcc, source, output, model,
        assembly=assembly,
        include_abi=include_abi,
        include_dirs=include_dirs,
        link=link,
    ))


def artifact_error(result: subprocess.CompletedProcess[str], output: Path) -> str | None:
    if result.returncode != 0:
        return output_tail(result)
    if not output.is_file():
        return f"expected artifact is missing: {output.name}"
    if output.stat().st_size == 0:
        return f"expected artifact is empty: {output.name}"
    return None


def rejection_error(result: subprocess.CompletedProcess[str],
                    expected_token: str) -> str | None:
    if result.returncode == 0:
        return "legacy spelling unexpectedly compiled"
    diagnostic = "\n".join(
        line for line in (result.stdout + result.stderr).splitlines()
        if "error" in line.lower() or "syntax error" in line.lower()
    ).lower()
    expected = expected_token.lower()
    token_pattern = rf"['\"]{re.escape(expected)}['\"]"
    if not re.search(token_pattern, diagnostic):
        return f"missing diagnostic token {expected!r}: {output_tail(result)}"
    return None


def replace_token(text: str, token: str, replacement: str) -> str:
    pattern = re.compile(rf"(?<![A-Za-z0-9_]){re.escape(token)}(?![A-Za-z0-9_])")
    replaced, count = pattern.subn(replacement, text)
    if count != 1:
        raise RuntimeError(
            f"expected exactly one token {token!r}, replaced {count} in fixture"
        )
    return replaced


def check_assembly(case: dict, assembly_path: Path) -> tuple[bool, str]:
    expression = case.get("assembly_regex")
    if not expression:
        return True, ""
    content = assembly_path.read_text(encoding="utf-8", errors="replace")
    if not re.search(expression, content):
        return False, f"assembly does not match {expression!r}"
    return True, ""


def run_positive(case: dict, sdcc: Path, results: list[tuple[str, str]]) -> None:
    source = ROOT / "support" / "stc32" / "tests" / "migration" / case["source"]
    for model in case.get("models", []):
        tag = f"{case['id']} [{model}]"
        with tempfile.TemporaryDirectory(prefix="mt4a-positive-") as tmp:
            directory = Path(tmp)
            asm_path = directory / f"{case['id']}-{model}.asm"
            rel_path = directory / f"{case['id']}-{model}.rel"
            asm_result = compile_one(sdcc, source, asm_path, model, assembly=True)
            reason = artifact_error(asm_result, asm_path)
            if reason:
                results.append(("FAIL", f"{tag}: -S: {reason}"))
                continue
            ok, reason = check_assembly(case, asm_path)
            if not ok:
                results.append(("FAIL", f"{tag}: {reason}"))
                continue
            rel_result = compile_one(sdcc, source, rel_path, model)
            reason = artifact_error(rel_result, rel_path)
            if reason:
                results.append(("FAIL", f"{tag}: -c: {reason}"))
                continue
            results.append(("PASS", tag))


def run_mechanical(case: dict, sdcc: Path, results: list[tuple[str, str]]) -> None:
    source = ROOT / "support" / "stc32" / "tests" / "migration" / case["source"]
    original = source.read_text(encoding="utf-8")
    token = case["token"]
    replacement = case["replacement"]
    try:
        migrated = replace_token(original, token, replacement)
    except RuntimeError as error:
        results.append(("FAIL", f"{case['id']}: {error}"))
        return

    with tempfile.TemporaryDirectory(prefix="mt4a-mechanical-") as tmp:
        directory = Path(tmp)
        migrated_source = directory / source.name
        migrated_source.write_text(migrated, encoding="utf-8")
        legacy_rel = directory / "legacy.rel"
        legacy_result = compile_one(sdcc, source, legacy_rel, "small")
        tag = case["id"]
        reason = rejection_error(legacy_result, case["diagnostic_token"])
        if reason:
            results.append(("FAIL", f"{tag}: {reason}"))
            return
        for model in MODEL_FLAGS:
            migrated_rel = directory / f"migrated-{model}.rel"
            migrated_result = compile_one(sdcc, migrated_source, migrated_rel, model)
            reason = artifact_error(migrated_result, migrated_rel)
            if reason:
                results.append(("FAIL", f"{tag} replacement [{model}]: {reason}"))
                return
    results.append(("PASS", f"{tag} bare-reject + {token}->{replacement}"))


def run_compat_header(case: dict, sdcc: Path,
                      results: list[tuple[str, str]]) -> None:
    source = SCRIPT_DIR / case["source"]
    mapped_source = SCRIPT_DIR / case["mapped_source"]
    with tempfile.TemporaryDirectory(prefix="mt4a-compat-") as tmp:
        directory = Path(tmp)
        legacy_result = compile_one(
            sdcc, source, directory / "legacy.rel", "small"
        )
        tag = case["id"]
        reason = rejection_error(legacy_result, case["diagnostic_token"])
        if reason:
            results.append(("FAIL", f"{tag}: {reason}"))
            return
        for model in case.get("models", []):
            mapped_rel = directory / f"mapped-{model}.rel"
            mapped_result = compile_one(sdcc, mapped_source, mapped_rel, model)
            reason = artifact_error(mapped_result, mapped_rel)
            if reason:
                results.append(("FAIL", f"{tag} compat-header [{model}]: {reason}"))
                return
    results.append(("PASS", f"{tag} legacy-reject + compat-header"))


def run_syntax_normalization(case: dict, sdcc: Path,
                             results: list[tuple[str, str]]) -> None:
    source = ROOT / "support" / "stc32" / "tests" / "migration" / case["source"]
    original = source.read_text(encoding="utf-8")
    legacy = original
    try:
        for normalization in case["legacy_replacements"]:
            before = normalization["from"]
            after = normalization["to"]
            legacy, count = re.subn(re.escape(before), after, legacy, count=1)
            if count != 1:
                raise RuntimeError(
                    f"expected exactly one normalization source {before!r}, replaced {count}"
                )
    except RuntimeError as error:
        results.append(("FAIL", f"{case['id']}: {error}"))
        return

    with tempfile.TemporaryDirectory(prefix="mt4a-syntax-") as tmp:
        directory = Path(tmp)
        legacy_source = directory / source.name
        legacy_source.write_text(legacy, encoding="utf-8")
        legacy_rel = directory / "legacy.rel"
        legacy_result = compile_one(
            sdcc, legacy_source, legacy_rel, "small",
            include_dirs=[source.parent],
        )
        tag = case["id"]
        reason = rejection_error(legacy_result, case["diagnostic_token"])
        if reason:
            results.append(("FAIL", f"{tag}: {reason}"))
            return

        normalized_source = directory / f"normalized-{source.name}"
        normalized_source.write_text(original, encoding="utf-8")
        for model in MODEL_FLAGS:
            normalized_rel = directory / f"normalized-{model}.rel"
            normalized_result = compile_one(
                sdcc, normalized_source, normalized_rel, model,
                include_dirs=[source.parent],
            )
            reason = artifact_error(normalized_result, normalized_rel)
            if reason:
                results.append(("FAIL", f"{tag} normalized [{model}]: {reason}"))
                return
    results.append(("PASS", f"{tag} legacy-reject + suffix-normalization"))


def check_control_map(ihx_path: Path) -> tuple[bool, str]:
    map_path = ihx_path.with_suffix(".map")
    if not map_path.is_file():
        return False, f"map file missing: {map_path}"
    content = map_path.read_text(encoding="utf-8", errors="replace")
    expected = {
        "abi_test_status": 0x30,
        "abi_test_reserved_31": 0x31,
        "abi_test_fail_line": 0x32,
        "abi_test_extra": 0x34,
    }
    found: dict[str, int] = {}
    for line in content.splitlines():
        match = re.match(r"^\s*([0-9A-Fa-f]{4,8})\s+(_\S+)", line)
        if not match:
            continue
        name = match.group(2).lstrip("_")
        if name in expected:
            found[name] = int(match.group(1), 16)
    wrong = [
        f"{name}@{found.get(name, 'missing')} expected 0x{address:02x}"
        for name, address in expected.items()
        if found.get(name) != address
    ]
    if wrong:
        return False, "control-area mismatch: " + ", ".join(wrong)
    if not re.search(r"^DSEG\s*=\s*0x0038\b", content,
                     flags=re.IGNORECASE | re.MULTILINE):
        return False, "DSEG origin is not 0x0038"
    return True, ""


def run_ucsim(ucsim: Path, ihx_path: Path, timeout_steps: int,
              expected_status: str) -> tuple[bool, str]:
    command = [
        str(ucsim), "-t251", "-c", "-", "-m",
        "-S", f"in={os.devnull},out=-", ihx_path.name,
    ]
    script = (
        "set error unknown_code on\n"
        "set opt selfjump_stop 0\n"
        f"step {timeout_steps} vclk\n"
        "dump iram 0x30 0x37\n"
        "quit\n"
    )
    result = subprocess.run(
        command,
        cwd=ihx_path.parent,
        input=script,
        capture_output=True,
        text=True,
        errors="replace",
        timeout=30,
    )
    output = result.stdout + result.stderr
    match = re.search(r"(?im)^\s*0x30\s+([0-9a-f]{2})\b", output)
    if not match:
        return False, f"uCsim status dump missing: {output[-500:]}"
    actual = "0x" + match.group(1).lower()
    if actual != expected_status.lower():
        return False, f"uCsim status {actual}, expected {expected_status}: {output[-300:]}"
    return True, ""


def run_behavior(case: dict, sdcc: Path, ucsim: Path | None,
                 skip_behavior: bool,
                 results: list[tuple[str, str]]) -> None:
    if skip_behavior:
        results.append(("SKIP", f"{case['id']}: behavior explicitly skipped"))
        return
    if ucsim is None or not ucsim.is_file():
        results.append(("FAIL", f"{case['id']}: uCsim is required for behavior evidence"))
        return
    source = ROOT / "support" / "stc32" / "tests" / "migration" / case["source"]
    for model in case.get("models", []):
        tag = f"{case['id']} [{model}]"
        with tempfile.TemporaryDirectory(prefix="mt4a-behavior-") as tmp:
            ihx_path = Path(tmp) / f"{case['id']}-{model}.ihx"
            compile_result = compile_one(
                sdcc, source, ihx_path, model, include_abi=True, link=True
            )
            reason = artifact_error(compile_result, ihx_path)
            if reason:
                results.append(("FAIL", f"{tag}: link: {reason}"))
                continue
            ok, reason = check_control_map(ihx_path)
            if not ok:
                results.append(("FAIL", f"{tag}: {reason}"))
                continue
            try:
                ok, reason = run_ucsim(
                    ucsim, ihx_path, case["timeout_steps"], case["expected_status"]
                )
            except subprocess.TimeoutExpired:
                ok, reason = False, "uCsim timed out"
            results.append(("PASS" if ok else "FAIL", f"{tag}: {reason}" if reason else tag))


def run_project_behavior(case: dict, sdcc: Path, ucsim: Path | None,
                         skip_behavior: bool,
                         results: list[tuple[str, str]]) -> None:
    if skip_behavior:
        results.append(("SKIP", f"{case['id']}: behavior explicitly skipped"))
        return
    if ucsim is None or not ucsim.is_file():
        results.append(("FAIL", f"{case['id']}: uCsim is required for behavior evidence"))
        return

    sources = [SCRIPT_DIR / source for source in case.get("sources", [])]
    if len(sources) < 2:
        results.append(("FAIL", f"{case['id']}: project case needs at least two sources"))
        return
    missing = [str(source) for source in sources if not source.is_file()]
    if missing:
        results.append(("FAIL", f"{case['id']}: source missing: {', '.join(missing)}"))
        return

    for model in case.get("models", []):
        tag = f"{case['id']} [{model}]"
        with tempfile.TemporaryDirectory(prefix="mt4a-project-") as tmp:
            directory = Path(tmp)
            rel_paths: list[Path] = []
            compile_failed = False
            include_dirs = sorted({source.parent for source in sources})
            include_dirs.append(ABI_TEST_DIR)
            for index, source in enumerate(sources):
                rel_path = directory / f"{index:02d}-{source.stem}.rel"
                compile_result = compile_one(
                    sdcc, source, rel_path, model, include_dirs=include_dirs
                )
                reason = artifact_error(compile_result, rel_path)
                if reason:
                    results.append((
                        "FAIL",
                        f"{tag}: compile {source.name}: {reason}",
                    ))
                    compile_failed = True
                    break
                rel_paths.append(rel_path)
            if compile_failed:
                continue

            ihx_path = directory / f"{case['id']}-{model}.ihx"
            link_command = [
                str(sdcc), "-mmcs251", "--data-loc", "0x38",
                *MODEL_FLAGS[model], "-o", str(ihx_path),
                *(str(path) for path in rel_paths),
            ]
            link_result = run(link_command)
            reason = artifact_error(link_result, ihx_path)
            if reason:
                results.append(("FAIL", f"{tag}: link: {reason}"))
                continue
            ok, reason = check_control_map(ihx_path)
            if not ok:
                results.append(("FAIL", f"{tag}: {reason}"))
                continue
            try:
                ok, reason = run_ucsim(
                    ucsim, ihx_path, case["timeout_steps"], case["expected_status"]
                )
            except subprocess.TimeoutExpired:
                ok, reason = False, "uCsim timed out"
            results.append(("PASS" if ok else "FAIL", f"{tag}: {reason}" if reason else tag))


def run_gap(case: dict, sdcc: Path, results: list[tuple[str, str]]) -> None:
    source = ROOT / "support" / "stc32" / "tests" / "migration" / case["source"]
    for model in case.get("models", []):
        tag = f"{case['id']} [{model}] status={case['status']}"
        with tempfile.TemporaryDirectory(prefix="mt4a-gap-") as tmp:
            asm_path = Path(tmp) / f"{case['id']}-{model}.asm"
            result = compile_one(sdcc, source, asm_path, model, assembly=True)
            reason = artifact_error(result, asm_path)
            if reason:
                results.append((
                    "FAIL",
                    f"{tag}: current expected acceptance changed: {reason}",
                ))
            else:
                results.append(("PASS", tag))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdcc", type=Path, default=default_toolchain_binary("sdcc"))
    parser.add_argument("--ucsim", type=Path, default=default_toolchain_binary("ucsim_51"))
    parser.add_argument(
        "--skip-behavior",
        action="store_true",
        help="skip the required uCsim behavior cases; result is explicitly SKIP",
    )
    args = parser.parse_args()
    args.sdcc = resolve_executable(args.sdcc)
    args.ucsim = resolve_executable(args.ucsim)
    manifest = load_manifest()
    if not args.sdcc.is_file():
        print(f"FAIL toolchain: sdcc not found: {args.sdcc}")
        return 1

    results: list[tuple[str, str]] = []
    for case in manifest["cases"]:
        kind = case["kind"]
        if kind == "positive":
            run_positive(case, args.sdcc, results)
        elif kind == "mechanical-replacement":
            run_mechanical(case, args.sdcc, results)
        elif kind == "compat-header":
            run_compat_header(case, args.sdcc, results)
        elif kind == "syntax-normalization":
            run_syntax_normalization(case, args.sdcc, results)
        elif kind == "behavior":
            run_behavior(case, args.sdcc, args.ucsim, args.skip_behavior, results)
        elif kind == "project-behavior":
            run_project_behavior(
                case, args.sdcc, args.ucsim, args.skip_behavior, results
            )
        elif kind == "diagnostic-gap":
            run_gap(case, args.sdcc, results)
        else:
            results.append(("FAIL", f"{case['id']}: unknown kind {kind!r}"))

    passed = sum(status == "PASS" for status, _ in results)
    skipped = sum(status == "SKIP" for status, _ in results)
    failures = [(status, message) for status, message in results if status == "FAIL"]
    for status, message in results:
        print(f"{status} {message}")
    print(f"Summary: {passed}/{len(results)} passed, {skipped} skipped")
    if failures:
        print(f"Failures: {len(failures)}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
