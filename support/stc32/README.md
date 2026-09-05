# STC32 support snapshot

This directory contains the STC32G / MCS-251 Source Mode ISA data, ABI and
runtime/assembler/simulator gates that accompany the public `stc32` branch of
this SDCC-derived repository.  The compiler, assembler, linker, simulator,
runtime and device support remain in the normal SDCC tree; this directory is
not a second compiler implementation and does not contain SDK or EDU code.

## Build

From the repository root, a clean POSIX build can be driven with:

```sh
bash support/stc32/scripts/build-toolchain.sh
```

The driver configures `--enable-mcs251-port`, installs into `build/install`,
rebuilds all MCS-251 library models, and writes
`build/install/share/openstc32/toolchain.json` plus the complete
`toolchain-artifacts.json` file boundary.  The MT-5C v2 identity records the
source root/upstream state, compiler/assembler/linker/simulator versions and
SHA-256, all four MCS-251 runtimes, installed headers, ABI 1.0 and the four
memory-model combinations.  Set `BUILD_DIR`, `PREFIX`, `JOBS`, or
`FORCE_CONFIGURE=1` to override local paths.  Windows-native builds use
`bash support/stc32/scripts/build-windows.sh --gates` from an MSYS2 UCRT64
shell.

## Gates

The support tests are intentionally kept under this directory so they can be
run from a standalone clone without the former integration repository:

```sh
bash support/stc32/scripts/run-posix-gates.sh --regression
```

That command runs the STC32-specific checks plus fresh `mcs251`,
`mcs251-large`, `mcs251-stack-auto`, `mcs51-small`, and `mcs51-large`
regression lanes, rejecting reused lane directories and non-zero summaries.
To run individual gates while diagnosing a failure:

```sh
python support/stc32/tools/opcode_check.py
python support/stc32/tools/ucsim_isa_probe.py --strict
python support/stc32/tools/ucsim_unknown_mode_probe.py
python support/stc32/tools/run_isa_semantics.py
python support/stc32/tools/run_abi_tests.py
python support/stc32/tools/run_runtime_tests.py
```

The tests use the installed toolchain by default.  `STC32_TOOLCHAIN_ROOT`,
`STC32_SDCC`, `STC32_SDAS251`, `STC32_SDLD`, and `STC32_UCSIM` can point them at
an explicitly selected installation.  Generated reports and temporary build
outputs are not part of the source snapshot.

## Package validation

`tools/verify_install.py` checks the complete installed-file boundary and binds
it to the current clean Git source state.  `tools/package_install.py` creates a
deterministic platform package plus checksum and manifest sidecars, and
`tools/verify_release_assets.py` reopens the archive and verifies every file
before any workflow artifact or GitHub Release can be published.
`tools/validate_package_install.py` then extracts the package into a fresh
directory, rejects temporary files and caller-supplied source/build paths, runs
the packaged compiler without `SDCC_HOME` or `COMPILER_PATH`, and compiles the
clean-room `tests/package/mt5d_smoke.c` example at `0xFF0000`.  Its MT-5D JSON
sidecar records the host/native compiler, packaged SDCC version, normalized
commands and exit codes, example hash, package SHA-256 and source identity;
final asset validation requires that sidecar.  Branch CI performs this complete
package validation without uploading binaries; tagged publishing also requires
the separate qualified-legal-review gate.  Schema v1, missing tool/runtime/header
identities, dirty source identities, incompatible ABI or memory-model
declarations, failed unpacked commands, and any selected or complete-file hash
drift are rejected rather than interpreted as legacy-compatible input.
The build uses `/opt/openstc32` only as a stable logical configure prefix and
uses `DESTDIR` to stage each platform tree before moving it to the requested
empty `PREFIX`; transient runner paths are therefore neither the package layout
nor an embedded installation identity.  A non-empty `PREFIX` is rejected so a
clean package cannot silently inherit stale files from an earlier installation.
Windows packages carry only the discovered UCRT64 runtime-DLL dependency closure.
The exact MSYS2 package versions are recorded in
`share/openstc32/windows-runtime-components.tsv`, and the matching package
license texts are copied below `share/openstc32/licenses/msys2-ucrt64/` before
the complete-file identity is generated.  This traceability does not replace
the qualified legal review required by the tagged release workflow.

## Ownership and provenance

`doc/stc32/SOURCE_PROVENANCE.json` records the clean-snapshot source and the
MT-3 freeze point.  SDCC and third-party files retain their existing headers
and licenses; this support snapshot does not replace the repository's GPL or
mixed-license notices.  `doc/stc32/ABI.md` and the ISA YAML files are the
normative toolchain-side records consumed by the SDK lockfile.
