#!/usr/bin/env python3
"""Verify packaged OpenSTC32 assets before artifact upload or release."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import stat
import tarfile
import zipfile
from pathlib import Path, PurePosixPath, PureWindowsPath

from install_identity import (
    ARTIFACT_SCHEMA,
    RELEASE_SCHEMA,
    RUNTIME_MODELS,
    SOURCE_REPOSITORY,
    TOOLCHAIN_SCHEMA,
    TOOL_COMMANDS,
    records_sha256,
)

TOKEN = re.compile(r"^[A-Za-z0-9._-]+$")
PLATFORMS = {
    "linux-x86_64": ("Linux", "x86_64", ".tar.gz"),
    "macos-arm64": ("Darwin", "arm64", ".tar.gz"),
    "windows-x86_64": ("Windows", "AMD64", ".zip"),
}
METADATA_PATHS = {
    PurePosixPath("share/openstc32/toolchain.json"),
    PurePosixPath("share/openstc32/toolchain-artifacts.json"),
}
VALIDATION_SCHEMA = "openstc32.package-validation.v1"
MT5D_EXAMPLE = (
    Path(__file__).resolve().parents[1] / "tests/package/mt5d_smoke.c"
)


def digest_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest(path: Path) -> str:
    return digest_bytes(path.read_bytes())


def load_object(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise SystemExit(f"cannot read JSON {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise SystemExit(f"expected JSON object: {path}")
    return value


def safe_name(name: object) -> PurePosixPath:
    if not isinstance(name, str):
        raise SystemExit(f"unsafe archive member: {name!r}")
    relative = PurePosixPath(name)
    windows = PureWindowsPath(name)
    if (
        not name
        or "\\" in name
        or relative.is_absolute()
        or windows.is_absolute()
        or windows.drive
        or ".." in relative.parts
    ):
        raise SystemExit(f"unsafe archive member: {name!r}")
    return relative


def read_archive(path: Path) -> dict[PurePosixPath, bytes]:
    members: dict[PurePosixPath, bytes] = {}
    if path.name.endswith(".tar.gz"):
        with tarfile.open(path, "r:gz") as archive:
            for member in archive.getmembers():
                relative = safe_name(member.name)
                if not member.isfile():
                    raise SystemExit(f"non-file archive member: {relative}")
                if relative in members:
                    raise SystemExit(f"duplicate archive member: {relative}")
                source = archive.extractfile(member)
                if source is None:
                    raise SystemExit(f"cannot read archive member: {relative}")
                members[relative] = source.read()
    elif path.suffix == ".zip":
        with zipfile.ZipFile(path) as archive:
            for member in archive.infolist():
                relative = safe_name(member.filename)
                if member.is_dir():
                    raise SystemExit(f"directory archive member: {relative}")
                mode = (member.external_attr >> 16) & 0xFFFF
                if member.create_system != 3 or not stat.S_ISREG(mode):
                    raise SystemExit(f"non-file archive member: {relative}")
                if relative in members:
                    raise SystemExit(f"duplicate archive member: {relative}")
                members[relative] = archive.read(member)
    else:
        raise SystemExit(f"unsupported package format: {path}")
    return members


def verify_record(record: object, members: dict[PurePosixPath, bytes],
                  label: str) -> PurePosixPath:
    if not isinstance(record, dict):
        raise SystemExit(f"invalid {label} record")
    relative = safe_name(record.get("path"))
    data = members.get(relative)
    if data is None:
        raise SystemExit(f"missing packaged {label}: {relative}")
    if record.get("size") != len(data) or record.get("sha256") != digest_bytes(data):
        raise SystemExit(f"packaged {label} mismatch: {relative}")
    return relative


def verify_platform(asset_dir: Path, tag: str, commit: str, platform: str) -> None:
    stem = f"openstc32-toolchain-{tag}-{platform}"
    host_os, host_arch, extension = PLATFORMS[platform]
    release_path = asset_dir / f"{stem}.release.json"
    release = load_object(release_path)
    package_name = f"{stem}{extension}"
    if release.get("package") != package_name:
        raise SystemExit(f"unexpected package name in release metadata: {platform}")
    package = asset_dir / package_name
    toolchain_path = asset_dir / f"{stem}.toolchain.json"
    artifacts_path = asset_dir / f"{stem}.artifacts.json"
    checksum_path = asset_dir / f"{package.name}.sha256"

    expected = {
        "schema": RELEASE_SCHEMA,
        "tag": tag,
        "platform": platform,
        "source_commit": commit,
        "host_os": host_os,
        "host_arch": host_arch,
        "toolchain_manifest": toolchain_path.name,
        "artifact_manifest": artifacts_path.name,
    }
    for key, value in expected.items():
        if release.get(key) != value:
            raise SystemExit(f"release metadata mismatch for {platform}/{key}")
    for path in (package, toolchain_path, artifacts_path, checksum_path):
        if not path.is_file():
            raise SystemExit(f"missing release asset: {path.name}")

    package_digest = digest(package)
    if release.get("package_sha256") != package_digest:
        raise SystemExit(f"package digest mismatch: {package.name}")
    expected_checksum = f"{package_digest}  {package.name}\n"
    if checksum_path.read_text(encoding="ascii") != expected_checksum:
        raise SystemExit(f"checksum file mismatch: {checksum_path.name}")
    if release.get("toolchain_manifest_sha256") != digest(toolchain_path):
        raise SystemExit(f"toolchain sidecar digest mismatch: {platform}")
    if release.get("artifact_manifest_sha256") != digest(artifacts_path):
        raise SystemExit(f"artifact sidecar digest mismatch: {platform}")

    toolchain_bytes = toolchain_path.read_bytes()
    artifacts_bytes = artifacts_path.read_bytes()
    try:
        toolchain = json.loads(toolchain_bytes)
        artifacts = json.loads(artifacts_bytes)
    except json.JSONDecodeError as exc:
        raise SystemExit(f"invalid manifest JSON for {platform}: {exc}") from exc
    if not isinstance(toolchain, dict) or not isinstance(artifacts, dict):
        raise SystemExit(f"manifest is not an object: {platform}")
    if toolchain.get("schema") != TOOLCHAIN_SCHEMA:
        raise SystemExit(f"unsupported toolchain manifest schema: {platform}")
    if artifacts.get("schema") != ARTIFACT_SCHEMA:
        raise SystemExit(f"unsupported artifact manifest schema: {platform}")
    if toolchain.get("source_commit") != commit or artifacts.get("source_commit") != commit:
        raise SystemExit(f"source binding mismatch: {platform}")
    if toolchain.get("source_dirty") is not False or artifacts.get("source_dirty") is not False:
        raise SystemExit(f"dirty source recorded in release asset: {platform}")
    pair_fields = (
        "target",
        "architecture",
        "chip",
        "host_os",
        "host_arch",
        "sdcc_version",
        "source_commit",
        "source_dirty",
        "source_state_sha256",
        "source",
        "compatibility",
        "built_at_utc",
    )
    for field in pair_fields:
        if toolchain.get(field) != artifacts.get(field):
            raise SystemExit(f"manifest pair mismatch: {platform}/{field}")
    if (
        toolchain.get("host_os") != host_os
        or toolchain.get("host_arch", "").lower() != host_arch.lower()
    ):
        raise SystemExit(f"manifest platform identity mismatch: {platform}")
    if release.get("source_state_sha256") != toolchain.get("source_state_sha256"):
        raise SystemExit(f"release source-state mismatch: {platform}")
    if release.get("compatibility") != toolchain.get("compatibility"):
        raise SystemExit(f"release compatibility mismatch: {platform}")

    source = toolchain.get("source")
    if not isinstance(source, dict) or set(source) != {"root", "upstream"}:
        raise SystemExit(f"invalid source identity: {platform}")
    source_root = source.get("root")
    source_upstream = source.get("upstream")
    if not isinstance(source_root, dict) or source_root != source_upstream:
        raise SystemExit(f"source root/upstream mismatch: {platform}")
    if (
        set(source_root) != {"repository", "commit", "dirty", "state_sha256"}
        or source_root.get("repository") != SOURCE_REPOSITORY
        or source_root.get("commit") != commit
        or source_root.get("dirty") is not False
        or source_root.get("state_sha256") != toolchain.get("source_state_sha256")
    ):
        raise SystemExit(f"invalid source checkout identity: {platform}")

    expected_compatibility = {
        "abi": {"major": 1, "minor": 0},
        "memory_models": [
            {"model": values["memory_model"],
             "stack_auto": values["stack_auto"]}
            for values in RUNTIME_MODELS.values()
        ],
    }
    if toolchain.get("compatibility") != expected_compatibility:
        raise SystemExit(f"incompatible ABI or memory-model manifest: {platform}")
    if toolchain.get("artifact_manifest") != "share/openstc32/toolchain-artifacts.json":
        raise SystemExit(f"unexpected artifact manifest path: {platform}")
    if toolchain.get("artifact_manifest_sha256") != digest_bytes(artifacts_bytes):
        raise SystemExit(f"toolchain does not bind artifact manifest: {platform}")

    members = read_archive(package)
    if members.get(PurePosixPath("share/openstc32/toolchain.json")) != toolchain_bytes:
        raise SystemExit(f"packaged toolchain manifest mismatch: {platform}")
    if members.get(PurePosixPath("share/openstc32/toolchain-artifacts.json")) != artifacts_bytes:
        raise SystemExit(f"packaged artifact manifest mismatch: {platform}")
    entries = artifacts.get("files")
    if not isinstance(entries, list) or not entries:
        raise SystemExit(f"artifact manifest has no files: {platform}")
    artifact_paths: set[PurePosixPath] = set()
    for entry in entries:
        if not isinstance(entry, dict):
            raise SystemExit(f"artifact manifest contains a non-object entry: {platform}")
        relative = safe_name(entry.get("path"))
        if relative in METADATA_PATHS or relative in artifact_paths:
            raise SystemExit(f"unsafe or duplicate artifact path: {platform}/{relative}")
        artifact_paths.add(relative)
    if artifacts.get("files_sha256") != records_sha256(entries):
        raise SystemExit(f"artifact file-set digest mismatch: {platform}")
    expected_members = METADATA_PATHS | artifact_paths
    if set(members) != expected_members:
        raise SystemExit(
            f"package file boundary mismatch: {platform} "
            f"manifest={len(expected_members)} archive={len(members)}"
        )
    for entry in entries:
        relative = safe_name(entry["path"])
        data = members[relative]
        if entry.get("size") != len(data) or entry.get("sha256") != digest_bytes(data):
            raise SystemExit(f"packaged artifact mismatch: {platform}/{relative}")

    tools = toolchain.get("tools")
    if not isinstance(tools, dict) or set(tools) != set(TOOL_COMMANDS):
        raise SystemExit(f"incomplete host-tool identity: {platform}")
    for role, (name, _arguments, _codes, _marker) in TOOL_COMMANDS.items():
        record = tools[role]
        relative = verify_record(record, members, f"{role} tool")
        if relative not in {
            PurePosixPath("bin") / name,
            PurePosixPath("bin") / f"{name}.exe",
        } or not isinstance(record.get("version"), str) or not record["version"].strip():
            raise SystemExit(f"invalid {role} tool identity: {platform}")

    runtimes = toolchain.get("runtimes")
    if not isinstance(runtimes, dict) or set(runtimes) != set(RUNTIME_MODELS):
        raise SystemExit(f"incomplete runtime identity: {platform}")
    for model, attributes in RUNTIME_MODELS.items():
        record = runtimes[model]
        relative = verify_record(record, members, f"runtime {model}")
        if (
            relative != PurePosixPath(f"share/sdcc/lib/{model}/libsdcc.lib")
            or any(record.get(key) != value for key, value in attributes.items())
        ):
            raise SystemExit(f"runtime compatibility mismatch: {platform}/{model}")

    headers = toolchain.get("headers")
    if not isinstance(headers, dict) or headers.get("root") != "share/sdcc/include/mcs251":
        raise SystemExit(f"invalid header identity: {platform}")
    header_entries = headers.get("files")
    if not isinstance(header_entries, list) or not header_entries:
        raise SystemExit(f"missing header identity: {platform}")
    header_paths = {
        verify_record(record, members, "header") for record in header_entries
    }
    actual_headers = {
        path for path in members
        if path.parts[:4] == ("share", "sdcc", "include", "mcs251")
    }
    if header_paths != actual_headers:
        raise SystemExit(f"header file boundary mismatch: {platform}")
    if headers.get("files_sha256") != records_sha256(header_entries):
        raise SystemExit(f"header file-set digest mismatch: {platform}")
    print(f"verified release assets: {platform} files={len(members)}")


def verify_validation(asset_dir: Path, tag: str, commit: str, platform: str) -> None:
    stem = f"openstc32-toolchain-{tag}-{platform}"
    validation_path = asset_dir / f"{stem}.validation.json"
    validation = load_object(validation_path)
    release = load_object(asset_dir / f"{stem}.release.json")
    toolchain = load_object(asset_dir / f"{stem}.toolchain.json")
    package_name = release["package"]
    expected = {
        "schema": VALIDATION_SCHEMA,
        "roadmap_id": "MT-5D",
        "platform": platform,
        "package": package_name,
        "package_sha256": release["package_sha256"],
        "source_commit": commit,
        "source_state_sha256": release["source_state_sha256"],
    }
    for key, value in expected.items():
        if validation.get(key) != value:
            raise SystemExit(f"package validation mismatch for {platform}/{key}")

    host_os, host_arch, _ = PLATFORMS[platform]
    host = validation.get("host")
    if (
        not isinstance(host, dict)
        or host.get("system") != host_os
        or str(host.get("machine", "")).lower() != host_arch.lower()
        or any(not isinstance(host.get(key), str) or not host[key].strip()
               for key in ("release", "version", "platform"))
    ):
        raise SystemExit(f"package validation host mismatch: {platform}")

    native = validation.get("native_compiler")
    installed = validation.get("installed_compiler")
    compiler_record = toolchain["tools"]["compiler"]
    if (
        not isinstance(native, dict)
        or native.get("returncode") != 0
        or not isinstance(native.get("version"), str)
        or not native["version"].strip()
    ):
        raise SystemExit(f"invalid native compiler evidence: {platform}")
    if (
        not isinstance(installed, dict)
        or installed.get("returncode") != 0
        or installed.get("version") != toolchain.get("sdcc_version")
        or installed.get("sha256") != compiler_record.get("sha256")
    ):
        raise SystemExit(f"invalid installed compiler evidence: {platform}")

    commands = validation.get("commands")
    expected_names = [
        "installed-compiler-version",
        "native-compiler-version",
        "compile-clean-room-example",
    ]
    if (
        not isinstance(commands, list)
        or not all(isinstance(entry, dict) for entry in commands)
        or [entry.get("name") for entry in commands] != expected_names
    ):
        raise SystemExit(f"invalid package command evidence: {platform}")
    for entry in commands:
        argv = entry.get("argv")
        if entry.get("returncode") != 0 or not isinstance(argv, list) or not argv:
            raise SystemExit(f"failed package command evidence: {platform}")
        for value in argv:
            if not isinstance(value, str) or not value:
                raise SystemExit(f"invalid package command argument: {platform}")
            windows = PureWindowsPath(value)
            if value.startswith("/") or windows.is_absolute() or windows.drive:
                raise SystemExit(f"absolute path in package command evidence: {platform}")
    if native.get("command") != commands[1]["argv"]:
        raise SystemExit(f"native compiler command evidence mismatch: {platform}")
    if installed.get("command") != commands[0]["argv"]:
        raise SystemExit(f"installed compiler command evidence mismatch: {platform}")

    packaged_compiler = f"<unpacked-prefix>/{compiler_record['path']}"
    expected_installed_command = [packaged_compiler, "--version"]
    expected_compile_command = [
        packaged_compiler,
        "-mstc32",
        "--model-small",
        "--code-loc",
        "0xFF0000",
        "<work-dir>/mt5d_smoke.c",
        "-o",
        "<work-dir>/mt5d_smoke.ihx",
    ]
    if commands[0]["argv"] != expected_installed_command:
        raise SystemExit(f"unexpected installed compiler command: {platform}")
    if (
        commands[1]["argv"][-1:] != ["--version"]
        or not commands[1]["argv"][0].startswith("<native-compiler>/")
    ):
        raise SystemExit(f"unexpected native compiler command: {platform}")
    if commands[2]["argv"] != expected_compile_command:
        raise SystemExit(f"unexpected clean-room compile command: {platform}")

    example = validation.get("example")
    example_sha256 = digest(MT5D_EXAMPLE)
    if (
        not isinstance(example, dict)
        or example.get("source") != "support/stc32/tests/package/mt5d_smoke.c"
        or example.get("source_sha256") != example_sha256
        or not re.fullmatch(r"[0-9a-f]{64}", str(example.get("output_sha256", "")))
        or example.get("output") != "<work-dir>/mt5d_smoke.ihx"
        or example.get("code_start") != 0xFF0000
    ):
        raise SystemExit(f"invalid package example evidence: {platform}")
    print(f"verified unpacked-package evidence: {platform}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("asset_dir", type=Path)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--platform", action="append", choices=sorted(PLATFORMS), required=True)
    parser.add_argument("--require-validation", action="store_true")
    args = parser.parse_args()
    if not TOKEN.fullmatch(args.tag):
        raise SystemExit(f"unsafe release tag: {args.tag!r}")
    if not re.fullmatch(r"[0-9a-fA-F]{40}", args.source_commit):
        raise SystemExit("source commit must be a full 40-character Git object ID")
    requested = args.platform
    if len(set(requested)) != len(requested):
        raise SystemExit("duplicate platform request")
    for platform in requested:
        verify_platform(args.asset_dir.resolve(), args.tag, args.source_commit, platform)
        if args.require_validation:
            verify_validation(
                args.asset_dir.resolve(), args.tag, args.source_commit, platform)
    expected_assets: set[str] = set()
    for platform in requested:
        stem = f"openstc32-toolchain-{args.tag}-{platform}"
        extension = PLATFORMS[platform][2]
        expected_assets.update(
            {
                f"{stem}{extension}",
                f"{stem}{extension}.sha256",
                f"{stem}.toolchain.json",
                f"{stem}.artifacts.json",
                f"{stem}.release.json",
            }
        )
        if args.require_validation:
            expected_assets.add(f"{stem}.validation.json")
    asset_dir = args.asset_dir.resolve()
    actual_assets = {path.name for path in asset_dir.iterdir() if path.is_file()}
    if actual_assets != expected_assets:
        raise SystemExit(
            "release asset boundary mismatch: "
            f"expected={len(expected_assets)} actual={len(actual_assets)} "
            f"unexpected={sorted(actual_assets - expected_assets)[:5]} "
            f"missing={sorted(expected_assets - actual_assets)[:5]}"
        )
    if any(not path.is_file() for path in asset_dir.iterdir()):
        raise SystemExit("release asset directory contains a non-file entry")


if __name__ == "__main__":
    main()
