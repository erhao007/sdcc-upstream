#!/usr/bin/env python3
"""Verify packaged OpenSTC32 assets before artifact upload or release."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import tarfile
import zipfile
from pathlib import Path, PurePosixPath


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


def safe_name(name: str) -> PurePosixPath:
    relative = PurePosixPath(name)
    if not name or relative.is_absolute() or ".." in relative.parts:
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
                if relative in members:
                    raise SystemExit(f"duplicate archive member: {relative}")
                members[relative] = archive.read(member)
    else:
        raise SystemExit(f"unsupported package format: {path}")
    return members


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
        "schema": 1,
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
    if toolchain.get("source_commit") != commit or artifacts.get("source_commit") != commit:
        raise SystemExit(f"source binding mismatch: {platform}")
    if toolchain.get("source_dirty") is not False or artifacts.get("source_dirty") is not False:
        raise SystemExit(f"dirty source recorded in release asset: {platform}")
    pair_fields = (
        "schema",
        "target",
        "architecture",
        "chip",
        "host_os",
        "host_arch",
        "sdcc_version",
        "source_commit",
        "source_dirty",
        "source_state_sha256",
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
    print(f"verified release assets: {platform} files={len(members)}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("asset_dir", type=Path)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--platform", action="append", choices=sorted(PLATFORMS), required=True)
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
