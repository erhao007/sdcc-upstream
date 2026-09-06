#!/usr/bin/env bash
# Sourced by the build driver and its native regression. ROOT and BUILD_DIR
# must already be absolute. Keep assertions enabled; normalize their __FILE__.
path_map_flags="-ffile-prefix-map=$ROOT=. -ffile-prefix-map=$BUILD_DIR=.build"
host_path_roots=()
msys_root_native=""
if [[ "$(uname -s)" == MINGW* ]]; then
  : "${MINGW_PREFIX:?run inside an MSYS2 native toolchain shell}"
  root_native="$(cygpath -m "$ROOT")"
  build_native="$(cygpath -m "$BUILD_DIR")"
  host_native="$(cygpath -m "$MINGW_PREFIX")"
  msys_root_native="$(cygpath -m /)"
  msys_root_native="${msys_root_native%/}"
  path_map_flags+=" -ffile-prefix-map=$root_native=."
  path_map_flags+=" -ffile-prefix-map=$build_native=.build"
  # setup-msys2 installs Boost under RUNNER_TEMP on GitHub. Header assertions
  # survive strip, and neither the checkout nor build-root maps cover them.
  host_path_roots=("$MINGW_PREFIX" "$host_native")
  for host_path_root in "${host_path_roots[@]}"; do
    path_map_flags+=" -ffile-prefix-map=$host_path_root=.host"
  done
  # These are logical runtime paths, not host compiler input paths. MSYS2
  # otherwise turns /opt/openstc32 (and /mingw/include) into paths below its
  # own installation. Cover both sdbinutils and bundled GCC's driver/cppdefault.
  # Do NOT exclude all arguments: -I and source paths still need conversion.
  logical_path_defines=(
    BINDIR LIBDIR LOCALEDIR DEBUGDIR PREFIX
    STANDARD_STARTFILE_PREFIX STANDARD_EXEC_PREFIX STANDARD_LIBEXEC_PREFIX
    STANDARD_BINDIR_PREFIX TOOLDIR_BASE_PREFIX
    GCC_INCLUDE_DIR FIXED_INCLUDE_DIR GPLUSPLUS_INCLUDE_DIR
    GPLUSPLUS_TOOL_INCLUDE_DIR GPLUSPLUS_BACKWARD_INCLUDE_DIR
    GPLUSPLUS_LIBCXX_INCLUDE_DIR LOCAL_INCLUDE_DIR CROSS_INCLUDE_DIR
    TOOL_INCLUDE_DIR NATIVE_SYSTEM_HEADER_DIR
  )
  for logical_path_define in "${logical_path_defines[@]}"; do
    export MSYS2_ARG_CONV_EXCL="${MSYS2_ARG_CONV_EXCL:+$MSYS2_ARG_CONV_EXCL;}-D$logical_path_define="
  done
  # These options carry strings to match, not files to open. In equals form
  # they can be excluded without disabling conversion of actual Python inputs.
  export MSYS2_ARG_CONV_EXCL="$MSYS2_ARG_CONV_EXCL;--host-prefix=;--forbid-path="
fi
