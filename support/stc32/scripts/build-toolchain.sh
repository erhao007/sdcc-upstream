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

python3 "$SUPPORT_ROOT/tools/install_identity.py" \
  --source-root "$ROOT" \
  --prefix "$PREFIX"

echo "STC32 toolchain ready: $PREFIX"
echo "SDCC: $SDCC"
echo "Manifest: $PREFIX/share/openstc32/toolchain.json"
