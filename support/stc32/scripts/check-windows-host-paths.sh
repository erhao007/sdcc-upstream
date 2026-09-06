#!/usr/bin/env bash
# Real native compile/strip regression, using exactly the build driver's maps.
set -euo pipefail
[[ "$(uname -s)" == MINGW* ]] || { echo "requires MSYS2 native shell" >&2; exit 1; }
SUPPORT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROOT="$(cd "$SUPPORT_ROOT/../.." && pwd)"
probe_dir="$(mktemp -d "${TMPDIR:-/tmp}/stc32-host-paths.XXXXXX")"
trap 'rm -rf -- "$probe_dir"' EXIT
BUILD_DIR="$probe_dir"
source "$SUPPORT_ROOT/scripts/host-path-maps.sh"
unset COMPILER_PATH
fixture="$SUPPORT_ROOT/tests/package/host_header_path.cpp"
g++ -g -O2 "$fixture" -o "$probe_dir/unmapped.exe"
# The production Autoconf driver likewise expands its space-separated flags.
g++ -g -O2 $path_map_flags "$fixture" -o "$probe_dir/mapped.exe"
strip --strip-debug "$probe_dir/unmapped.exe" "$probe_dir/mapped.exe"
python "$SUPPORT_ROOT/tests/package/check_host_header_path.py" \
  "$probe_dir" "$host_native"
"$probe_dir/mapped.exe"
echo "Windows host-header path regression passed (assertions retained)"
