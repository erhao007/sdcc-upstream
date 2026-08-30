# OpenSTC32 legal review checklist

This document is an engineering hand-off for a qualified open-source lawyer.
It records open questions and required evidence; it is not a legal opinion and
does not approve a release.

The machine-readable scope is
[`LEGAL_REVIEW_SCOPE.json`](LEGAL_REVIEW_SCOPE.json). Validate it with:

```sh
python3 support/stc32/tools/check_legal_review.py
```

Until the scoped review approves binary distribution, the release workflow
also invokes the validator with `--require-binary-release-approval` and stops
before installing build dependencies or publishing a new release.

## Engineering inventory completed

- [x] Preserve `COPYING`, `LICENSE`, `doc/README.txt`, and per-file notices as
  the authoritative inherited license sources.
- [x] Record the SDCC import baseline and a reproducible command for listing
  every changed path.
- [x] Record that the four project-authored MCS-251 runtime assembly files
  contain a GPL-2.0-or-later linking exception.
- [x] Record that `stc32g12k128.h` states GPL-2.0-or-later but does not contain
  that linking exception.
- [x] Preserve source provenance and third-party review status in
  `SOURCE_PROVENANCE.json` and `THIRD_PARTY.yml`.
- [x] Keep SDK, EDU, bootloader, bridge firmware, private history, and raw
  hardware logs outside this public repository.

## File-level license and rights review

- [ ] Export `git diff --name-status 6962481..HEAD` for the release candidate
  and classify every added or modified file.
- [ ] Map every new file to an exact copyright holder and SPDX license
  expression; retain all inherited holders and notices.
- [ ] Reconcile Git author identities with names in file headers and archive
  employer, contractor, or contributor permissions where applicable.
- [ ] Review files without an explicit notice, including newly authored
  backend, test, tooling, workflow, and documentation material.
- [ ] Run an approved license scanner and manually resolve every unknown,
  conflict, generated file, and false positive.
- [ ] Decide whether a scoped REUSE/SPDX configuration is appropriate without
  rewriting or misclassifying inherited SDCC licensing.

## Device header and firmware boundary

- [ ] Confirm the lawful source and redistribution basis for every register,
  bit, address, and explanatory comment in `stc32g12k128.h`.
- [ ] Obtain a compatible linking exception or dual-license grant from all
  relevant rights holders, or replace the header with a legally approved
  device pack.
- [ ] Record the resulting rule for GPL firmware, non-GPL SDK examples, and
  closed commercial firmware separately.
- [ ] Confirm whether generated firmware incorporates protectable header or
  runtime material and what notices/source obligations apply.

## ISA, manuals, and provenance

- [ ] Verify the exact public Intel and STC documents, editions, URLs, and
  archived copies used by the ISA and register data.
- [ ] Separate factual instruction/register data from project-authored
  selection, arrangement, tests, and explanatory text.
- [ ] Confirm that no Keil C251 proprietary header, library, startup,
  disassembly, or reverse-engineered implementation entered the repository.
- [ ] Resolve the pending review entries in `THIRD_PARTY.yml`.

## Binary release compliance

- [ ] Include the approved license texts, copyright notices, and a generated
  component/file BOM in every Linux, macOS, and Windows package.
- [ ] Bind each binary package to its immutable source tag, complete install
  manifest, checksum, and corresponding-source location.
- [ ] Verify the source offered for a binary is sufficient to rebuild that
  exact package, including build and packaging scripts.
- [ ] Review macOS signing/notarization and Windows Authenticode terms without
  treating signatures as license compliance.
- [ ] Review project names and disclaimers for SDCC/STC trademark and
  non-endorsement concerns.

## Contributions and final decision

- [ ] Select and publish a DCO, CLA, or equivalent contribution-rights policy.
- [ ] Record reviewer name/organization, qualification, scope, reviewed commit,
  date, exceptions, and permitted distribution forms.
- [ ] Mark every item in `LEGAL_REVIEW_SCOPE.json` as resolved or document a
  release-blocking exception.
- [ ] Issue an explicit decision for public source maintenance, public binary
  releases, noncommercial SDK source, and closed commercial firmware; one
  decision must not be silently applied to the others.

## Counsel sign-off

```text
Reviewer:
Organization / qualification:
Repository and commit:
Scope:
Decision:
Open exceptions:
Date:
Signature / archived approval reference:
```
