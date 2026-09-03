#!/usr/bin/env python3
"""Verify a complete OpenSTC32 install tree and its source binding."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath, PureWindowsPath

from install_identity import (
    ARTIFACT_MANIFEST,
    ARTIFACT_SCHEMA,
    METADATA_PATHS,
    RUNTIME_MODELS,
    SOURCE_REPOSITORY,
    TOOLCHAIN_SCHEMA,
    TOOL_COMMANDS,
    records_sha256,
    source_state,
)

METADATA = {PurePosixPath(path.as_posix()) for path in METADATA_PATHS}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_json(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise SystemExit(f"cannot read JSON {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise SystemExit(f"expected JSON object: {path}")
    return value


def safe_relative(value: object) -> PurePosixPath:
    if not isinstance(value, str) or not value:
        raise SystemExit(f"invalid artifact path: {value!r}")
    relative = PurePosixPath(value)
    windows = PureWindowsPath(value)
    if (
        "\\" in value
        or relative.is_absolute()
        or windows.is_absolute()
        or windows.drive
        or ".." in relative.parts
        or relative in METADATA
    ):
        raise SystemExit(f"unsafe artifact path: {value}")
    return relative


def verify_file_record(prefix: Path, record: object, label: str) -> PurePosixPath:
    if not isinstance(record, dict):
        raise SystemExit(f"invalid {label} record")
    relative = safe_relative(record.get("path"))
    path = prefix.joinpath(*relative.parts)
    if path.is_symlink() or not path.is_file():
        raise SystemExit(f"missing or unsafe {label}: {relative}")
    data = path.read_bytes()
    if (
        record.get("size") != len(data)
        or record.get("sha256") != hashlib.sha256(data).hexdigest()
    ):
        raise SystemExit(f"{label} mismatch: {relative}")
    return relative


def verify(
    prefix: Path,
    source_root: Path,
    expected_host_os: str | None = None,
    expected_host_arch: str | None = None,
) -> int:
    if not prefix.is_dir():
        raise SystemExit(f"install prefix is not a directory: {prefix}")

    toolchain_path = prefix / "share/openstc32/toolchain.json"
    artifact_path = prefix / "share/openstc32/toolchain-artifacts.json"
    toolchain = load_json(toolchain_path)
    artifact = load_json(artifact_path)
    if toolchain.get("schema") != TOOLCHAIN_SCHEMA:
        raise SystemExit("unsupported toolchain manifest schema")
    if artifact.get("schema") != ARTIFACT_SCHEMA:
        raise SystemExit("unsupported artifact manifest schema")

    listed: set[PurePosixPath] = set()
    entries = artifact.get("files")
    if not isinstance(entries, list) or not entries:
        raise SystemExit("artifact manifest has no files")
    for entry in entries:
        if not isinstance(entry, dict):
            raise SystemExit("artifact manifest contains a non-object entry")
        relative = safe_relative(entry.get("path"))
        if relative in listed:
            raise SystemExit(f"duplicate artifact path: {relative}")
        path = prefix.joinpath(*relative.parts)
        if path.is_symlink() or not path.is_file():
            raise SystemExit(f"missing or unsafe artifact: {relative}")
        data = path.read_bytes()
        if (
            entry.get("size") != len(data)
            or entry.get("sha256") != hashlib.sha256(data).hexdigest()
        ):
            raise SystemExit(f"artifact mismatch: {relative}")
        listed.add(relative)
    if artifact.get("files_sha256") != records_sha256(entries):
        raise SystemExit("artifact file-set digest mismatch")

    actual: set[PurePosixPath] = set()
    for path in prefix.rglob("*"):
        relative = PurePosixPath(path.relative_to(prefix).as_posix())
        if path.is_symlink():
            raise SystemExit(f"unexpected symlink in install tree: {relative}")
        if path.is_file() and relative not in METADATA:
            actual.add(relative)
    if listed != actual:
        missing = sorted(str(path) for path in actual - listed)
        extra = sorted(str(path) for path in listed - actual)
        raise SystemExit(
            "artifact coverage mismatch: "
            f"listed={len(listed)} actual={len(actual)} "
            f"unlisted={missing[:5]} absent={extra[:5]}"
        )

    expected_identity = {
        "target": "stc32",
        "architecture": "mcs251",
        "chip": "STC32G12K128",
    }
    for key, expected in expected_identity.items():
        if toolchain.get(key) != expected or artifact.get(key) != expected:
            raise SystemExit(f"manifest identity mismatch for {key}")
    pair_fields = (
        "target", "architecture", "chip", "host_os", "host_arch",
        "sdcc_version", "source", "source_commit", "source_dirty",
        "source_state_sha256", "compatibility", "built_at_utc",
    )
    for key in pair_fields:
        if toolchain.get(key) != artifact.get(key):
            raise SystemExit(f"manifest pair mismatch for {key}")

    source = toolchain.get("source")
    if not isinstance(source, dict) or set(source) != {"root", "upstream"}:
        raise SystemExit("toolchain manifest has invalid source identity")
    source_identity_root = source.get("root")
    source_upstream = source.get("upstream")
    if not isinstance(source_identity_root, dict) or source_identity_root != source_upstream:
        raise SystemExit("source root/upstream identity mismatch")
    expected_source_keys = {"repository", "commit", "dirty", "state_sha256"}
    if set(source_identity_root) != expected_source_keys:
        raise SystemExit("source identity has missing or unknown fields")
    if source_identity_root.get("repository") != SOURCE_REPOSITORY:
        raise SystemExit("unexpected source repository")
    if (
        source_identity_root.get("commit") != toolchain.get("source_commit")
        or source_identity_root.get("dirty") != toolchain.get("source_dirty")
        or source_identity_root.get("state_sha256") != toolchain.get("source_state_sha256")
    ):
        raise SystemExit("source identity aliases diverge")

    expected_compatibility = {
        "abi": {"major": 1, "minor": 0},
        "memory_models": [
            {"model": values["memory_model"],
             "stack_auto": values["stack_auto"]}
            for values in RUNTIME_MODELS.values()
        ],
    }
    if toolchain.get("compatibility") != expected_compatibility:
        raise SystemExit("incompatible ABI or memory-model manifest")

    tools = toolchain.get("tools")
    if not isinstance(tools, dict) or set(tools) != set(TOOL_COMMANDS):
        raise SystemExit("toolchain manifest has incomplete host-tool identity")
    for role, (name, _arguments, _codes, _marker) in TOOL_COMMANDS.items():
        record = tools[role]
        relative = verify_file_record(prefix, record, f"{role} tool")
        if relative not in {
            PurePosixPath("bin") / name,
            PurePosixPath("bin") / f"{name}.exe",
        }:
            raise SystemExit(f"unexpected {role} tool path: {relative}")
        if not isinstance(record.get("version"), str) or not record["version"].strip():
            raise SystemExit(f"missing {role} tool version")

    runtimes = toolchain.get("runtimes")
    if not isinstance(runtimes, dict) or set(runtimes) != set(RUNTIME_MODELS):
        raise SystemExit("toolchain manifest has incomplete runtime identity")
    for model, attributes in RUNTIME_MODELS.items():
        record = runtimes[model]
        relative = verify_file_record(prefix, record, f"runtime {model}")
        expected_path = PurePosixPath(
            f"share/sdcc/lib/{model}/libsdcc.lib")
        if relative != expected_path or any(
                record.get(key) != value for key, value in attributes.items()):
            raise SystemExit(f"runtime compatibility mismatch: {model}")

    headers = toolchain.get("headers")
    if not isinstance(headers, dict) or headers.get("root") != "share/sdcc/include/mcs251":
        raise SystemExit("toolchain manifest has invalid header identity")
    header_entries = headers.get("files")
    if not isinstance(header_entries, list) or not header_entries:
        raise SystemExit("toolchain manifest has no headers")
    header_paths = {
        verify_file_record(prefix, record, "header") for record in header_entries
    }
    actual_headers = {
        PurePosixPath(path.relative_to(prefix).as_posix())
        for path in (prefix / headers["root"]).rglob("*") if path.is_file()
    }
    if header_paths != actual_headers:
        raise SystemExit("header file boundary mismatch")
    if headers.get("files_sha256") != records_sha256(header_entries):
        raise SystemExit("header file-set digest mismatch")

    head, state_sha256, source_dirty = source_state(source_root)
    if expected_host_os and toolchain["host_os"] != expected_host_os:
        raise SystemExit(
            f"unexpected host OS: {toolchain['host_os']} (expected {expected_host_os})"
        )
    if expected_host_arch and toolchain["host_arch"].lower() != expected_host_arch.lower():
        raise SystemExit(
            f"unexpected host architecture: {toolchain['host_arch']} "
            f"(expected {expected_host_arch})"
        )
    if toolchain["source_commit"] != head:
        raise SystemExit(
            f"toolchain source commit mismatch: manifest={toolchain['source_commit']} HEAD={head}"
        )
    if source_dirty:
        raise SystemExit("current source tree is dirty")
    if toolchain["source_state_sha256"] != state_sha256:
        raise SystemExit("toolchain source-state hash does not match current source tree")
    if toolchain.get("source_dirty") is not False or artifact.get("source_dirty") is not False:
        raise SystemExit("toolchain manifest records a dirty source tree")
    if toolchain.get("artifact_manifest") != ARTIFACT_MANIFEST.as_posix():
        raise SystemExit("unexpected artifact manifest path")
    if toolchain.get("artifact_manifest_sha256") != sha256(artifact_path):
        raise SystemExit("toolchain manifest does not bind artifact manifest")

    print(
        "verified install boundary: "
        f"files={len(actual)} host={toolchain['host_os']}/{toolchain['host_arch']} "
        f"source={head}"
    )
    return len(actual)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("prefix", type=Path)
    parser.add_argument(
        "--source-root",
        type=Path,
        default=Path(__file__).resolve().parents[3],
    )
    parser.add_argument("--expect-host-os")
    parser.add_argument("--expect-host-arch")
    args = parser.parse_args()
    verify(
        args.prefix.resolve(),
        args.source_root.resolve(),
        args.expect_host_os,
        args.expect_host_arch,
    )


if __name__ == "__main__":
    main()
