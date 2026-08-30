#!/usr/bin/env bash
set -euo pipefail

# Run the standalone toolchain's POSIX validation gates against one explicit
# build/install pair.  Release and branch CI share this entry point so their
# correctness gates cannot silently drift apart.
SUPPORT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROOT="$(cd "$SUPPORT_ROOT/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
PREFIX="${PREFIX:-$BUILD_DIR/install}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '2')}"
SIM_TIMEOUT="${SIM_TIMEOUT:-15}"

fail() {
  echo "run-posix-gates: $*" >&2
  exit 1
}

case "$(uname -s)" in
  Linux|Darwin|FreeBSD) ;;
  *) fail "unsupported host; use build-windows.sh --gates on MSYS2" ;;
esac

run_regressions=0
case "${1:-}" in
  "") ;;
  --regression) run_regressions=1 ;;
  *) fail "usage: $0 [--regression]" ;;
esac

[[ "$BUILD_DIR" = /* ]] || BUILD_DIR="$ROOT/$BUILD_DIR"
[[ "$PREFIX" = /* ]] || PREFIX="$ROOT/$PREFIX"
[[ -x "$PREFIX/bin/sdcc" ]] || fail "installed compiler not found: $PREFIX/bin/sdcc"
[[ -d "$BUILD_DIR/src/mcs251" ]] || fail "configured build tree not found: $BUILD_DIR"

export STC32_TOOLCHAIN_ROOT="$PREFIX"

echo "== STC32 core gates =="
cd "$ROOT"
python3 support/stc32/tools/check_legal_review.py
python3 support/stc32/tools/opcode_check.py
PYTHONPATH=tools/pylib python3 support/stc32/tools/run_isa_semantics.py
python3 support/stc32/tools/run_runtime_tests.py
python3 support/stc32/tools/run_abi_tests.py
PYTHONPATH=tools/pylib python3 support/stc32/tools/ucsim_isa_probe.py \
  --strict --out "$BUILD_DIR/isa-decode-report.md"
python3 support/stc32/tools/ucsim_unknown_mode_probe.py

# Some backend checks discover installed data below BUILD_DIR/install even when
# CI installs to a separate prefix.  A fresh CI build has no entry here; if a
# caller already has one, accept it only when it resolves to this exact prefix.
build_install="$BUILD_DIR/install"
if [[ "$PREFIX" != "$build_install" ]]; then
  mkdir -p "$build_install"
  if [[ -e "$build_install/share" || -L "$build_install/share" ]]; then
    existing_share="$(cd "$build_install/share" 2>/dev/null && pwd -P)" \
      || fail "cannot resolve existing $build_install/share"
    expected_share="$(cd "$PREFIX/share" && pwd -P)"
    [[ "$existing_share" == "$expected_share" ]] \
      || fail "$build_install/share does not resolve to $PREFIX/share"
  else
    ln -s "$PREFIX/share" "$build_install/share"
  fi
fi
make -C "$BUILD_DIR/src/mcs251" check

if (( ! run_regressions )); then
  echo "POSIX core gates passed"
  exit 0
fi

echo "== fresh product regression lanes =="
regression_root="$BUILD_DIR/support/regression"
[[ -d "$regression_root" ]] || fail "regression build tree not found: $regression_root"

# Do not let host GCC resolve SDCC's private assembler wrapper through a
# compiler build-time COMPILER_PATH.
unset COMPILER_PATH

regression_run_id="${STC32_REGRESSION_RUN_ID:-$(date -u +%Y%m%dT%H%M%SZ)-$$}"
[[ "$regression_run_id" =~ ^[A-Za-z0-9._-]+$ ]] \
  || fail "invalid STC32_REGRESSION_RUN_ID: $regression_run_id"
regression_tmp="gen-$regression_run_id"
regression_results="results-$regression_run_id"
[[ ! -e "$regression_root/$regression_tmp" ]] \
  || fail "regression temp directory is not fresh: $regression_tmp"
[[ ! -e "$regression_root/$regression_results" ]] \
  || fail "regression result directory is not fresh: $regression_results"

echo "regression evidence: $regression_tmp / $regression_results"
regression_rc=0
for lane in mcs251 mcs251-large mcs251-stack-auto mcs51-small mcs51-large; do
  make -C "$regression_root" -j"$JOBS" \
    SIM_TIMEOUT="$SIM_TIMEOUT" \
    TMP_DIR="$regression_tmp" RESULTS_DIR="$regression_results" \
    "test-$lane" \
    || regression_rc=1
  summary="$regression_root/$regression_results/$lane.sum"
  if [[ ! -f "$summary" ]]; then
    echo "run-posix-gates: missing regression summary: $summary" >&2
    regression_rc=1
    continue
  fi
  grep -E "^Summary for '$lane':" "$summary" || true
  if ! grep -Eq "^Summary for '$lane': 0 failures," "$summary"; then
    echo "run-posix-gates: failing regression summary: $summary" >&2
    regression_rc=1
  fi
done

(( regression_rc == 0 )) || fail "one or more product regression lanes failed"
echo "POSIX build, core gates, and product regression lanes passed"
