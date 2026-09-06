#!/usr/bin/env bash
# Real native compile/strip regression, using exactly the build driver's maps.
set -euo pipefail
[[ "$(uname -s)" == MINGW* ]] || { echo "requires MSYS2 native shell" >&2; exit 1; }
SUPPORT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROOT="$(cd "$SUPPORT_ROOT/../.." && pwd)"
probe_dir="$(mktemp -d "${TMPDIR:-/tmp}/stc32-host-paths.XXXXXX")"
trap 'rm -rf -- "$probe_dir"' EXIT
BUILD_DIR="$probe_dir"
original_arg_exclusions="${MSYS2_ARG_CONV_EXCL:-}"
source "$SUPPORT_ROOT/scripts/host-path-maps.sh"
unset COMPILER_PATH
fixture="$SUPPORT_ROOT/tests/package/host_header_path.cpp"
logical_defines=('-DPREFIX="/opt/openstc32"' '-DNATIVE_SYSTEM_HEADER_DIR="/mingw/include"')
env MSYS2_ARG_CONV_EXCL="$original_arg_exclusions" \
  g++ -g -O2 "${logical_defines[@]}" "$fixture" -o "$probe_dir/unmapped.exe"
# The production Autoconf driver likewise expands its space-separated flags.
g++ -g -O2 $path_map_flags "${logical_defines[@]}" "$fixture" -o "$probe_dir/mapped.exe"
strip --strip-debug "$probe_dir/unmapped.exe" "$probe_dir/mapped.exe"
python "$SUPPORT_ROOT/tests/package/check_host_header_path.py" \
  "$probe_dir" "$msys_root_native"
echo "Windows host-header path regression passed (assertions retained)"
