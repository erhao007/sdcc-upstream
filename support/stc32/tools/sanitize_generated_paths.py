#!/usr/bin/env python3
"""Remove transient checkout paths from generated host-build sources."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


def path_variants(value: str) -> set[str]:
    """Return POSIX, Windows, and C-string spellings for one path."""
    normalized = value.replace("\\", "/")
    variants = {
        value,
        normalized,
        normalized.replace("/", "\\"),
        normalized.replace("/", "\\\\"),
    }
    drive_path = re.match(r"^([A-Za-z]):(/.*)$", normalized)
    if drive_path:
        drive = drive_path.group(1)
        tail = drive_path.group(2)
        for letter in {drive.lower(), drive.upper()}:
            native = f"{letter}:{tail}"
            variants.update(
                {
                    native,
                    native.replace("/", "\\"),
                    native.replace("/", "\\\\"),
                }
            )
        msys = f"/{drive.lower()}{tail}"
        variants.update(
            {
                msys,
                msys.replace("/", "\\"),
                msys.replace("/", "\\\\"),
            }
        )
    else:
        msys_path = re.match(r"^/([A-Za-z])(/.*)$", normalized)
        if msys_path:
            drive = msys_path.group(1)
            tail = msys_path.group(2)
            for letter in {drive.lower(), drive.upper()}:
                native = f"{letter}:{tail}"
                variants.update(
                    {
                        native,
                        native.replace("/", "\\"),
                        native.replace("/", "\\\\"),
                    }
                )
    return {variant for variant in variants if variant}


def sanitize_text(text: str, replacements: list[tuple[str, str]]) -> str:
    expanded = {
        (variant, new)
        for old, new in replacements
        for variant in path_variants(old)
    }
    # BUILD_DIR commonly lives below ROOT. Replace the longest value first so
    # the build root becomes .build instead of the less precise ./build.
    for old, new in sorted(expanded, key=lambda item: len(item[0]), reverse=True):
        text = text.replace(old, new)
    return text


def sanitize_files(
    paths: list[Path], replacements: list[tuple[str, str]]
) -> list[Path]:
    changed: list[Path] = []
    for path in paths:
        content = path.read_text(encoding="utf-8")
        sanitized = sanitize_text(content, replacements)
        if sanitized != content:
            path.write_text(sanitized, encoding="utf-8")
            changed.append(path)
    return changed


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", required=True)
    parser.add_argument("--build-root", required=True)
    parser.add_argument("--host-prefix", action="append", default=[])
    parser.add_argument("paths", nargs="+", type=Path)
    args = parser.parse_args()
    changed = sanitize_files(
        args.paths,
        [(args.source_root, "."), (args.build_root, ".build")]
        + [(prefix, ".host") for prefix in args.host_prefix],
    )
    print(f"Sanitized transient paths from {len(changed)} generated build files")


if __name__ == "__main__":
    main()
