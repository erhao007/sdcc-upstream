#!/usr/bin/env python3
"""Convert the zevorn/sdcc-c251 instruction-forms.tsv into isa/mcs251.yaml.

The TSV is the machine-readable ISA matrix of the GPL SDCC-derived project
https://github.com/zevorn/sdcc-c251 (data itself comes from the public Intel
8XC251SB and STC32G instruction tables; every row cites the Intel manual page).

Usage:
    python3 tools/tsv_to_yaml.py <instruction-forms.tsv> [output.yaml]

Output is deterministic (no timestamps) so the generated YAML is diff-friendly.
"""
import csv
import sys
from pathlib import Path

import yaml  # needs pyyaml; see isa/README.md

ALLOWED_FLAGS = {"CY", "AC", "OV", "N", "Z"}


def parse_bytes(s: str):
    """'a5 2e' -> [0xa5, 0x2e]; empty/'-' -> None"""
    s = s.strip()
    if not s or s == "-":
        return None
    return [int(b, 16) for b in s.split()]


def parse_flags(s: str):
    s = s.strip()
    if not s or s == "-":
        return []
    out = []
    for f in s.split(","):
        f = f.strip()
        if not f:
            continue
        if f not in ALLOWED_FLAGS:
            raise ValueError(f"unknown flag {f!r}")
        out.append(f)
    return out


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    tsv_path = Path(sys.argv[1])
    out_path = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(__file__).resolve().parent.parent / "isa" / "mcs251.yaml"

    instructions = []
    with open(tsv_path, newline="") as f:
        for row in csv.DictReader(f, delimiter="\t"):
            mnemonic = row["mnemonic"].strip()
            source = parse_bytes(row["source_bytes"])
            if source is None:
                raise ValueError(f"row {row['id']}: missing source_bytes")
            binary = parse_bytes(row["binary_bytes"])
            instructions.append({
                "id": row["id"].strip(),
                "mnemonic": mnemonic.upper(),
                "family": mnemonic.lower(),
                "operands": [o.strip() for o in row["operands"].split(",") if o.strip()],
                "assembly": row["assembly"].strip(),
                "source_bytes": ["0x%02x" % b for b in source],
                "binary_bytes": None if binary is None else ["0x%02x" % b for b in binary],
                "length": len(source),
                "flags": parse_flags(row["flags"]),
                "reference": row["reference"].strip(),
            })

    doc = {
        "schema_version": 1,
        "arch": "mcs251",
        "mode": "source",
        "source": (
            "Generated from zevorn/sdcc-c251 sdas/as251/tests/instruction-forms.tsv "
            "(GPL-2 SDCC-derived; encoding data from public Intel 8XC251SB / STC32G "
            "instruction tables, each row cites the Intel manual page). "
            "Regenerate with tools/tsv_to_yaml.py."
        ),
        "instruction_count": len(instructions),
        "instructions": instructions,
    }

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, "w") as f:
        yaml.safe_dump(doc, f, sort_keys=False, allow_unicode=False, default_flow_style=False, width=100)
    print(f"wrote {len(instructions)} instructions to {out_path}")


if __name__ == "__main__":
    main()
