#!/usr/bin/env bash
set -euo pipefail

# This driver lives below support/stc32 in the standalone public toolchain
# repository.  Keep the repository root separate from the support snapshot so
# the script never depends on the old integration checkout layout.
SUPPORT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROOT="$(cd "$SUPPORT_ROOT/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
PREFIX="${PREFIX:-$BUILD_DIR/install}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '2')}"

[[ "$BUILD_DIR" = /* ]] || BUILD_DIR="$ROOT/$BUILD_DIR"
[[ "$PREFIX" = /* ]] || PREFIX="$ROOT/$PREFIX"
mkdir -p "$BUILD_DIR"

if [[ "$(uname -s)" == MINGW* ]]; then
  # Windows-native host: stage DLLs that a rogue legacy
  # C:\Windows\System32\zlib1.dll would otherwise shadow (see
  # scripts/windows-build-fixups.sh).  No-op on POSIX hosts.
  bash "$SUPPORT_ROOT/scripts/windows-build-fixups.sh" pre
fi

configured=0
if [[ "${FORCE_CONFIGURE:-0}" == 1 ]]; then
  configured=1
elif [[ ! -f "$BUILD_DIR/.stc32-configured" ]]; then
  configured=1
elif [[ "$(<"$BUILD_DIR/.stc32-configured")" != "$PREFIX" ]]; then
  configured=1
fi

if ((configured)); then
  configure_cppflags="${CPPFLAGS-}"
  configure_ldflags="${LDFLAGS-}"
  # Apple Silicon Homebrew keeps Boost outside the system include path.  Keep
  # explicit caller flags authoritative, but make a clean checkout work with
  # the standard Homebrew prefixes when no flags were supplied.
  if [[ -z "${CPPFLAGS+x}" && "$(uname -s)" == Darwin ]]; then
    brew_prefixes=()
    for candidate in /opt/homebrew /usr/local; do
      [[ -d "$candidate" ]] && brew_prefixes+=("$candidate")
    done
    if command -v brew >/dev/null 2>&1; then
      detected_prefix="$(brew --prefix 2>/dev/null || true)"
      [[ -n "$detected_prefix" && -d "$detected_prefix" ]] && brew_prefixes+=("$detected_prefix")
    fi
    for brew_prefix in "${brew_prefixes[@]}"; do
      if [[ -f "$brew_prefix/include/boost/graph/adjacency_list.hpp" ]]; then
        if [[ -z "${CPPFLAGS+x}" ]]; then
          configure_cppflags="-I$brew_prefix/include"
        fi
        if [[ -z "${LDFLAGS+x}" ]]; then
          configure_ldflags="-L$brew_prefix/lib"
        fi
        echo "Using Homebrew dependencies from $brew_prefix"
        break
      fi
    done
  fi
  (
    cd "$BUILD_DIR"
    env CPPFLAGS="$configure_cppflags" LDFLAGS="$configure_ldflags" \
      "$ROOT/configure" \
      --disable-doc \
      --disable-pic14-port --disable-pic16-port \
      --enable-mcs251-port \
      --with-isl=no \
      --prefix="$PREFIX"
  )
  printf '%s\n' "$PREFIX" > "$BUILD_DIR/.stc32-configured"
fi

if [[ "$(uname -s)" == MINGW* ]]; then
  # The in-tree bin/sdcc + bin/sdcpp POSIX sh shims cannot be spawned by
  # the native compiler during the device-library stage, so the first
  # top-level make may fail there.  Let it, replace the shims once the
  # host tools exist, then retry strictly; any real error still fails
  # the strict retry below.
  make -C "$BUILD_DIR" -j"$JOBS" || true
  bash "$SUPPORT_ROOT/scripts/windows-build-fixups.sh" post-host-tools
  # The native compiler resolves sdcpp via PATH (cmd-style spawn), and
  # sdcpp resolves cc1 through COMPILER_PATH.
  export PATH="$BUILD_DIR/bin:$PATH"
  export COMPILER_PATH="$BUILD_DIR/support/cpp/gcc"
fi
make -C "$BUILD_DIR" -j"$JOBS"

# Device-library objects do not depend on the compiler executable, so an
# ordinary incremental make can silently reuse libraries produced by an older
# backend.  The engineering contract requires all four MCS-251 variants to be
# rebuilt with the current compiler.  Refresh the model-specific generated
# Makefile explicitly because legacy configure.in dependencies can prevent an
# incremental top-level make from noticing Makefile.in changes.  Then -B
# propagates through the recursive model-mcs251 makes and forces every library
# object to be regenerated.
(
  cd "$BUILD_DIR"
  ./config.status device/lib/mcs251/Makefile
)
make -B -C "$BUILD_DIR/device/lib" -j"$JOBS" model-mcs251
make -C "$BUILD_DIR" install
make -C "$BUILD_DIR/device/lib" -j"$JOBS" install

if [[ "$(uname -s)" == MINGW* ]]; then
  bash "$SUPPORT_ROOT/scripts/windows-build-fixups.sh" post-install
fi

SDCC="$PREFIX/bin/sdcc"
HEADER="$PREFIX/share/sdcc/include/mcs251/stc32g12k128.h"
LIB="$PREFIX/share/sdcc/lib/mcs251-small/libsdcc.lib"
test -x "$SDCC"
test -f "$HEADER"
test -f "$LIB"

python3 - "$ROOT" "$PREFIX" <<'PY'
import json
import hashlib
import os
import platform
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

root = Path(sys.argv[1])
prefix = Path(sys.argv[2])
sdcc = prefix / "bin" / "sdcc"


def source_state(path):
    head = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=path).strip()
    diff = subprocess.check_output(
        ["git", "diff", "--binary", "HEAD"], cwd=path)
    untracked = subprocess.check_output(
        ["git", "ls-files", "--others", "--exclude-standard", "-z"],
        cwd=path)
    names = sorted(name for name in untracked.split(b"\0") if name)
    payload = bytearray(b"HEAD\0")
    payload.extend(head)
    payload.extend(b"\0DIFF\0")
    payload.extend(diff)
    for name in names:
        file_path = path / os.fsdecode(name)
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
    return hashlib.sha256(payload).hexdigest(), bool(diff or names)


source_state_sha256, source_dirty = source_state(root)
commit = subprocess.check_output(
    ["git", "-C", str(root), "rev-parse", "HEAD"],
    text=True, stderr=subprocess.DEVNULL).strip()
requested_commit = os.environ.get("STC32_SOURCE_COMMIT")
if requested_commit and requested_commit != commit:
    raise SystemExit(
        "STC32_SOURCE_COMMIT does not match source HEAD: "
        f"requested={requested_commit} HEAD={commit}"
    )
version = subprocess.check_output(
    [str(sdcc), "--version"], text=True, stderr=subprocess.STDOUT
).splitlines()[0]
built_at = datetime.now(timezone.utc).isoformat()
metadata_paths = {
    Path("share/openstc32/toolchain.json"),
    Path("share/openstc32/toolchain-artifacts.json"),
}

# The artifact manifest is the complete installed-file boundary.  Reject
# symlinks before asking Path whether an entry is a regular file: a broken
# symlink returns is_file() == False and would otherwise disappear from the
# inventory while remaining present in the install tree.
artifact_files = []
for candidate in sorted(prefix.rglob("*"),
                        key=lambda path: path.relative_to(prefix).as_posix()):
    relative = candidate.relative_to(prefix)
    if candidate.is_symlink():
        raise SystemExit(f"refusing symlink in install tree: {relative}")
    if not candidate.is_file():
        continue
    if relative in metadata_paths:
        continue
    content = candidate.read_bytes()
    artifact_files.append({
        "path": relative.as_posix(),
        "size": len(content),
        "sha256": hashlib.sha256(content).hexdigest(),
    })

artifact_manifest = {
    "schema": 1,
    "target": "stc32",
    "architecture": "mcs251",
    "chip": "STC32G12K128",
    "host_os": platform.system(),
    "host_arch": platform.machine(),
    "sdcc_version": version,
    "source_commit": commit,
    "source_dirty": source_dirty,
    "source_state_sha256": source_state_sha256,
    "built_at_utc": built_at,
    "files": artifact_files,
}
artifact_out = prefix / "share" / "openstc32" / "toolchain-artifacts.json"
artifact_out.parent.mkdir(parents=True, exist_ok=True)
artifact_out.write_text(json.dumps(artifact_manifest, indent=2) + "\n")
artifact_sha256 = hashlib.sha256(artifact_out.read_bytes()).hexdigest()

manifest = {
    "schema": 1,
    "target": "stc32",
    "architecture": "mcs251",
    "chip": "STC32G12K128",
    "host_os": platform.system(),
    "host_arch": platform.machine(),
    "sdcc_version": version,
    "source_commit": commit,
    "source_dirty": source_dirty,
    "source_state_sha256": source_state_sha256,
    "configure": [
        "--disable-doc",
        "--disable-pic14-port",
        "--disable-pic16-port",
        "--enable-mcs251-port",
        "--with-isl=no",
    ],
    "artifact_manifest": "share/openstc32/toolchain-artifacts.json",
    "artifact_manifest_sha256": artifact_sha256,
    "built_at_utc": built_at,
}
out = prefix / "share" / "openstc32" / "toolchain.json"
out.write_text(json.dumps(manifest, indent=2) + "\n")
print(f"toolchain manifest: {out}")
print(f"artifact manifest: {artifact_out} ({len(artifact_files)} files)")
PY

echo "STC32 toolchain ready: $PREFIX"
echo "SDCC: $SDCC"
echo "Manifest: $PREFIX/share/openstc32/toolchain.json"
