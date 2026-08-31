#!/usr/bin/env python3
"""Verify a complete OpenSTC32 install tree and its source binding."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
from pathlib import Path, PurePosixPath, PureWindowsPath


METADATA = {
    PurePosixPath("share/openstc32/toolchain.json"),
    PurePosixPath("share/openstc32/toolchain-artifacts.json"),
}


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


def source_state(source_root: Path) -> tuple[str, str, bool]:
    try:
        head = subprocess.check_output(
            ["git", "rev-parse", "HEAD"],
            cwd=source_root,
            stderr=subprocess.DEVNULL,
        ).strip()
        diff = subprocess.check_output(
            ["git", "diff", "--binary", "HEAD"], cwd=source_root
        )
        untracked = subprocess.check_output(
            ["git", "ls-files", "--others", "--exclude-standard", "-z"],
            cwd=source_root,
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        raise SystemExit(f"cannot inspect source state at {source_root}") from exc

    names = sorted(name for name in untracked.split(b"\0") if name)
    payload = bytearray(b"HEAD\0")
    payload.extend(head)
    payload.extend(b"\0DIFF\0")
    payload.extend(diff)
    for name in names:
        file_path = source_root / os.fsdecode(name)
        if file_path.is_file():
            content = file_path.read_bytes()
            marker = b"FILE"
        else:
            content = b""
            marker = b"NONFILE"
        payload.extend(b"\0UNTRACKED\0")
        payload.extend(marker)
        payload.extend(len(name).to_bytes(8, "big"))
        payload.extend(name)
        payload.extend(len(content).to_bytes(8, "big"))
        payload.extend(content)
    return (
        head.decode(),
        hashlib.sha256(payload).hexdigest(),
        bool(diff or names),
    )


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
        "schema": 1,
        "target": "stc32",
        "architecture": "mcs251",
        "chip": "STC32G12K128",
    }
    for key, expected in expected_identity.items():
        if toolchain.get(key) != expected or artifact.get(key) != expected:
            raise SystemExit(f"manifest identity mismatch for {key}")

    head, state_sha256, source_dirty = source_state(source_root)
    for key in ("source_commit", "source_state_sha256", "host_os", "host_arch", "sdcc_version"):
        if not toolchain.get(key) or toolchain.get(key) != artifact.get(key):
            raise SystemExit(f"manifest pair mismatch for {key}")
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
    if toolchain.get("artifact_manifest") != "share/openstc32/toolchain-artifacts.json":
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
