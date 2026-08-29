#!/usr/bin/env python3
"""Check that strict unknown-opcode handling is scoped to MCS-251."""
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

SUPPORT_ROOT = Path(__file__).resolve().parent.parent
ROOT = SUPPORT_ROOT.parent.parent
TOOLCHAIN_ROOT = Path(os.environ.get("STC32_TOOLCHAIN_ROOT", str(ROOT / "build" / "install")))
SIM = Path(os.environ.get("STC32_UCSIM", str(TOOLCHAIN_ROOT / "bin" / "ucsim_51")))
if os.name == "nt" and not SIM.exists() and Path(str(SIM) + ".exe").exists():
    SIM = Path(str(SIM) + ".exe")


def ihex(data: bytes) -> str:
    record = bytes([len(data), 0, 0, 0]) + data
    return ":" + record.hex().upper() + f"{(-sum(record)) & 0xff:02X}\n:00000001FF\n"


def run(cpu: str, unknown_mode: str):
    # A5 is undefined on legacy 8051 and A5 00 is an unsupported Source Mode
    # sub-form.  The following NOP proves that mcs51 unknown_code=off keeps
    # executing, while MCS-251 must stop at the prefix.
    image = ihex(bytes([0xA5, 0x00, 0x80, 0xFE]))
    commands = (
        f"set error unknown_code {unknown_mode}\n"
        "set opt selfjump_stop 0\n"
        "step 2\nstate\nquit\n"
    )
    with tempfile.TemporaryDirectory(prefix="ucsim-unknown-") as td:
        work = Path(td)
        (work / "probe.ihx").write_text(image)
        return subprocess.run(
            [str(SIM), f"-t{cpu}", "-c", "-", "-m",
             "-S", f"in={os.devnull},out=-", "probe.ihx"],
            cwd=work, input=commands, text=True, errors="replace",
            capture_output=True, timeout=20,
        )


def main() -> int:
    if not SIM.exists():
        print(f"error: missing {SIM}; build uCsim first", file=sys.stderr)
        return 2

    legacy = run("51", "off")
    legacy_out = legacy.stdout + legacy.stderr
    legacy_pc = re.search(r"PC=\s*0x([0-9a-f]+)", legacy_out)
    if legacy.returncode != 0 or not legacy_pc or int(legacy_pc.group(1), 16) != 2:
        print(f"FAIL mcs51 unknown_code=off: rc={legacy.returncode}\n{legacy_out}")
        return 1

    strict = run("251", "on")
    strict_out = strict.stdout + strict.stderr
    strict_pc = re.search(r"PC=\s*0x([0-9a-f]+)", strict_out)
    if strict.returncode != 106 or not strict_pc or int(strict_pc.group(1), 16) != 0:
        print(f"FAIL mcs251 strict unknown opcode: rc={strict.returncode}\n{strict_out}")
        return 1

    print("PASS unknown opcode scope (mcs51 continues; mcs251 stops at PC=0)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
