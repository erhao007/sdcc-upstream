"""Assert the negative control fails hygiene and the mapped PE preserves assert."""
from pathlib import Path, PurePosixPath
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from validate_package_install import verify_package_hygiene

directory = Path(sys.argv[1])
host_prefix = sys.argv[2]
member = PurePosixPath("bin/probe.exe")
unmapped = (directory / "unmapped.exe").read_bytes()
mapped = (directory / "mapped.exe").read_bytes()
try:
    verify_package_hygiene({member: unmapped}, [host_prefix])
except SystemExit as exc:
    if "absolute build path" not in str(exc):
        raise
    print(f"Negative control rejected after strip: {exc}")
else:
    raise SystemExit("negative control lost its host-header assertion path")
verify_package_hygiene({member: mapped}, [host_prefix])
if b".host/include/boost/smart_ptr/shared_ptr.hpp" not in mapped:
    raise SystemExit("mapped binary must retain Boost assertion provenance")
print("Mapped PE contains relative Boost assertion path and no host prefix")
