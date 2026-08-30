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
`build/install/share/openstc32/toolchain.json`.  Set `BUILD_DIR`, `PREFIX`,
`JOBS`, or `FORCE_CONFIGURE=1` to override local paths.  Windows-native builds
use `bash support/stc32/scripts/build-windows.sh --gates` from an MSYS2 UCRT64
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

## Ownership and provenance

`doc/stc32/SOURCE_PROVENANCE.json` records the clean-snapshot source and the
MT-3 freeze point.  SDCC and third-party files retain their existing headers
and licenses; this support snapshot does not replace the repository's GPL or
mixed-license notices.  `doc/stc32/ABI.md` and the ISA YAML files are the
normative toolchain-side records consumed by the SDK lockfile.
