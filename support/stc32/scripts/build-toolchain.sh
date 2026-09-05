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
# Compile against a stable logical installation prefix, then override `prefix`
# only while staging the package tree.  Using the temporary staging directory
# as configure's prefix embeds CI/developer paths in host tools such as sdar.
CONFIGURE_PREFIX="/opt/openstc32"
CONFIGURE_POLICY="relocatable-v2-disable-nls-zstd"

[[ "$BUILD_DIR" = /* ]] || BUILD_DIR="$ROOT/$BUILD_DIR"
[[ "$PREFIX" = /* ]] || PREFIX="$ROOT/$PREFIX"
mkdir -p "$BUILD_DIR"
INSTALL_STAGE="$BUILD_DIR/.openstc32-install-stage"

# Host compiler diagnostics and assertion strings can otherwise retain the
# checkout/build directory through __FILE__.  Release packages must not expose
# CI or developer paths, so normalize both roots at compile time while keeping
# caller-supplied optimization/warning flags intact.
path_map_flags="-ffile-prefix-map=$ROOT=. -ffile-prefix-map=$BUILD_DIR=.build"
if [[ "$(uname -s)" == MINGW* ]]; then
  root_native="$(cygpath -m "$ROOT")"
  build_native="$(cygpath -m "$BUILD_DIR")"
  path_map_flags+=" -ffile-prefix-map=$root_native=."
  path_map_flags+=" -ffile-prefix-map=$build_native=.build"
fi
# An explicitly supplied CFLAGS/CXXFLAGS value tells Autoconf that the caller
# supplied the complete host flags and suppresses its normal GCC defaults
# (-g -O2). Keep those defaults when the caller did not provide an override;
# otherwise the SDCC host binary is built unoptimized and the target-library
# rebuild becomes several times slower on CI.
if [[ "${CFLAGS+x}" == x ]]; then
  host_cflags="$CFLAGS"
else
  host_cflags="-g -O2"
fi
if [[ "${CXXFLAGS+x}" == x ]]; then
  host_cxxflags="$CXXFLAGS"
else
  host_cxxflags="-g -O2"
fi
configure_cflags="${host_cflags:+$host_cflags }$path_map_flags"
configure_cxxflags="${host_cxxflags:+$host_cxxflags }$path_map_flags"
configure_key="$(printf '%s\n%s\n%s\n%s\n%s\n' \
  "$PREFIX" "$CONFIGURE_PREFIX" "$CONFIGURE_POLICY" \
  "$configure_cflags" "$configure_cxxflags")"

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
elif [[ "$(<"$BUILD_DIR/.stc32-configured")" != "$configure_key" ]]; then
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
      CFLAGS="$configure_cflags" CXXFLAGS="$configure_cxxflags" \
      "$ROOT/configure" \
      --disable-doc \
      --disable-pic14-port --disable-pic16-port \
      --enable-mcs251-port \
      --disable-nls \
      --without-zstd \
      --with-isl=no \
      --prefix="$CONFIGURE_PREFIX"
  )
  printf '%s\n' "$configure_key" > "$BUILD_DIR/.stc32-configured"
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

# GCC's bundled preprocessor records its configure command in configargs.h;
# compiler prefix-map flags do not rewrite that arbitrary string literal.
# Normalize the generated header, then let its dependency edge rebuild sdcpp.
cpp_configargs="$BUILD_DIR/support/cpp/gcc/configargs.h"
if [[ -f "$cpp_configargs" ]]; then
  sanitize_args=("$ROOT" "." "$BUILD_DIR" ".build")
  if [[ "$(uname -s)" == MINGW* ]]; then
    sanitize_args+=("$root_native" "." "$build_native" ".build")
  fi
  python3 - "$cpp_configargs" "${sanitize_args[@]}" <<'PY'
from pathlib import Path
import sys

header = Path(sys.argv[1])
content = header.read_text(encoding="utf-8")
sanitized = content
values = sys.argv[2:]
for old, new in zip(values[0::2], values[1::2]):
    sanitized = sanitized.replace(old, new)
    sanitized = sanitized.replace(old.replace("/", "\\\\"), new)
if sanitized != content:
    header.write_text(sanitized, encoding="utf-8")
PY
  # COMPILER_PATH is required by the already-built native sdcpp.exe so it can
  # find cc1 on Windows.  It must not leak into the host-GCC rebuild below:
  # GCC would otherwise select support/cpp/gcc/as (its in-tree wrapper), whose
  # original assembler is unset in this standalone build, and try to execute
  # the first -I option as a command.
  restore_compiler_path=0
  if [[ "$(uname -s)" == MINGW* && -n "${COMPILER_PATH+x}" ]]; then
    saved_compiler_path="$COMPILER_PATH"
    unset COMPILER_PATH
    restore_compiler_path=1
  fi
  make -C "$BUILD_DIR/support/cpp" -j"$JOBS"
  if ((restore_compiler_path)); then
    export COMPILER_PATH="$saved_compiler_path"
  fi
fi

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
library_jobs="$JOBS"
if [[ "$(uname -s)" == MINGW* ]]; then
  # Recursive GNU make can exhaust its MSYS jobserver while -B remakes the
  # generated dependency files for all four models.  A serial forced rebuild
  # is slower but deterministic, and still proves every library was refreshed.
  library_jobs=1
fi
make -B -C "$BUILD_DIR/device/lib" -j"$library_jobs" model-mcs251
# Bypass a caller-provided rm function/alias.  Some developer shells define
# rm as a wrapper that mishandles option-first invocations; the release driver
# must always execute the platform utility directly.
command rm -rf "$INSTALL_STAGE"
make -C "$BUILD_DIR" install DESTDIR="$INSTALL_STAGE"
make -C "$BUILD_DIR/device/lib" -j"$JOBS" install DESTDIR="$INSTALL_STAGE"

staged_prefix="$INSTALL_STAGE$CONFIGURE_PREFIX"
test -d "$staged_prefix"
# Libtool archives are build-time metadata rather than runtime inputs.  Their
# dependency_libs fields can retain the native build directory even when the
# host binaries use the stable configure prefix, so exclude them from the
# relocatable installation before its complete-file manifest is generated.
find "$staged_prefix" -type f -name '*.la' -delete
if [[ -e "$PREFIX" ]]; then
  if [[ -n "$(find "$PREFIX" -mindepth 1 -print -quit 2>/dev/null)" ]]; then
    echo "refusing to replace non-empty install prefix: $PREFIX" >&2
    exit 1
  fi
  rmdir "$PREFIX"
fi
mkdir -p "$(dirname "$PREFIX")"
mv "$staged_prefix" "$PREFIX"
command rm -rf "$INSTALL_STAGE"

if [[ "$(uname -s)" == MINGW* ]]; then
  bash "$SUPPORT_ROOT/scripts/windows-build-fixups.sh" post-install
fi

SDCC="$PREFIX/bin/sdcc"
HEADER="$PREFIX/share/sdcc/include/mcs251/stc32g12k128.h"
LIB="$PREFIX/share/sdcc/lib/mcs251-small/libsdcc.lib"
test -x "$SDCC"
test -f "$HEADER"
test -f "$LIB"

python3 "$SUPPORT_ROOT/tools/install_identity.py" \
  --source-root "$ROOT" \
  --prefix "$PREFIX"

echo "STC32 toolchain ready: $PREFIX"
echo "SDCC: $SDCC"
echo "Manifest: $PREFIX/share/openstc32/toolchain.json"
