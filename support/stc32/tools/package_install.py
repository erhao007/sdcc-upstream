#!/usr/bin/env python3
"""Create a deterministic OpenSTC32 package and bound release metadata."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tarfile
import zipfile
from datetime import datetime, timezone
from pathlib import Path

# Importing the verifier must not create an untracked __pycache__ below the
# source tree immediately before its clean-tree check.
sys.dont_write_bytecode = True
from verify_install import verify


TOKEN = re.compile(r"^[A-Za-z0-9._-]+$")
PLATFORMS = {
    "linux-x86_64": ("Linux", "x86_64", "tar.gz"),
    "macos-arm64": ("Darwin", "arm64", "tar.gz"),
    "windows-x86_64": ("Windows", "AMD64", "zip"),
}


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_epoch(source_root: Path) -> int:
    return int(
        subprocess.check_output(
            ["git", "show", "-s", "--format=%ct", "HEAD"],
            cwd=source_root,
            text=True,
        ).strip()
    )


def installed_files(prefix: Path) -> list[Path]:
    files = []
    for path in prefix.rglob("*"):
        if path.is_symlink():
            raise SystemExit(f"refusing symlink in package input: {path}")
        if path.is_file():
            files.append(path)
    return sorted(files, key=lambda path: path.relative_to(prefix).as_posix())


def write_tar_gz(prefix: Path, output: Path, epoch: int) -> None:
    with output.open("wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=epoch) as compressed:
            with tarfile.open(fileobj=compressed, mode="w", format=tarfile.GNU_FORMAT) as archive:
                for path in installed_files(prefix):
                    relative = path.relative_to(prefix).as_posix()
                    # Installed SDCC host-tool aliases can be hard links.  The
                    # release boundary is a complete regular-file inventory,
                    # so expand every entry instead of emitting TAR links.
                    info = tarfile.TarInfo(relative)
                    info.size = path.stat().st_size
                    info.mode = path.stat().st_mode & 0o777
                    info.type = tarfile.REGTYPE
                    info.uid = 0
                    info.gid = 0
                    info.uname = ""
                    info.gname = ""
                    info.mtime = epoch
                    with path.open("rb") as source:
                        archive.addfile(info, source)


def write_zip(prefix: Path, output: Path, epoch: int) -> None:
    timestamp = datetime.fromtimestamp(max(epoch, 315532800), timezone.utc)
    date_time = (
        timestamp.year,
        timestamp.month,
        timestamp.day,
        timestamp.hour,
        timestamp.minute,
        timestamp.second,
    )
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in installed_files(prefix):
            relative = path.relative_to(prefix).as_posix()
            info = zipfile.ZipInfo(relative, date_time=date_time)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3
            info.external_attr = (path.stat().st_mode & 0xFFFF) << 16
            archive.writestr(info, path.read_bytes(), compresslevel=9)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("prefix", type=Path)
    parser.add_argument("output_dir", type=Path)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--platform", choices=sorted(PLATFORMS), required=True)
    parser.add_argument(
        "--source-root",
        type=Path,
        default=Path(__file__).resolve().parents[3],
    )
    args = parser.parse_args()
    if not TOKEN.fullmatch(args.tag):
        raise SystemExit(f"unsafe package tag: {args.tag!r}")

    prefix = args.prefix.resolve()
    source_root = args.source_root.resolve()
    output_dir = args.output_dir.resolve()
    host_os, host_arch, package_format = PLATFORMS[args.platform]
    verify(prefix, source_root, host_os, host_arch)

    output_dir.mkdir(parents=True, exist_ok=True)
    stem = f"openstc32-toolchain-{args.tag}-{args.platform}"
    extension = ".tar.gz" if package_format == "tar.gz" else ".zip"
    package = output_dir / f"{stem}{extension}"
    epoch = source_epoch(source_root)
    if package_format == "tar.gz":
        write_tar_gz(prefix, package, epoch)
    else:
        write_zip(prefix, package, epoch)

    toolchain_source = prefix / "share/openstc32/toolchain.json"
    artifacts_source = prefix / "share/openstc32/toolchain-artifacts.json"
    toolchain_sidecar = output_dir / f"{stem}.toolchain.json"
    artifacts_sidecar = output_dir / f"{stem}.artifacts.json"
    shutil.copyfile(toolchain_source, toolchain_sidecar)
    shutil.copyfile(artifacts_source, artifacts_sidecar)
    toolchain = json.loads(toolchain_source.read_text(encoding="utf-8"))

    release_metadata = {
        "schema": 1,
        "tag": args.tag,
        "platform": args.platform,
        "package": package.name,
        "package_sha256": digest(package),
        "source_commit": toolchain["source_commit"],
        "source_state_sha256": toolchain["source_state_sha256"],
        "host_os": toolchain["host_os"],
        "host_arch": toolchain["host_arch"],
        "toolchain_manifest": toolchain_sidecar.name,
        "toolchain_manifest_sha256": digest(toolchain_sidecar),
        "artifact_manifest": artifacts_sidecar.name,
        "artifact_manifest_sha256": digest(artifacts_sidecar),
        "source_date_epoch": epoch,
    }
    metadata_path = output_dir / f"{stem}.release.json"
    metadata_path.write_text(
        json.dumps(release_metadata, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    checksum_path = output_dir / f"{package.name}.sha256"
    checksum_path.write_text(
        f"{release_metadata['package_sha256']}  {package.name}\n", encoding="ascii"
    )
    print(f"packaged {package} ({release_metadata['package_sha256']})")


if __name__ == "__main__":
    main()
