# OpenSTC32 toolchain

English | [简体中文](README.zh-CN.md)

[![STC32 toolchain CI](https://github.com/erhao007/sdcc-upstream/actions/workflows/ci.yml/badge.svg?branch=stc32)](https://github.com/erhao007/sdcc-upstream/actions/workflows/ci.yml)
[![STC32 platform builds](https://github.com/erhao007/sdcc-upstream/actions/workflows/platforms.yml/badge.svg?branch=stc32)](https://github.com/erhao007/sdcc-upstream/actions/workflows/platforms.yml)
[![STC32 toolchain release](https://github.com/erhao007/sdcc-upstream/actions/workflows/release.yml/badge.svg)](https://github.com/erhao007/sdcc-upstream/actions/workflows/release.yml)

OpenSTC32 is an SDCC-derived, upstream-oriented toolchain port for the Intel
MCS-251 architecture and the STC32G12K128 microcontroller. It provides a native
C compiler, assembler, linker, simulator, device headers, and runtime libraries
without requiring Keil C251.

This is an independent development repository, not an official SDCC or STC
release. The initial implementation supports **Source Mode only**. The
user-facing `-mstc32` option is an alias for the single internal `mcs251` port;
it is not a separate backend or ABI.

## Current release status

| Gate | Current public-release evidence |
| --- | --- |
| Linux x86_64 build and tests | Passed on GitHub Actions, Ubuntu 24.04 |
| Linux x86_64 release package | Historical prerelease with SHA-256 and complete install manifests |
| macOS release package | Not yet published for the current public tag |
| Windows x86_64 release package | Not yet published for the current public tag |
| STC32G12K128 real-board validation | Historical project evidence exists, but has not been rerun and bound to the current public release |

The current historical Linux package is the
[r7 prerelease](https://github.com/erhao007/sdcc-upstream/releases/tag/stc32-mt3-support-20260830-r7).
The release notes and asset names state the exact host platform covered by each
package. Do not treat simulator or host-build results as real-board evidence.

Every pull request and default-branch update builds and runs the full product
gates on GitHub-hosted macOS 15 arm64 and Windows Server 2025 x86_64 runners.
Those jobs also create and validate a package in temporary runner storage, but
do not upload it. A tagged release remains separately blocked before binary
generation unless the scoped qualified legal review permits distribution. When
that gate is opened, Linux, macOS, and Windows must all pass before a single
complete release is published.

## Download the historical Linux x86_64 prerelease

With the GitHub CLI installed:

```sh
tag=stc32-mt3-support-20260830-r7
gh release download "$tag" \
  --repo erhao007/sdcc-upstream \
  --pattern '*-linux-x86_64.tar.gz' \
  --pattern '*-linux-x86_64.tar.gz.sha256'
sha256sum --check ./*.sha256
mkdir openstc32-toolchain
tar -xzf ./*-linux-x86_64.tar.gz -C openstc32-toolchain
openstc32-toolchain/bin/sdcc -mstc32 --version
```

The installed tree includes:

- `share/openstc32/toolchain.json`, which binds the build to its source commit;
- `share/openstc32/toolchain-artifacts.json`, which records every installed
  file, size, and SHA-256;
- four MCS-251 runtime variants, plus the STC32G12K128 device header.

## Build from source

On Ubuntu 24.04, install the same dependencies used by CI:

```sh
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  bison build-essential flex libboost-dev libboost-graph-dev \
  libreadline-dev libzstd-dev python3 zlib1g-dev
```

Then build and install from a clean checkout:

```sh
git clone --branch stc32 https://github.com/erhao007/sdcc-upstream.git
cd sdcc-upstream
env -u COMPILER_PATH -u SDCC_HOME \
  bash support/stc32/scripts/build-toolchain.sh
build/install/bin/sdcc -mstc32 --version
```

`BUILD_DIR`, `PREFIX`, and `JOBS` can be set to override the default
`build/`, `build/install/`, and detected CPU count. On MSYS2 UCRT64, the
versioned Windows-native entry point is:

```sh
bash support/stc32/scripts/build-windows.sh --gates
```

The Windows command is a source-build path, not a claim that a current Windows
release package has passed publication acceptance.

## Compile firmware

The following is the minimal compile-and-link shape. Real firmware must also
provide the startup, memory layout, device initialization, and application code
required by its board:

```sh
mkdir -p build/firmware
build/install/bin/sdcc -mstc32 -c src/main.c \
  -o build/firmware/main.rel
build/install/bin/sdcc -mstc32 build/firmware/main.rel \
  -o build/firmware/firmware.ihx
```

The ABI and memory-model contract is documented in
[`doc/stc32/ABI.md`](doc/stc32/ABI.md). Architecture and port-registration
details are in [`doc/stc32/ARCHITECTURE.md`](doc/stc32/ARCHITECTURE.md).

## Run the toolchain gates

After a successful build:

```sh
export STC32_TOOLCHAIN_ROOT="$PWD/build/install"
python3 support/stc32/tools/opcode_check.py
PYTHONPATH=tools/pylib python3 support/stc32/tools/run_isa_semantics.py
python3 support/stc32/tools/run_runtime_tests.py
python3 support/stc32/tools/run_abi_tests.py
PYTHONPATH=tools/pylib python3 support/stc32/tools/ucsim_isa_probe.py --strict
python3 support/stc32/tools/ucsim_unknown_mode_probe.py
make -C build/src/mcs251 check
```

Every new opcode must have an automated test. Compiler code-generation changes
must include simulator regression coverage and must preserve the existing SDCC
ports.

## Repository map

- `src/mcs251/` — SDCC MCS-251 C backend;
- `sdas/as251/` and `sdld/` — assembler and linker support;
- `sim/ucsim/src/sims/mcs251.src/` — MCS-251 simulator;
- `device/include/mcs251/` and `device/lib/mcs251/` — device header and runtime;
- `support/stc32/isa/` — canonical Source Mode ISA data;
- `support/stc32/tests/` and `support/stc32/tools/` — automated gates;
- `doc/stc32/` — ABI, architecture, provenance, dependency, and sync records.

## Scope and contribution rules

- Use only public Intel MCS-251 documentation, public STC32G documentation,
  and SDCC source.
- Do not copy, decompile, or reverse engineer proprietary Keil C251 material.
- Keep patches small and suitable for eventual upstream review.
- Correctness comes before optimization; keep `-mstc32` normalized to the
  single `mcs251` port.
- Include the exact host, command, compiler version, reduced test case, and
  error output in bug reports.

Please use [GitHub Issues](https://github.com/erhao007/sdcc-upstream/issues) for
reproducible defects and proposed changes. Upstream synchronization notes are
kept in [`doc/stc32/UPSTREAM_SYNC.md`](doc/stc32/UPSTREAM_SYNC.md).

## Licensing and provenance

SDCC is a collection of components under multiple free-software licenses. The
repository-level [`COPYING`](COPYING), [`LICENSE`](LICENSE), original
[`doc/README.txt`](doc/README.txt), and per-file notices remain authoritative.
STC32-specific source and dependency provenance is recorded in
[`doc/stc32/SOURCE_PROVENANCE.json`](doc/stc32/SOURCE_PROVENANCE.json) and
[`doc/stc32/THIRD_PARTY.yml`](doc/stc32/THIRD_PARTY.yml). The engineering input
for qualified legal review is tracked in
[`doc/stc32/LEGAL_REVIEW_SCOPE.json`](doc/stc32/LEGAL_REVIEW_SCOPE.json) and
[`doc/stc32/LEGAL_REVIEW_CHECKLIST.md`](doc/stc32/LEGAL_REVIEW_CHECKLIST.md);
that review is still pending.

Do not assume that one repository-level license label replaces component or
per-file terms. Binary redistributors should preserve the corresponding license
and copyright notices and provide the complete corresponding source as required
by the applicable licenses. This summary is not legal advice.
