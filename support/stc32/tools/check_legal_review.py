#!/usr/bin/env python3
"""Validate the engineering input to the OpenSTC32 legal review.

This is a deterministic schema/path/known-boundary check.  It deliberately
does not make a legal decision or mark any open review item as approved.
"""

import argparse
import json
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
DEFAULT_SCOPE = ROOT / "doc" / "stc32" / "LEGAL_REVIEW_SCOPE.json"
RUNTIME_FILES = {
    "device/lib/mcs251/atomic_flag_clear.asm",
    "device/lib/mcs251/atomic_flag_test_and_set.asm",
    "device/lib/mcs251/crtxclear.asm",
    "device/lib/mcs251/crtxinit.asm",
}
DEVICE_HEADER = "device/include/mcs251/stc32g12k128.h"
EXCEPTION_TEXT = "As a special exception"
EXPECTED_REPOSITORY = "erhao007/sdcc-upstream"
EXPECTED_IMPORT_COMMIT = "6962481"
EXPECTED_DELTA_COMMAND = "git diff --name-status 6962481..HEAD"
EXPECTED_EVIDENCE_FILES = {
    "COPYING",
    "LICENSE",
    "doc/README.txt",
    "doc/stc32/SOURCE_PROVENANCE.json",
    "doc/stc32/THIRD_PARTY.yml",
    "doc/stc32/UPSTREAM_SYNC.md",
    "doc/stc32/LEGAL_REVIEW_CHECKLIST.md",
}
EXPECTED_COMPONENT_IDS = {
    "sdcc-inherited-mixed-license-baseline",
    "mcs251-compiler-backend",
    "mcs251-assembler-linker",
    "mcs251-simulator",
    "mcs251-runtime-with-linking-exception",
    "stc32g12k128-device-header",
    "stc32-isa-tests-tools-and-scripts",
    "stc32-documentation-and-release-metadata",
}
EXPECTED_DECISION_IDS = {
    "rights-chain",
    "device-header-firmware-boundary",
    "isa-and-manual-provenance",
    "binary-notices-and-source-offer",
    "names-and-trademarks",
    "contribution-policy",
}


def fail(message: str) -> None:
    raise ValueError(message)


def require_string(item: dict, key: str, context: str) -> str:
    value = item.get(key)
    if not isinstance(value, str) or not value.strip():
        fail(f"{context}: {key} must be a non-empty string")
    return value


def matched_files(pattern: str) -> list[Path]:
    candidate = Path(pattern)
    if candidate.is_absolute() or ".." in candidate.parts:
        fail(f"unsafe path glob: {pattern}")
    return sorted(path for path in ROOT.glob(pattern) if path.is_file())


