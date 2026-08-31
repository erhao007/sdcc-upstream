#!/usr/bin/env python3
"""Fail-closed tests for install and release-package boundaries."""

from __future__ import annotations

import hashlib
import io
import json
import os
import platform as host_platform
import stat
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path, PurePosixPath


STC32_ROOT = Path(__file__).resolve().parents[1]
TOOLS = STC32_ROOT / "tools"
REPOSITORY = STC32_ROOT.parents[1]
sys.path.insert(0, str(TOOLS))

import package_install  # noqa: E402
import verify_install  # noqa: E402
import verify_release_assets  # noqa: E402


PLATFORM_BY_HOST = {
    ("Darwin", "arm64"): "macos-arm64",
    ("Linux", "x86_64"): "linux-x86_64",
    ("Windows", "amd64"): "windows-x86_64",
}


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


class ReleaseToolTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "source"
        self.prefix = self.root / "prefix"
        self.output = self.root / "dist"
        self.source.mkdir()
        self.prefix.mkdir()
        subprocess.run(["git", "init", "-q"], cwd=self.source, check=True)
        subprocess.run(
            ["git", "config", "user.email", "release-tests@example.invalid"],
            cwd=self.source,
            check=True,
        )
        subprocess.run(
            ["git", "config", "user.name", "OpenSTC32 release tests"],
            cwd=self.source,
            check=True,
        )
        (self.source / "tracked.txt").write_text("source\n", encoding="utf-8")
        subprocess.run(["git", "add", "tracked.txt"], cwd=self.source, check=True)
        subprocess.run(
            ["git", "commit", "-q", "-m", "test source"],
            cwd=self.source,
            check=True,
        )
        self.head, self.state_sha256, dirty = verify_install.source_state(self.source)
        self.assertFalse(dirty)

        system = host_platform.system()
        machine = host_platform.machine().lower()
        key = (system, machine)
        if key not in PLATFORM_BY_HOST:
            self.skipTest(f"unsupported release-test host: {system}/{machine}")
        self.platform = PLATFORM_BY_HOST[key]
        host_os, host_arch, _ = verify_release_assets.PLATFORMS[self.platform]
        self.host_os = host_os
        self.host_arch = host_arch

        executable = self.prefix / "bin" / "sdcc"
        executable.parent.mkdir(parents=True)
        executable.write_bytes(b"synthetic-sdcc\n")
        executable.chmod(0o755)
        self._write_manifests()

    def _write_manifests(self) -> None:
        metadata_dir = self.prefix / "share" / "openstc32"
        metadata_dir.mkdir(parents=True, exist_ok=True)
        files = []
        metadata_paths = {
            Path("share/openstc32/toolchain.json"),
            Path("share/openstc32/toolchain-artifacts.json"),
        }
        for path in sorted(self.prefix.rglob("*")):
            if not path.is_file() or path.relative_to(self.prefix) in metadata_paths:
                continue
            data = path.read_bytes()
            files.append(
                {
                    "path": path.relative_to(self.prefix).as_posix(),
                    "size": len(data),
                    "sha256": sha256_bytes(data),
                }
            )
        identity = {
            "schema": 1,
            "target": "stc32",
            "architecture": "mcs251",
            "chip": "STC32G12K128",
            "host_os": self.host_os,
            "host_arch": self.host_arch,
            "sdcc_version": "synthetic test toolchain",
            "source_commit": self.head,
            "source_dirty": False,
            "source_state_sha256": self.state_sha256,
        }
        artifact = {**identity, "files": files}
        artifact_path = metadata_dir / "toolchain-artifacts.json"
        artifact_path.write_text(
            json.dumps(artifact, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        toolchain = {
            **identity,
            "artifact_manifest": "share/openstc32/toolchain-artifacts.json",
            "artifact_manifest_sha256": hashlib.sha256(
                artifact_path.read_bytes()
            ).hexdigest(),
        }
        (metadata_dir / "toolchain.json").write_text(
            json.dumps(toolchain, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )

    def _run_packager(self) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                sys.executable,
                str(TOOLS / "package_install.py"),
                str(self.prefix),
                str(self.output),
                "--tag",
                "test-release",
                "--platform",
                self.platform,
                "--source-root",
                str(self.source),
            ],
            check=True,
            text=True,
            capture_output=True,
        )

    def _run_verifier(self, commit: str | None = None) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                sys.executable,
                str(TOOLS / "verify_release_assets.py"),
                str(self.output),
                "--tag",
                "test-release",
                "--source-commit",
                commit or self.head,
                "--platform",
                self.platform,
            ],
            check=False,
            text=True,
            capture_output=True,
        )

    def test_valid_package_and_asset_boundary_failures(self) -> None:
        verified = verify_install.verify(
            self.prefix, self.source, self.host_os, self.host_arch
        )
        self.assertEqual(verified, 1)
        self._run_packager()
        result = self._run_verifier()
        self.assertEqual(result.returncode, 0, result.stderr)

        unexpected = self.output / "unexpected.bin"
        unexpected.write_bytes(b"stale asset")
        result = self._run_verifier()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("release asset boundary mismatch", result.stderr)
        unexpected.unlink()

        result = self._run_verifier("0" * 40)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("source_commit", result.stderr)

        package = next(
            path
            for path in self.output.iterdir()
            if path.name.endswith((".tar.gz", ".zip"))
        )
        original_package = package.read_bytes()
        package.write_bytes(original_package + b"tampered")
        result = self._run_verifier()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("package digest mismatch", result.stderr)
        package.write_bytes(original_package)

        checksum = next(self.output.glob("*.sha256"))
        original = checksum.read_bytes()
        checksum.write_text("0" * 64 + "  wrong.zip\n", encoding="ascii")
        result = self._run_verifier()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("checksum file mismatch", result.stderr)
        checksum.write_bytes(original)

        checksum.unlink()
        result = self._run_verifier()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing release asset", result.stderr)

    def test_install_rejects_extra_dirty_and_unsafe_paths(self) -> None:
        extra = self.prefix / "unlisted.bin"
        extra.write_bytes(b"not in manifest")
        with self.assertRaisesRegex(SystemExit, "artifact coverage mismatch"):
            verify_install.verify(self.prefix, self.source)
        extra.unlink()

        (self.source / "untracked.txt").write_text("dirty\n", encoding="utf-8")
        with self.assertRaisesRegex(SystemExit, "current source tree is dirty"):
            verify_install.verify(self.prefix, self.source)
        (self.source / "untracked.txt").unlink()

        executable = self.prefix / "bin" / "sdcc"
        original = executable.read_bytes()
        executable.write_bytes(original + b"tampered")
        with self.assertRaisesRegex(SystemExit, "artifact mismatch"):
            verify_install.verify(self.prefix, self.source)
        executable.write_bytes(original)

        with self.assertRaisesRegex(SystemExit, "unexpected host OS"):
            verify_install.verify(self.prefix, self.source, "WrongOS", self.host_arch)

        artifact_path = self.prefix / "share/openstc32/toolchain-artifacts.json"
        artifact = json.loads(artifact_path.read_text(encoding="utf-8"))
        artifact["files"][0]["path"] = "..\\escape.exe"
        artifact_path.write_text(json.dumps(artifact), encoding="utf-8")
        with self.assertRaisesRegex(SystemExit, "unsafe artifact path"):
            verify_install.verify(self.prefix, self.source)

    def test_archive_reader_rejects_unsafe_and_special_members(self) -> None:
        tar_path = self.root / "unsafe.tar.gz"
        with tarfile.open(tar_path, "w:gz") as archive:
            info = tarfile.TarInfo("../escape")
            info.size = 1
            archive.addfile(info, io.BytesIO(b"x"))
        with self.assertRaisesRegex(SystemExit, "unsafe archive member"):
            verify_release_assets.read_archive(tar_path)

        with tarfile.open(tar_path, "w:gz") as archive:
            info = tarfile.TarInfo("link")
            info.type = tarfile.SYMTYPE
            info.linkname = "target"
            archive.addfile(info)
        with self.assertRaisesRegex(SystemExit, "non-file archive member"):
            verify_release_assets.read_archive(tar_path)

        with tarfile.open(tar_path, "w:gz") as archive:
            for content in (b"a", b"b"):
                info = tarfile.TarInfo("duplicate")
                info.size = 1
                archive.addfile(info, io.BytesIO(content))
        with self.assertRaisesRegex(SystemExit, "duplicate archive member"):
            verify_release_assets.read_archive(tar_path)

        zip_path = self.root / "unsafe.zip"
        with zipfile.ZipFile(zip_path, "w") as archive:
            info = zipfile.ZipInfo("link")
            info.create_system = 3
            info.external_attr = (stat.S_IFLNK | 0o777) << 16
            archive.writestr(info, "target")
        with self.assertRaisesRegex(SystemExit, "non-file archive member"):
            verify_release_assets.read_archive(zip_path)

        with zipfile.ZipFile(zip_path, "w") as archive:
            info = zipfile.ZipInfo("C:\\escape.exe")
            info.create_system = 3
            info.external_attr = (stat.S_IFREG | 0o644) << 16
            archive.writestr(info, b"x")
        with self.assertRaisesRegex(SystemExit, "unsafe archive member"):
            verify_release_assets.read_archive(zip_path)

        regular_zip = self.root / "regular.zip"
        package_install.write_zip(self.prefix, regular_zip, 315532800)
        regular_members = verify_release_assets.read_archive(regular_zip)
        self.assertIn(PurePosixPath("bin/sdcc"), regular_members)

    def test_hardlinks_are_expanded_and_release_workflow_is_immutable(self) -> None:
        executable = self.prefix / "bin" / "sdcc"
        alias = self.prefix / "bin" / "sdar"
        try:
            os.link(executable, alias)
        except OSError as exc:
            self.skipTest(f"hard links unavailable: {exc}")
        self._write_manifests()
        self._run_packager()
        package = next(
            path
            for path in self.output.iterdir()
            if path.name.endswith((".tar.gz", ".zip"))
        )
        members = verify_release_assets.read_archive(package)
        self.assertEqual(
            members[PurePosixPath("bin/sdcc")], members[PurePosixPath("bin/sdar")]
        )

        workflow = (REPOSITORY / ".github/workflows/release.yml").read_text(
            encoding="utf-8"
        )
        windows_job = workflow.split("  windows-package:", 1)[1].split(
            "  publish-release:", 1
        )[0]
        self.assertIn("timeout-minutes: 360", windows_job)
        self.assertNotIn("gh release upload", workflow)
        self.assertIn("refusing to mutate existing release", workflow)


if __name__ == "__main__":
    unittest.main()
