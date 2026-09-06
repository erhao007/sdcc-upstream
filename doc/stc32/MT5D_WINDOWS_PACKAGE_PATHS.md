# MT-5D: Windows host-header paths in packages

## Failure and cause

At PR #6 head `68e19b7`, Windows run
[34025908703](https://github.com/erhao007/sdcc-upstream/actions/runs/34025908703)
completed the build and all five regression lanes, then rejected `bin/sdcc.exe`
with `absolute build path in package`. Its tested synthetic merge was
`978c4f05330a145c661a29577e7cf4429ea12f29`.

The previous fixes covered debug sections, configured installation prefixes,
generated lexer/parser paths and the checkout/build roots. They did not cover
host dependency headers. Boost assertions expand `__FILE__` into runtime string
data that survives `strip --strip-debug`. The Windows-native installation of
that head contains `C:/msys64/ucrt64/include/boost/...` strings. In the GitHub
log, the native toolchain instead resides at `D:/a/_temp/msys64/ucrt64`, below
the forbidden `RUNNER_TEMP`. Thus the old local checkout/build-root scan could
pass while the GitHub package scan failed. The failed job did not upload its
binary, so attribution is based on the log's toolchain location and the native
binary/reproduction, not on inspection of the missing GitHub artifact.

## Bounded repair

- Share the actual build prefix-map flags with a Windows-native regression.
  Map both MSYS and native `MINGW_PREFIX` spellings to `.host`; do not disable
  assertions, whitelist temporary paths or patch compiled binaries.
- Remove the newly recorded host-prefix flags from generated `configargs.h`
  before the existing host-tool rebuild. Keep the `COMPILER_PATH` isolation.
- Run the package hygiene rule over the installed files immediately after DLL
  staging, before the expensive regression lanes. Include the host dependency
  root even when it lives outside `RUNNER_TEMP` on a developer machine.
- Report the matched token, byte offset and bounded context on failure.
  Keep the independent post-archive validation and every regression gate.

## Reproduction and acceptance

From the public checkout in an MSYS2 UCRT64 shell:

```bash
bash support/stc32/scripts/check-windows-host-paths.sh
python -m unittest discover -s support/stc32/tests -p 'test_release_*.py'
```

The native regression compiles the project-owned `host_header_path.cpp` twice
against the installed, publicly licensed Boost dependency. It strips both PE
files, requires hygiene to reject the unmapped negative control, requires the
mapped binary to retain `.host/include/boost/smart_ptr/shared_ptr.hpp`, and
executes its successful path. It neither extracts nor redistributes Boost or
Keil implementation files.

Use a **fresh build directory and empty install prefix** for complete validation.
Changing CFLAGS in an existing Makefile does not necessarily rebuild old objects.
Require the installed-file scan, identity verification, packaged relocation
smoke and current-head CI on every platform. A passing native probe alone is E1,
not complete package E5, and neither result provides E4 or closes MT-5D.
