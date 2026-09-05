#!/usr/bin/env bash
# windows-build-fixups.sh — Windows-native (MSYS2 UCRT64) build-tree fixups.
#
# SDCC's build system assumes a POSIX host in a few places; this script
# adapts the MSYS2 build tree so `make`/device-lib/install and the gate
# runners work without further manual steps.  Every stage is idempotent.
#
# Stages:
#   pre              stage correct redistributable UCRT64 runtime DLLs beside
#                    toolchain exes.  A rogue legacy
#                    C:\Windows\System32\zlib1.dll otherwise wins the DLL
#                    search and breaks as/ld/cc1plus/sdar with
#                    STATUS_ENTRYPOINT_NOT_FOUND.
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
    # $1: directory whose PE files get their redistributable UCRT64 dependency
    # closure as sibling DLLs.  Inspect copied DLLs too, since libstdc++ and
    # libiconv have their own MinGW runtime dependencies.
    local dir="$1"
    [[ -d "$dir" ]] || return 0
    local binary dependency dependency_lower changed
    changed=1
    while ((changed)); do
        changed=0
        for binary in "$dir"/*.exe "$dir"/*.dll; do
            [[ -e "$binary" ]] || continue
            while IFS= read -r dependency; do
                dependency_lower="${dependency,,}"
                case "$dependency_lower" in
                zlib1.dll|libzstd.dll|libgcc_s_seh-1.dll|libstdc++-6.dll|libwinpthread-1.dll|libiconv-2.dll)
                    if [[ -f "$GOOD_DLL_DIR/$dependency_lower" &&
                          ! -f "$dir/$dependency_lower" ]]; then
                        cp -f "$GOOD_DLL_DIR/$dependency_lower" "$dir/$dependency_lower"
                        echo "fixups: staged $dependency_lower into $dir"
                        changed=1
                    fi
                    ;;
                esac
            done < <(objdump -p "$binary" 2>/dev/null |
                sed -n 's/^[[:space:]]*DLL Name: //p')
        done
    done
}

stage_runtime_component() {
    # $1: MSYS2 package, $2: license-directory name, remaining: DLL names.
    local package="$1" license_name="$2"
    shift 2
    local dll included=() installed_name installed_version dll_csv
    for dll in "$@"; do
        [[ -f "$PREFIX/bin/$dll" ]] && included+=("$dll")
    done
    ((${#included[@]})) || return 0

    read -r installed_name installed_version < <(pacman -Q "$package")
    [[ "$installed_name" == "$package" && -n "$installed_version" ]] || {
        echo "cannot resolve runtime package version: $package" >&2
        exit 1
    }
    license_source="/ucrt64/share/licenses/$license_name"
    [[ -d "$license_source" ]] || {
        echo "missing runtime license directory: $license_source" >&2
        exit 1
    }
    license_target="$PREFIX/share/openstc32/licenses/msys2-ucrt64/$license_name"
    mkdir -p "$license_target"
    cp -a "$license_source"/. "$license_target/"
    dll_csv=$(IFS=,; echo "${included[*]}")
    printf '%s\t%s\t%s\t%s\n' \
        "$package" "$installed_version" "$license_name" "$dll_csv" \
        >> "$PREFIX/share/openstc32/windows-runtime-components.tsv"
}

stage_runtime_licenses() {
    mkdir -p "$PREFIX/share/openstc32"
    printf 'package\tversion\tlicense_directory\tdlls\n' \
        > "$PREFIX/share/openstc32/windows-runtime-components.tsv"
    stage_runtime_component mingw-w64-ucrt-x86_64-gcc-libs gcc-libs \
        libgcc_s_seh-1.dll libstdc++-6.dll
    stage_runtime_component mingw-w64-ucrt-x86_64-libwinpthread winpthreads \
        libwinpthread-1.dll
    stage_runtime_component mingw-w64-ucrt-x86_64-libiconv libiconv \
        libiconv-2.dll
    stage_runtime_component mingw-w64-ucrt-x86_64-zlib zlib zlib1.dll
    stage_runtime_component mingw-w64-ucrt-x86_64-zstd zstd libzstd.dll
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

copy_exact() {
    # MSYS treats foo and foo.exe as the same executable in several file
    # operations.  Use the native Windows API when both names must coexist.
    local source_win target_win
    source_win="$(cygpath -w "$1")"
    target_win="$(cygpath -w "$2")"
    export source_win target_win
    # shellcheck disable=SC2016 # PowerShell, not POSIX-shell, variables.
    powershell.exe -NoProfile -NonInteractive -Command \
        '[System.IO.File]::WriteAllBytes($env:target_win, [System.IO.File]::ReadAllBytes($env:source_win))'
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
    # Recursive device-library Makefiles invoke ../../bin/sdcc and
    # ../../bin/sdcpp as literal POSIX paths.  Provide real PE copies without
    # suffixes by overwriting the generated shell shims; the native compiler
    # still keeps the sibling .exe names for cmd-style child-process lookup.
    # Do not use MSYS cp/rm for these two names: it may resolve the no-suffix
    # path to the sibling .exe instead of the generated shell shim.
    copy_exact "$BUILD_DIR/bin/sdcc.exe" "$BUILD_DIR/bin/sdcc"
    copy_exact "$BUILD_DIR/bin/sdcpp.exe" "$BUILD_DIR/bin/sdcpp"
    chmod +x "$BUILD_DIR/bin/sdcc" "$BUILD_DIR/bin/sdcpp"
    extensionless_copies "$BUILD_DIR/bin"
    # Build-tree sdbinutils tools (sdar & co.) also import zlib1.dll and
    # run from their own directories during device-library archiving.
    find "$BUILD_DIR/support/sdbinutils" -name "*.exe" -printf "%h\n" \
        2>/dev/null | sort -u | while read -r dir; do
        stage_dlls "$dir"
    done
    ;;
post-install)
    stage_dlls "$PREFIX/bin"
    stage_runtime_licenses
    ;;
gates-prep)
    extensionless_copies "$BUILD_DIR/bin"
    extensionless_copies "$BUILD_DIR/src"
    extensionless_copies "$BUILD_DIR/sim/ucsim/src/sims/s51.src"
    ;;
*)
    echo "unknown stage: $STAGE" >&2; exit 2 ;;
esac
