#!/usr/bin/env bash
# build-windows.sh — reproducible Windows-native (MSYS2 UCRT64) driver.
#
# Prerequisites (one-time, from an MSYS2 UCRT64 shell):
#   pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-boost \
#     mingw-w64-ucrt-x86_64-python mingw-w64-ucrt-x86_64-python-yaml \
#     mingw-w64-ucrt-x86_64-python-pyserial \
#     bison flex git make patch diffutils python
#
# Known machine caveat (not fixable without an admin account): a legacy
# C:\Windows\System32\zlib1.dll shadows the UCRT64 one for any executable
# that imports zlib1.dll and runs outside a directory carrying a correct
# copy.  scripts/windows-build-fixups.sh stages correct copies beside the
# affected executables; the definitive fix is renaming/removing the rogue
# system DLL as an administrator.
#
# Usage (MSYS2 UCRT64 shell, inside the repository):
#   bash scripts/build-windows.sh [--gates]
#
# --gates additionally runs the standalone public toolchain suite: release
# boundary unit tests, opcode and ISA contracts, assembler/backend checks,
# ABI/runtime tests, and the five regression lanes (framework python is forced
# to the MSYS python because native ucrt64 python exceeds the Win32 32K
# argv+environment spawn limit used by the framework's collation rules).
set -euo pipefail

# This driver is stored under support/stc32 in the standalone toolchain
# repository; ROOT is the actual SDCC checkout, not support/stc32 itself.
SUPPORT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROOT="$(cd "$SUPPORT_ROOT/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
PREFIX="${PREFIX:-$BUILD_DIR/install}"
export BUILD_DIR PREFIX
JOBS="${JOBS:-$(nproc)}"

fail() { echo "build-windows: $*" >&2; exit 1; }

[[ "$(uname -s)" == MINGW* ]] || fail "must run inside an MSYS2 UCRT64 shell"

echo "== prerequisites =="
for tool in gcc g++ make bison flex git objdump; do
  command -v "$tool" >/dev/null 2>&1 || fail "missing tool: $tool"
done
python - <<'PY' || fail "missing python modules (yaml/serial)"
import yaml, serial
PY
[[ -x /usr/bin/python ]] || fail "MSYS python missing: pacman -S python"

echo "== build =="
export SDCC_HOME="$PREFIX"           # Windows sdcc argv[0] path discovery is inert
export PYTHONUTF8=1
export STC32_SOURCE_COMMIT="$(git -C "$ROOT" rev-parse HEAD)"
bash "$SUPPORT_ROOT/scripts/build-toolchain.sh"
bash "$SUPPORT_ROOT/scripts/windows-build-fixups.sh" gates-prep

[[ "${1:-}" == "--gates" ]] || { echo "build-windows: done (no gates)"; exit 0; }

echo "== gates =="
cd "$ROOT"
# The native compiler resolves sdcpp via PATH (cmd-style spawn); expose
# both the installed tree and the build tree bin directories.
export PATH="$PREFIX/bin:$BUILD_DIR/bin:$PATH"
export COMPILER_PATH="$BUILD_DIR/support/cpp/gcc"
# Gate runners resolve installed binaries through STC32_TOOLCHAIN_ROOT, as
# run-posix-gates.sh does; the in-tree build/install default does not exist
# for out-of-tree builds.
export STC32_TOOLCHAIN_ROOT="$PREFIX"
# The standalone public toolchain intentionally excludes the integration
# repository's Factory/DFU/BLE product tests.  Its authoritative toolchain
# gates are the explicit ISA, assembler/backend, ABI, runtime and regression
# commands below.
python -m unittest discover -s support/stc32/tests -p 'test_release_*.py'
python support/stc32/tools/opcode_check.py
# Keep the ISA decode report out of the source tree; verify_install.py
# requires a clean checkout (run-posix-gates.sh redirects it the same way).
python support/stc32/tools/ucsim_isa_probe.py \
  --strict --out "$BUILD_DIR/isa-decode-report.md"
python support/stc32/tools/ucsim_unknown_mode_probe.py
python support/stc32/tools/run_isa_semantics.py
make -C "$BUILD_DIR/sdas/as251" check
# The mcs251 backend check discovers installed data below BUILD_DIR/install
# even when CI installs to a separate prefix (see run-posix-gates.sh); bridge
# it with a directory junction, which needs no privileges on Windows.
build_install="$BUILD_DIR/install"
if [[ "$PREFIX" != "$build_install" && ! -e "$build_install/share" ]]; then
  mkdir -p "$build_install"
  cmd //c mklink //J "$(cygpath -w "$build_install/share")" \
    "$(cygpath -w "$PREFIX/share")" >/dev/null
fi
# The mcs251 backend check includes the ralloc2 directed gate; run it to
# completion and propagate its status without hiding the remaining gates.
mcs251_check_rc=0
make -C "$BUILD_DIR/src/mcs251" check || mcs251_check_rc=$?
echo "mcs251-backend-check-rc=$mcs251_check_rc"
python support/stc32/tools/run_abi_tests.py
python support/stc32/tools/run_runtime_tests.py
echo "== source migration gates =="
python support/stc32/tests/migration/run_migration_tests.py

echo "== regression lanes =="
cd "$BUILD_DIR/support/regression"
regression_rc=0
# COMPILER_PATH above is for SDCC's private cc1 lookup.  Leaving it set for
# the regression framework's host-GCC build makes GCC pick SDCC's `as`
# wrapper as the native assembler on Windows.  The installed/build bin paths
# already expose sdcpp.exe to SDCC, so the host-tool phase must be clean.
unset COMPILER_PATH
# Never let make's lack of a compiler dependency reuse an older lane result.
# A caller may provide a stable evidence label; otherwise every invocation
# gets fresh output directories shared by the five lanes in this run.
regression_run_id="${STC32_REGRESSION_RUN_ID:-$(date -u +%Y%m%dT%H%M%SZ)-$$}"
[[ "$regression_run_id" =~ ^[A-Za-z0-9._-]+$ ]] \
  || fail "invalid STC32_REGRESSION_RUN_ID: $regression_run_id"
regression_tmp="gen-$regression_run_id"
regression_results="results-$regression_run_id"
echo "regression evidence: $regression_tmp / $regression_results"
for lane in mcs251 mcs251-large mcs251-stack-auto mcs51-small mcs51-large; do
  make -j"$JOBS" PYTHON=/usr/bin/python SIM_TIMEOUT=15 \
    TMP_DIR="$regression_tmp" RESULTS_DIR="$regression_results" \
    "test-$lane" \
    || regression_rc=$?
  summary="$regression_results/$lane.sum"
  if [[ ! -f "$summary" ]]; then
    echo "build-windows: missing regression summary: $summary" >&2
    regression_rc=1
  elif ! grep -Eq "^Summary for '$lane': 0 failures," "$summary"; then
    cat "$summary" >&2
    regression_rc=1
  fi
done
cd "$ROOT"
if [[ "$mcs251_check_rc" -ne 0 || "$regression_rc" -ne 0 ]]; then
  exit 1
fi

echo "Windows build and gates passed"
