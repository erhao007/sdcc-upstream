#!/usr/bin/env python3
"""Generate the canonical OpenSTC32 toolchain installation identity."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import subprocess
from datetime import datetime, timezone
from pathlib import Path


TOOLCHAIN_SCHEMA = "openstc32.toolchain.v2"
ARTIFACT_SCHEMA = "openstc32.toolchain-artifacts.v2"
RELEASE_SCHEMA = "openstc32.toolchain-release.v2"
SOURCE_REPOSITORY = "erhao007/sdcc-upstream"
ARTIFACT_MANIFEST = Path("share/openstc32/toolchain-artifacts.json")
TOOLCHAIN_MANIFEST = Path("share/openstc32/toolchain.json")
METADATA_PATHS = {ARTIFACT_MANIFEST, TOOLCHAIN_MANIFEST}
RUNTIME_MODELS = {
    "mcs251-small": {"memory_model": "small", "stack_auto": False},
    "mcs251-large": {"memory_model": "large", "stack_auto": False},
    "mcs251-small-stack-auto": {"memory_model": "small", "stack_auto": True},
    "mcs251-large-stack-auto": {"memory_model": "large", "stack_auto": True},
}
TOOL_COMMANDS = {
    "compiler": ("sdcc", ("--version",), (0,), "SDCC :"),
    "assembler": ("sdas251", ("-h",), (0,), "Assembler V"),
    # ASlink prints its version/help and exits with ER_ERROR when no link input
    # is supplied.  Empty stdin prevents an interactive prompt.
    "linker": ("sdld", ("-v",), (0, 3), "Linker V"),
    "simulator": ("ucsim_51", ("-V",), (0,), "uCsim "),
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_state(source_root: Path) -> tuple[str, str, bool]:
    source_root = Path(source_root).resolve()
    try:
        head = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=source_root,
            stderr=subprocess.DEVNULL).strip()
        diff = subprocess.check_output(
            ["git", "diff", "--binary", "HEAD"], cwd=source_root)
        untracked = subprocess.check_output(
            ["git", "ls-files", "--others", "--exclude-standard", "-z"],
            cwd=source_root)
    except (OSError, subprocess.CalledProcessError) as exc:
        raise SystemExit(f"cannot inspect source state at {source_root}") from exc

    names = sorted(name for name in untracked.split(b"\0") if name)
    payload = bytearray(b"HEAD\0")
    payload.extend(head)
    payload.extend(b"\0DIFF\0")
    payload.extend(diff)
    for name in names:
        path = source_root / os.fsdecode(name)
        if path.is_file():
            content = path.read_bytes()
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
        head.decode("ascii"),
        hashlib.sha256(payload).hexdigest(),
        bool(diff or names),
    )


def host_binary(prefix: Path, name: str) -> Path:
    path = prefix / "bin" / name
    if path.is_file():
        return path
    executable = Path(str(path) + ".exe")
    if executable.is_file():
        return executable
    raise SystemExit(f"missing installed host tool: {path}")


def file_record(path: Path, prefix: Path) -> dict:
    path = Path(path)
    data = path.read_bytes()
    return {
        "path": path.relative_to(prefix).as_posix(),
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
    }


def records_sha256(records: list[dict]) -> str:
    digest = hashlib.sha256()
    for record in sorted(records, key=lambda item: item["path"]):
        digest.update(record["path"].encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(record["size"]).encode("ascii"))
        digest.update(b"\0")
        digest.update(record["sha256"].encode("ascii"))
        digest.update(b"\n")
    return digest.hexdigest()


def tool_record(prefix: Path, name: str, arguments: tuple[str, ...],
                allowed_returncodes: tuple[int, ...], marker: str) -> dict:
    path = host_binary(prefix, name)
    try:
        result = subprocess.run(
            [str(path), *arguments], input="", capture_output=True, text=True,
            errors="replace", timeout=30, check=False)
    except (OSError, subprocess.SubprocessError) as exc:
        raise SystemExit(f"cannot query installed host tool: {path}: {exc}") from exc
    output = result.stdout + "\n" + result.stderr
    version = next(
        (line.strip() for line in output.splitlines() if marker in line), None)
    if result.returncode not in allowed_returncodes or not version:
        raise SystemExit(
            f"cannot determine version for {path}: rc={result.returncode}")
    record = file_record(path, prefix)
    record["version"] = version
    return record


def build_manifests(source_root: Path, prefix: Path) -> tuple[Path, Path, int]:
    source_root = Path(source_root).resolve()
    prefix = Path(prefix).resolve()
    commit, state_sha256, dirty = source_state(source_root)
    requested_commit = os.environ.get("STC32_SOURCE_COMMIT")
    if requested_commit and requested_commit != commit:
        raise SystemExit(
            "STC32_SOURCE_COMMIT does not match source HEAD: "
            f"requested={requested_commit} HEAD={commit}")

    source_checkout = {
        "repository": SOURCE_REPOSITORY,
        "commit": commit,
        "dirty": dirty,
        "state_sha256": state_sha256,
    }
    source = {
        "root": dict(source_checkout),
        # In the final public toolchain repository the repository root is the
        # upstream checkout.  Keep both named identities so consumers never
        # infer the pre-split root/upstream relationship.
        "upstream": dict(source_checkout),
    }

    tools = {
        role: tool_record(prefix, *spec)
        for role, spec in TOOL_COMMANDS.items()
    }
    runtimes = {}
    for model, attributes in RUNTIME_MODELS.items():
        path = prefix / "share" / "sdcc" / "lib" / model / "libsdcc.lib"
        if not path.is_file():
            raise SystemExit(f"missing installed runtime: {path}")
        runtimes[model] = {**attributes, **file_record(path, prefix)}

    header_root = prefix / "share" / "sdcc" / "include" / "mcs251"
    header_paths = sorted(path for path in header_root.rglob("*") if path.is_file())
    if not header_paths:
        raise SystemExit(f"no installed MCS-251 headers: {header_root}")
    header_records = [file_record(path, prefix) for path in header_paths]
    headers = {
        "root": header_root.relative_to(prefix).as_posix(),
        "files": header_records,
        "files_sha256": records_sha256(header_records),
    }
    compatibility = {
        "abi": {"major": 1, "minor": 0},
        "memory_models": [
            {"model": values["memory_model"],
             "stack_auto": values["stack_auto"]}
            for values in RUNTIME_MODELS.values()
        ],
    }

    artifact_files = []
    for candidate in sorted(
            prefix.rglob("*"), key=lambda path: path.relative_to(prefix).as_posix()):
        relative = candidate.relative_to(prefix)
        if candidate.is_symlink():
            raise SystemExit(f"refusing symlink in install tree: {relative}")
        if candidate.is_file() and relative not in METADATA_PATHS:
            artifact_files.append(file_record(candidate, prefix))

    built_at = datetime.now(timezone.utc).isoformat()
    common = {
        "target": "stc32",
        "architecture": "mcs251",
        "chip": "STC32G12K128",
        "host_os": platform.system(),
        "host_arch": platform.machine(),
        "sdcc_version": tools["compiler"]["version"],
        "source": source,
        # Compatibility aliases retained for v1 consumers; v2 validators bind
        # them to source.upstream and reject divergence.
        "source_commit": commit,
        "source_dirty": dirty,
        "source_state_sha256": state_sha256,
        "compatibility": compatibility,
        "built_at_utc": built_at,
    }
    artifact_manifest = {
        "schema": ARTIFACT_SCHEMA,
        **common,
        "files": artifact_files,
        "files_sha256": records_sha256(artifact_files),
    }
    artifact_path = prefix / ARTIFACT_MANIFEST
    artifact_path.parent.mkdir(parents=True, exist_ok=True)
    artifact_path.write_text(
        json.dumps(artifact_manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8")

    manifest = {
        "schema": TOOLCHAIN_SCHEMA,
        **common,
        "tools": tools,
        "runtimes": runtimes,
        "headers": headers,
        "configure": [
            "--disable-doc",
            "--disable-pic14-port",
            "--disable-pic16-port",
            "--enable-mcs251-port",
            "--with-isl=no",
        ],
        "artifact_manifest": ARTIFACT_MANIFEST.as_posix(),
        "artifact_manifest_sha256": sha256(artifact_path),
    }
    manifest_path = prefix / TOOLCHAIN_MANIFEST
    manifest_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8")
    return manifest_path, artifact_path, len(artifact_files)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--prefix", type=Path, required=True)
    args = parser.parse_args()
    manifest, artifacts, count = build_manifests(args.source_root, args.prefix)
    print(f"toolchain manifest: {manifest}")
    print(f"artifact manifest: {artifacts} ({count} files)")


if __name__ == "__main__":
    main()
