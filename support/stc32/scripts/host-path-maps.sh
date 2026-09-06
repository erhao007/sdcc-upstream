#!/usr/bin/env bash
# Sourced by the build driver and its native regression. ROOT and BUILD_DIR
# must already be absolute. Keep assertions enabled; normalize their __FILE__.
path_map_flags="-ffile-prefix-map=$ROOT=. -ffile-prefix-map=$BUILD_DIR=.build"
host_path_roots=()
if [[ "$(uname -s)" == MINGW* ]]; then
  : "${MINGW_PREFIX:?run inside an MSYS2 native toolchain shell}"
  root_native="$(cygpath -m "$ROOT")"
  build_native="$(cygpath -m "$BUILD_DIR")"
  host_native="$(cygpath -m "$MINGW_PREFIX")"
  path_map_flags+=" -ffile-prefix-map=$root_native=."
  path_map_flags+=" -ffile-prefix-map=$build_native=.build"
  # setup-msys2 installs Boost under RUNNER_TEMP on GitHub. Header assertions
  # survive strip, and neither the checkout nor build-root maps cover them.
  host_path_roots=("$MINGW_PREFIX" "$host_native")
  for host_path_root in "${host_path_roots[@]}"; do
    path_map_flags+=" -ffile-prefix-map=$host_path_root=.host"
  done
fi
