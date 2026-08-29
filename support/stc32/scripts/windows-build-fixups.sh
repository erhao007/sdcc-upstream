#!/usr/bin/env bash
# windows-build-fixups.sh — Windows-native (MSYS2 UCRT64) build-tree fixups.
#
# SDCC's build system assumes a POSIX host in a few places; this script
# adapts the MSYS2 build tree so `make`/device-lib/install and the gate
# runners work without further manual steps.  Every stage is idempotent.
#
# Stages:
#   pre              stage correct ucrt64 DLLs beside toolchain exes that
#                    import zlib1.dll/libzstd.dll (a rogue legacy
#                    C:\Windows\System32\zlib1.dll otherwise wins the DLL
#                    search and breaks as/ld/cc1plus/sdar with
#                    STATUS_ENTRYPOINT_NOT_FOUND).
#   post-host-tools  replace the bin/sdcc + bin/sdcpp POSIX sh scripts with
#                    the real executables so the native sdcc.exe can spawn
#                    sdcpp.exe during the device-library build.
#   post-install     stage the same DLLs into the install prefix.
#   gates-prep       create extensionless PE copies beside the .exe files
#                    that the test harness references by POSIX names
#                    (build/bin, build/src, build/sim .../s51.src).
set -euo pipefail

# The fixups are kept with the support snapshot, while build/install paths
# belong to the standalone SDCC repository two levels above it.
SUPPORT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROOT="$(cd "$SUPPORT_ROOT/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
PREFIX="${PREFIX:-$BUILD_DIR/install}"
STAGE="${1:?usage: windows-build-fixups.sh <pre|post-host-tools|post-install|gates-prep>}"

[[ "$(uname -s)" == MINGW* ]] || { echo "not a MINGW host; nothing to do"; exit 0; }

GOOD_DLL_DIR="${GOOD_DLL_DIR:-/ucrt64/bin}"

stage_dlls() {
    # $1: directory whose *.exe get correct sibling DLLs when they import them
    local dir="$1"
    [[ -d "$dir" ]] || return 0
    local exe deps
    for exe in "$dir"/*.exe; do
        [[ -e "$exe" ]] || continue
        deps=$(objdump -p "$exe" 2>/dev/null | grep "DLL Name" | grep -cE "zlib1|libzstd" || true)
        if [[ "$deps" -gt 0 && ! -f "$dir/zlib1.dll" ]]; then
            cp -f "$GOOD_DLL_DIR/zlib1.dll" "$GOOD_DLL_DIR/libzstd.dll" "$dir/"
            echo "fixups: staged zlib1.dll/libzstd.dll into $dir"
        fi
    done
}

extensionless_copies() {
    # $1: directory; create a no-suffix copy of every *.exe (native python,
    # so MSYS' automatic .exe resolution cannot fake the existence check)
    local dir="$1"
    [[ -d "$dir" ]] || return 0
    python - "$dir" <<'PY'
import filecmp
import pathlib
import shutil
import sys
d = pathlib.Path(sys.argv[1])
for p in d.glob("*.exe"):
    b = d / p.stem
    if not b.exists() or not filecmp.cmp(p, b, shallow=False):
        shutil.copyfile(p, b)
        print(f"fixups: refreshed extensionless copy {b}")
PY
}

case "$STAGE" in
pre)
    for d in /ucrt64/x86_64-w64-mingw32/bin \
             /ucrt64/lib/gcc/*/*/; do
        stage_dlls "$d"
    done
    ;;
post-host-tools)
    if [[ -f "$BUILD_DIR/src/sdcc.exe" ]]; then
        cp -f "$BUILD_DIR/src/sdcc.exe" "$BUILD_DIR/bin/sdcc.exe"
    fi
    if [[ -f "$BUILD_DIR/support/cpp/gcc/cpp.exe" ]]; then
        cp -f "$BUILD_DIR/support/cpp/gcc/cpp.exe" "$BUILD_DIR/bin/sdcpp.exe"
    fi
    rm -f "$BUILD_DIR/bin/sdcc" "$BUILD_DIR/bin/sdcpp"
    # Build-tree sdbinutils tools (sdar & co.) also import zlib1.dll and
    # run from their own directories during device-library archiving.
    find "$BUILD_DIR/support/sdbinutils" -name "*.exe" -printf "%h\n" \
        2>/dev/null | sort -u | while read -r dir; do
        stage_dlls "$dir"
    done
    ;;
post-install)
    stage_dlls "$PREFIX/bin"
    ;;
gates-prep)
    extensionless_copies "$BUILD_DIR/bin"
    extensionless_copies "$BUILD_DIR/src"
    extensionless_copies "$BUILD_DIR/sim/ucsim/src/sims/s51.src"
    ;;
*)
    echo "unknown stage: $STAGE" >&2; exit 2 ;;
esac
