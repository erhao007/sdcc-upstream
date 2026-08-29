#!/usr/bin/env python3
"""Validate isa/mcs251.yaml (and cross-check registers/addressing_modes).

Checks:
  1. top-level structure and instruction_count consistency
  2. per-instruction required fields and types
  3. unique ids; 65 families; mnemonic/family case conventions
  4. length == len(source_bytes); byte values 0x00..0xff
  5. flags are a subset of {CY, AC, OV, N, Z}
  6. source_bytes unique; non-null binary_bytes unique
  7. operand tokens exist in isa/addressing_modes.yaml
  8. (optional --tsv) sync check against the source TSV

Usage:
    PYTHONPATH=tools/pylib python3 tools/opcode_check.py [--tsv <forms.tsv>]
"""
import argparse
import csv
import sys
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parent.parent
ISA = ROOT / "isa"
ALLOWED_FLAGS = {"CY", "AC", "OV", "N", "Z"}
REQUIRED_FIELDS = ("id", "mnemonic", "family", "operands", "assembly",
                   "source_bytes", "binary_bytes", "length", "flags", "reference")
EXPECTED_FAMILIES = 65

errors = []
warnings = []


def err(msg):
    errors.append(msg)


def warn(msg):
    warnings.append(msg)


def check_bytes(field, inst):
    if field not in inst:
        return None
    val = inst[field]
    if val is None:
        return None
    if not isinstance(val, list):
        err(f"{inst['id']}: {field} must be a list or null")
        return None
    out = []
    for b in val:
        if not isinstance(b, str) or not b.lower().startswith("0x"):
            err(f"{inst['id']}: {field} entry {b!r} not a 0x hex string")
            continue
        try:
            n = int(b, 16)
        except ValueError:
            err(f"{inst['id']}: {field} entry {b!r} not a hex int")
            continue
        if not (0 <= n <= 0xFF):
            err(f"{inst['id']}: {field} byte {b!r} out of range")
        out.append(n)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tsv", help="optional source TSV for sync check")
    args = ap.parse_args()

    doc = yaml.safe_load(open(ISA / "mcs251.yaml", encoding="utf-8"))
    if doc.get("schema_version") != 1:
        err("schema_version must be 1")
    if doc.get("arch") != "mcs251":
        err("arch must be mcs251")
    if doc.get("mode") != "source":
        err("mode must be source")

    ins = doc.get("instructions")
    if not isinstance(ins, list) or not ins:
        err("instructions must be a non-empty list")
        sys.exit(1)
    if doc.get("instruction_count") != len(ins):
        err(f"instruction_count {doc.get('instruction_count')} != {len(ins)}")

    ids = set()
    families = set()
    src_seen = {}
    bin_seen = {}
    addr_tokens = set()
    if (ISA / "addressing_modes.yaml").exists():
        am = yaml.safe_load(open(ISA / "addressing_modes.yaml", encoding="utf-8"))
        addr_tokens = {t for m in am.get("modes", []) for t in m.get("tokens", [])}

    for inst in ins:
        for f in REQUIRED_FIELDS:
            if f not in inst:
                err(f"{inst.get('id', '?')}: missing field {f}")
        if not isinstance(inst.get("operands"), list):
            err(f"{inst.get('id')}: operands must be a list")
        iid = inst.get("id")
        if iid in ids:
            err(f"duplicate id {iid}")
        ids.add(iid)
        fam = inst.get("family")
        if fam:
            families.add(fam)
        if inst.get("mnemonic") != inst.get("mnemonic", "").upper():
            err(f"{iid}: mnemonic must be uppercase")
        if inst.get("family") != inst.get("mnemonic", "").lower():
            err(f"{iid}: family must equal lowercase mnemonic")
        src = check_bytes("source_bytes", inst)
        binf = check_bytes("binary_bytes", inst)
        if src is not None:
            if inst.get("length") != len(src):
                err(f"{iid}: length {inst.get('length')} != {len(src)}")
            t = tuple(src)
            if t in src_seen:
                err(f"{iid}: source_bytes collision with {src_seen[t]}")
            src_seen[t] = iid
        if binf is not None:
            t = tuple(binf)
            if t in bin_seen:
                err(f"{iid}: binary_bytes collision with {bin_seen[t]}")
            bin_seen[t] = iid
        for fl in inst.get("flags", []):
            if fl not in ALLOWED_FLAGS:
                err(f"{iid}: unknown flag {fl!r}")
        # Intel 8XC251SB UM Table 5-10: AC is affected by byte operations
        # only.  Width is decided by the DESTINATION operand (the first
        # one): "Rm,@WRj" stores a byte into Rm, so it DOES affect AC —
        # only WRj/DRk destinations are word/dword wide.
        ops = [str(o) for o in inst.get("operands", [])]
        dst = ops[0] if ops else ""
        dst_wide = dst.startswith("WR") or dst.startswith("DR")
        if dst_wide and inst.get("mnemonic", "").upper() in (
                "ADD", "SUB", "SUBB", "CMP") and "AC" in inst.get("flags", []):
            err(f"{iid}: wide-destination arithmetic must not list AC (UM 5-10)")
        if (not dst_wide) and inst.get("mnemonic", "").upper() in (
                "ADD", "SUB", "SUBB", "CMP") and "AC" not in inst.get("flags", []):
            err(f"{iid}: byte-destination arithmetic should list AC (UM 5-10)")
        for tok in inst.get("operands", []):
            if addr_tokens and tok not in addr_tokens:
                err(f"{iid}: operand token {tok!r} not in addressing_modes.yaml")

    if len(families) != EXPECTED_FAMILIES:
        err(f"family count {len(families)} != expected {EXPECTED_FAMILIES}")

    if args.tsv:
        rows = list(csv.DictReader(open(args.tsv), delimiter="\t"))
        if len(rows) != len(ins):
            err(f"TSV row count {len(rows)} != yaml {len(ins)}")
        tsv_ids = {r["id"] for r in rows}
        yaml_ids = {i["id"] for i in ins}
        if tsv_ids != yaml_ids:
            err(f"id sets differ: only-in-tsv={sorted(tsv_ids - yaml_ids)} only-in-yaml={sorted(yaml_ids - tsv_ids)}")

    for w in warnings:
        print(f"WARN: {w}")
    if errors:
        print(f"FAIL: {len(errors)} error(s)")
        for e in errors[:30]:
            print(f"  - {e}")
        sys.exit(1)
    print(f"PASS: {len(ins)} instructions, {len(families)} families, "
          f"{len(src_seen)} unique source encodings, "
          f"{len(bin_seen)} unique binary encodings")


if __name__ == "__main__":
    main()
