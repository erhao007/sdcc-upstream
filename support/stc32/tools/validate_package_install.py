#!/usr/bin/env python3
"""Extract an MT-5D package and exercise only the relocated installation."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform as host_platform
import re
import shutil
import subprocess
import tarfile
import tempfile
import zipfile
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath

from verify_release_assets import (
    PLATFORMS,
    digest,
    load_object,
    read_archive,
    safe_name,
    verify_platform,
)


SCHEMA = "openstc32.package-validation.v1"
EXAMPLE = Path(__file__).resolve().parents[1] / "tests/package/mt5d_smoke.c"
EPHEMERAL_SUFFIXES = {".log", ".tmp", ".temp", ".pyc", ".pyo"}
EPHEMERAL_PARTS = {"__pycache__", ".pytest_cache"}
BUILD_METADATA_SUFFIXES = {".la"}


def normalized_machine(value: str) -> str:
    return value.lower().replace("x86-64", "x86_64")


def executable(prefix: Path, relative: PurePosixPath) -> Path:
    path = prefix.joinpath(*relative.parts)
    if path.is_file():
        return path
    candidate = Path(str(path) + ".exe")
    if candidate.is_file():
        return candidate
    raise SystemExit(f"missing unpacked executable: {relative}")


def normalized_argv(argv: list[str], prefix: Path, work: Path) -> list[str]:
    replacements = (
        (str(prefix), "<unpacked-prefix>"),
        (str(work), "<work-dir>"),
    )
    result = []
    for value in argv:
        normalized = value
        for old, new in replacements:
            normalized = normalized.replace(old, new)
            normalized = normalized.replace(old.replace("/", "\\"), new)
        result.append(normalized.replace("\\", "/"))
    return result


def run_command(name: str, argv: list[str], prefix: Path, work: Path,
                env: dict[str, str]) -> tuple[dict, subprocess.CompletedProcess[str]]:
    try:
        result = subprocess.run(
            argv,
            cwd=work,
            env=env,
            input="",
            capture_output=True,
            text=True,
            errors="replace",
            timeout=180,
            check=False,
        )
    except (OSError, subprocess.SubprocessError) as exc:
        raise SystemExit(f"cannot run unpacked {name}: {exc}") from exc
    record = {
        "name": name,
        "argv": normalized_argv(argv, prefix, work),
        "returncode": result.returncode,
    }
    if result.returncode:
        output = (result.stdout + "\n" + result.stderr)[-2000:]
        raise SystemExit(f"unpacked {name} failed rc={result.returncode}: {output}")
    return record, result


def parse_ihex_start(path: Path) -> int:
    address_base = 0
    first: int | None = None
    saw_eof = False
    for line_no, line in enumerate(path.read_text(encoding="ascii").splitlines(), 1):
        if not line.startswith(":"):
            raise SystemExit(f"invalid IHX line {line_no}")
        try:
            record = bytes.fromhex(line[1:])
        except ValueError as exc:
            raise SystemExit(f"invalid IHX hex at line {line_no}") from exc
        if len(record) < 5 or record[0] != len(record) - 5 or sum(record) & 0xff:
            raise SystemExit(f"invalid IHX record at line {line_no}")
        count = record[0]
        address = (record[1] << 8) | record[2]
        record_type = record[3]
        payload = record[4:4 + count]
        if record_type == 0x00 and count:
            candidate = address_base + address
            first = candidate if first is None else min(first, candidate)
        elif record_type == 0x01:
            if count or address:
                raise SystemExit("malformed IHX EOF record")
            saw_eof = True
        elif record_type == 0x04:
            if count != 2 or address:
                raise SystemExit("malformed IHX extended-linear record")
            address_base = int.from_bytes(payload, "big") << 16
        elif record_type == 0x02:
            if count != 2 or address:
                raise SystemExit("malformed IHX extended-segment record")
            address_base = int.from_bytes(payload, "big") << 4
        elif record_type not in (0x03, 0x05):
            raise SystemExit(f"unsupported IHX record type 0x{record_type:02x}")
    if not saw_eof or first is None:
        raise SystemExit("IHX is empty or has no EOF record")
    return first


def forbidden_tokens(values: list[str]) -> list[bytes]:
    tokens: set[bytes] = set()
    for value in values:
        if not value:
            continue
        variants = {value, value.replace("\\", "/"), value.replace("/", "\\")}
        for variant in variants:
            encoded = variant.encode("utf-8", errors="strict")
            if len(encoded) >= 4:
                tokens.add(encoded)
    return sorted(tokens)


def verify_package_hygiene(members: dict[PurePosixPath, bytes],
                           forbidden: list[str]) -> None:
    tokens = forbidden_tokens(forbidden)
    for relative, data in members.items():
        if relative.suffix.lower() in EPHEMERAL_SUFFIXES or any(
                part in EPHEMERAL_PARTS for part in relative.parts):
            raise SystemExit(f"temporary file in package: {relative}")
        if relative.suffix.lower() in BUILD_METADATA_SUFFIXES:
            raise SystemExit(f"build metadata in package: {relative}")
        for token in tokens:
            if token in data:
                raise SystemExit(f"absolute build path in package: {relative}")


def executable_members(package: Path) -> set[PurePosixPath]:
    result: set[PurePosixPath] = set()
    if package.name.endswith(".tar.gz"):
        with tarfile.open(package, "r:gz") as archive:
            for member in archive.getmembers():
                relative = safe_name(member.name)
                if member.isfile() and member.mode & 0o111:
                    result.add(relative)
    elif package.suffix == ".zip":
        with zipfile.ZipFile(package) as archive:
            for member in archive.infolist():
                relative = safe_name(member.filename)
                mode = (member.external_attr >> 16) & 0xFFFF
                if not member.is_dir() and mode & 0o111:
                    result.add(relative)
    else:
        raise SystemExit(f"unsupported package format: {package}")
    return result


def extract_members(members: dict[PurePosixPath, bytes], prefix: Path,
                    executables: set[PurePosixPath]) -> None:
    for relative, data in members.items():
        target = prefix.joinpath(*relative.parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        target.chmod(0o755 if relative in executables else 0o644)


def first_version_line(output: str, marker: str | None = None) -> str:
    lines = [line.strip() for line in output.splitlines() if line.strip()]
    if marker:
        lines = [line for line in lines if marker in line]
    if not lines:
        raise SystemExit("version command returned no usable version")
    return lines[0]


def validate(asset_dir: Path, tag: str, source_commit: str, platform: str,
             native_compiler: str, forbidden: list[str], output: Path) -> dict:
    verify_platform(asset_dir, tag, source_commit, platform)
    stem = f"openstc32-toolchain-{tag}-{platform}"
    extension = PLATFORMS[platform][2]
    package = asset_dir / f"{stem}{extension}"
    release = load_object(asset_dir / f"{stem}.release.json")
    toolchain = load_object(asset_dir / f"{stem}.toolchain.json")
    members = read_archive(package)
    verify_package_hygiene(members, forbidden)
    executables = executable_members(package)

    expected_os, expected_arch, _ = PLATFORMS[platform]
    actual_os = host_platform.system()
    actual_arch = host_platform.machine()
    if (
        actual_os != expected_os
        or normalized_machine(actual_arch) != normalized_machine(expected_arch)
    ):
        raise SystemExit(
            f"package platform does not match host: package={expected_os}/{expected_arch} "
            f"host={actual_os}/{actual_arch}"
        )

    with tempfile.TemporaryDirectory(prefix="openstc32-mt5d-") as temporary:
        root = Path(temporary)
        prefix = root / "unpacked"
        work = root / "work"
        prefix.mkdir()
        work.mkdir()
        extract_members(members, prefix, executables)

        compiler_record = toolchain["tools"]["compiler"]
        compiler = executable(prefix, PurePosixPath(compiler_record["path"]))
        environment = dict(os.environ)
        environment.pop("SDCC_HOME", None)
        environment.pop("COMPILER_PATH", None)
        environment["PATH"] = str(prefix / "bin") + os.pathsep + environment.get("PATH", "")

        commands = []
        record, version_result = run_command(
            "installed-compiler-version", [str(compiler), "--version"],
            prefix, work, environment)
        commands.append(record)
        compiler_version = first_version_line(
            version_result.stdout + "\n" + version_result.stderr, "SDCC :")
        if compiler_version != toolchain.get("sdcc_version"):
            raise SystemExit("unpacked compiler version does not match package manifest")

        native_path = shutil.which(native_compiler)
        if not native_path:
            raise SystemExit(f"native compiler not found: {native_compiler}")
        record, native_result = run_command(
            "native-compiler-version", [native_path, "--version"],
            prefix, work, environment)
        record["argv"][0] = f"<native-compiler>/{Path(native_path).name}"
        commands.append(record)
        native_version = first_version_line(native_result.stdout + "\n" + native_result.stderr)

        source = work / EXAMPLE.name
        shutil.copyfile(EXAMPLE, source)
        output_ihx = work / "mt5d_smoke.ihx"
        compile_argv = [
            str(compiler), "-mstc32", "--model-small",
            "--code-loc", "0xFF0000", str(source), "-o", str(output_ihx),
        ]
        record, _ = run_command(
            "compile-clean-room-example", compile_argv, prefix, work, environment)
        commands.append(record)
        if not output_ihx.is_file():
            raise SystemExit("unpacked compiler produced no IHX output")
        code_start = parse_ihex_start(output_ihx)
        if code_start != 0xFF0000:
            raise SystemExit(f"unexpected package-smoke code start: 0x{code_start:06x}")

        evidence = {
            "schema": SCHEMA,
            "roadmap_id": "MT-5D",
            "validated_at_utc": datetime.now(timezone.utc).isoformat(),
            "platform": platform,
            "package": package.name,
            "package_sha256": digest(package),
            "source_commit": source_commit,
            "source_state_sha256": release["source_state_sha256"],
            "host": {
                "system": actual_os,
                "machine": actual_arch,
                "release": host_platform.release(),
                "version": host_platform.version(),
                "platform": host_platform.platform(),
                "runner_os": os.environ.get("RUNNER_OS"),
                "runner_arch": os.environ.get("RUNNER_ARCH"),
                "image_os": os.environ.get("ImageOS"),
                "image_version": os.environ.get("ImageVersion"),
            },
            "native_compiler": {
                "command": commands[1]["argv"],
                "returncode": commands[1]["returncode"],
                "version": native_version,
            },
            "installed_compiler": {
                "command": commands[0]["argv"],
                "returncode": commands[0]["returncode"],
                "version": compiler_version,
                "sha256": compiler_record["sha256"],
            },
            "commands": commands,
            "example": {
                "source": EXAMPLE.relative_to(Path(__file__).resolve().parents[3]).as_posix(),
                "source_sha256": hashlib.sha256(EXAMPLE.read_bytes()).hexdigest(),
                "output": "<work-dir>/mt5d_smoke.ihx",
                "output_sha256": digest(output_ihx),
                "code_start": code_start,
            },
        }

    output.write_text(json.dumps(evidence, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(
        f"validated unpacked package: {platform} package_sha256={evidence['package_sha256']} "
        f"compiler={compiler_version} evidence={output}"
    )
    return evidence


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("asset_dir", type=Path)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--platform", choices=sorted(PLATFORMS), required=True)
    parser.add_argument("--native-compiler", default="cc")
    parser.add_argument("--forbid-path", action="append", default=[])
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9._-]+", args.tag):
        raise SystemExit(f"unsafe package tag: {args.tag!r}")
    if not re.fullmatch(r"[0-9a-fA-F]{40}", args.source_commit):
        raise SystemExit("source commit must be a full 40-character Git object ID")
    asset_dir = args.asset_dir.resolve()
    stem = f"openstc32-toolchain-{args.tag}-{args.platform}"
    output = asset_dir / f"{stem}.validation.json"
    validate(
        asset_dir, args.tag, args.source_commit, args.platform,
        args.native_compiler, args.forbid_path, output)


if __name__ == "__main__":
    main()
