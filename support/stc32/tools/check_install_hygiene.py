#!/usr/bin/env python3
"""Run the package hygiene rule on a staged install, before expensive gates."""
import argparse
from pathlib import Path, PurePosixPath

from sanitize_generated_paths import path_variants
from validate_package_install import verify_package_hygiene


def check_install(prefix: Path, forbidden: list[str]) -> int:
    if not prefix.is_dir():
        raise SystemExit(f"missing install directory: {prefix}")
    variants = sorted({variant for root in forbidden if root
                       for variant in path_variants(root)})
    count = 0
    for path in sorted(prefix.rglob("*")):
        if path.is_file():
            relative = PurePosixPath(path.relative_to(prefix).as_posix())
            verify_package_hygiene({relative: path.read_bytes()}, variants)
            count += 1
    if not count:
        raise SystemExit("empty installation")
    return count


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("prefix", type=Path)
    parser.add_argument("--forbid-path", action="append", default=[])
    args = parser.parse_args()
    count = check_install(args.prefix, args.forbid_path)
    print(f"Install hygiene passed before regression gates: files={count}")


if __name__ == "__main__":
    main()
