# stc_diyclock migration fixture provenance

This directory is a small, de-businessized migration fixture derived from the
application source shape of `zerog2k/stc_diyclock`. It is not a copy of that
repository and it does not contain its device headers, peripheral drivers,
submodules, build products, Keil files, or binary artifacts.

## Fixed upstream reference

- Repository: <https://github.com/zerog2k/stc_diyclock>
- Revision: `beb3b5139fe64a4b8bfc9a24539ed5c0cc8c5fe3` (`main` at 2026-09-01)
- Reviewed source path: `src/main.c`
- Declared license text: `LICENCE`, retained locally as `LICENSE.upstream`
- `src/main.c` SHA-256: `f0045989073f4b3a718acb8142e0bfda281cdfac2ac65d30d8c94e73b3a73ed8`
- `LICENCE` SHA-256: `efe24cfeb6bd94aa08ddacec960fdbacd5843eb985fe5c10405432e4b4ad6b4b`
- Copyright notice in the reviewed source: Jens Jensen, 2016
- The upstream repository is an archived, multi-file SDCC STC15 application.

The repository-level MIT notice is evidence for the upstream author's material,
not a conclusion that every file in the repository has the same provenance.
On 2026-09-01 the project user explicitly confirmed the fixed `src/main.c`
minimal-derivative and public-redistribution scope recorded in
`AUTHORIZATION.md`, including all listed exclusions. The MT-4A source-input
status is therefore `USER_CONFIRMED / MIT_SCOPED`. This is an engineering
record, not legal advice or a qualified legal audit.

## Local extraction boundary

`real_main_excerpt.c` is a minimal derivative retaining only the source-level
patterns needed by MT-4A: volatile `__bit` state, an interrupt declaration with
the upstream `__interrupt 1 __using 1` shape, and a small state update. The
state transition body and the test-visible names are intentionally re-written
for this fixture. The checked-in form normalizes the suffix to SDCC's current
canonical `__interrupt (1) __using (1)` spelling; the upstream no-parentheses
form is therefore a mechanical migration input, not a claim of native syntax
acceptance.

`clock_app.h` and `main.c` are independently authored test support. They do not
come from the upstream repository. The upstream copyright and MIT notice are
retained because the fixture is derived from the reviewed source shape; the
complete notice is in `LICENSE.upstream`.

The following material is explicitly excluded and was not imported or analyzed:

- `src/adc.c`, which carries an STC MCU International A/D demo attribution;
- `stc15.h` and all other vendor/device headers;
- RTC, LED, EEPROM, NMEA, and other peripheral implementation files;
- the `stcgal` submodule and all external dependencies;
- Keil project files, startup/linker material, object/listing files, and binaries.

## Test contract

`real-stc-diyclock-main` compiles the normalized derived source and checks the
generated assembly for `reti` in all three migration models. The
`stc-diyclock-derived-project` case separately compiles the two translation
units, links them, and checks the state transition with uCsim. These checks are
source/host evidence only; they do not prove STC32G12K128 hardware behavior or
the upstream project's original peripheral behavior.