def validate_scope(scope_path: Path, require_binary_release_approval: bool) -> tuple[int, int]:
    data = json.loads(scope_path.read_text(encoding="utf-8"))
    if data.get("schema") != "openstc32.legal-review-scope.v1":
        fail("unexpected legal-review schema")
    if data.get("status") != "pending-qualified-legal-review":
        fail("engineering inventory must remain pending qualified legal review")
    if data.get("not_legal_advice") is not True:
        fail("not_legal_advice must be true")
    if data.get("repository") != EXPECTED_REPOSITORY:
        fail(f"repository must remain {EXPECTED_REPOSITORY}")

    baseline = data.get("audit_baseline")
    if not isinstance(baseline, dict):
        fail("audit_baseline must be an object")
    import_commit = require_string(baseline, "sdcc_import_commit", "audit_baseline")
    if import_commit != EXPECTED_IMPORT_COMMIT:
        fail(f"audit baseline must remain {EXPECTED_IMPORT_COMMIT}")
    if baseline.get("delta_command") != EXPECTED_DELTA_COMMAND:
        fail("audit baseline delta command changed")
    subprocess.run(
        ["git", "cat-file", "-e", f"{import_commit}^{{commit}}"],
        cwd=ROOT,
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    if baseline.get("classification_status") != "pending-file-level-review":
        fail("audit baseline classification must remain pending")

    release_policy = data.get("engineering_release_policy")
    if not isinstance(release_policy, dict):
        fail("engineering_release_policy must be an object")
    if release_policy.get("public_toolchain_source_maintenance") != (
        "continue-with-existing-notices-and-open-review-status"
    ):
        fail("public source maintenance policy changed without qualified review")
    if release_policy.get("new_stable_binary_release") != (
        "requires-scoped-license-bom-and-notice-review"
    ):
        fail("stable binary release policy changed without qualified review")
    if release_policy.get("non_gpl_sdk_or_closed_firmware_distribution") != (
        "blocked-pending-device-header-and-linking-review"
    ):
        fail("non-GPL SDK and closed-firmware release policy must remain blocked")
    if require_binary_release_approval:
        fail("new binary releases are blocked pending qualified legal review")

    evidence_files = data.get("evidence_files")
    if not isinstance(evidence_files, list) or not evidence_files:
        fail("evidence_files must be a non-empty list")
    if len(evidence_files) != len(set(evidence_files)):
        fail("evidence_files contains duplicate entries")
    if set(evidence_files) != EXPECTED_EVIDENCE_FILES:
        fail("evidence_files must cover the complete engineering evidence set")
    for relative in evidence_files:
        if not isinstance(relative, str) or not (ROOT / relative).is_file():
            fail(f"missing evidence file: {relative!r}")

    components = data.get("components")
    if not isinstance(components, list) or not components:
        fail("components must be a non-empty list")
    ids: set[str] = set()
    component_files: dict[str, set[str]] = {}
    components_by_id: dict[str, dict] = {}
    for component in components:
        if not isinstance(component, dict):
            fail("component entries must be objects")
        component_id = require_string(component, "id", "component")
        if component_id in ids:
            fail(f"duplicate component id: {component_id}")
        ids.add(component_id)
        components_by_id[component_id] = component
        for field in (
            "declared_license",
            "linking_exception",
            "review_status",
            "required_action",
        ):
            require_string(component, field, component_id)
        if not component["review_status"].startswith("pending-"):
            fail(f"{component_id}: legal review status must remain pending")
        globs = component.get("path_globs")
        if not isinstance(globs, list) or not globs:
            fail(f"{component_id}: path_globs must be a non-empty list")
        files: set[str] = set()
        for pattern in globs:
            if not isinstance(pattern, str) or not pattern:
                fail(f"{component_id}: invalid path glob")
            matches = matched_files(pattern)
            if not matches:
                fail(f"{component_id}: path glob matched no files: {pattern}")
            files.update(path.relative_to(ROOT).as_posix() for path in matches)
        component_files[component_id] = files

    if ids != EXPECTED_COMPONENT_IDS:
        fail("component inventory is incomplete or contains an unknown component")

    runtime_id = "mcs251-runtime-with-linking-exception"
    if component_files.get(runtime_id) != RUNTIME_FILES:
        fail("runtime exception component must cover exactly the four runtime sources")
    if components_by_id[runtime_id]["linking_exception"] != "present":
        fail("runtime component must record its linking exception as present")
    for relative in sorted(RUNTIME_FILES):
        if EXCEPTION_TEXT not in (ROOT / relative).read_text(encoding="utf-8"):
            fail(f"runtime linking exception text missing: {relative}")

    header_id = "stc32g12k128-device-header"
    if component_files.get(header_id) != {DEVICE_HEADER}:
        fail("device-header component must cover exactly stc32g12k128.h")
    if components_by_id[header_id]["linking_exception"] != "not-present":
        fail("device header must record that no linking exception is present")
    if EXCEPTION_TEXT in (ROOT / DEVICE_HEADER).read_text(encoding="utf-8"):
        fail("device header exception status changed; qualified review is required")

    decisions = data.get("open_decisions")
    if not isinstance(decisions, list) or not decisions:
        fail("open_decisions must be a non-empty list")
    decision_ids: set[str] = set()
    for decision in decisions:
        if not isinstance(decision, dict):
            fail("open decision entries must be objects")
        decision_id = require_string(decision, "id", "open_decision")
        if decision_id in decision_ids:
            fail(f"duplicate open decision id: {decision_id}")
        decision_ids.add(decision_id)
        if decision.get("status") != "open":
            fail(f"{decision_id}: decision cannot be closed by the engineering validator")
        require_string(decision, "question", decision_id)

    if decision_ids != EXPECTED_DECISION_IDS:
        fail("open-decision inventory is incomplete or contains an unknown decision")

    return len(components), len(decisions)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--scope", type=Path, default=DEFAULT_SCOPE)
    parser.add_argument(
        "--require-binary-release-approval",
        action="store_true",
        help="fail unless the scoped qualified legal review permits a binary release",
    )
    args = parser.parse_args()
    try:
        component_count, decision_count = validate_scope(
            args.scope.resolve(), args.require_binary_release_approval
        )
    except (OSError, ValueError, json.JSONDecodeError, subprocess.CalledProcessError) as error:
        print(f"legal-review-check: FAIL: {error}", file=sys.stderr)
        return 1
    print(
        "legal-review-check: PASS "
        f"components={component_count} open_decisions={decision_count} "
        "status=pending-qualified-legal-review"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
