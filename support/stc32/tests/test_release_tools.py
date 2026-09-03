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
from unittest import mock


STC32_ROOT = Path(__file__).resolve().parents[1]
TOOLS = STC32_ROOT / "tools"
REPOSITORY = STC32_ROOT.parents[1]
sys.path.insert(0, str(TOOLS))

import package_install  # noqa: E402
import install_identity  # noqa: E402
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

        for role, (name, _arguments, _codes, _marker) in \
                install_identity.TOOL_COMMANDS.items():
            executable = self.prefix / "bin" / name
            executable.parent.mkdir(parents=True, exist_ok=True)
            executable.write_bytes(f"synthetic-{role}\n".encode())
            executable.chmod(0o755)
        for model in install_identity.RUNTIME_MODELS:
            runtime = (
                self.prefix / "share" / "sdcc" / "lib" / model / "libsdcc.lib"
            )
            runtime.parent.mkdir(parents=True, exist_ok=True)
            runtime.write_bytes(f"synthetic-{model}\n".encode())
        header = (
            self.prefix / "share" / "sdcc" / "include" / "mcs251" /
            "stc32g12k128.h"
        )
        header.parent.mkdir(parents=True, exist_ok=True)
        header.write_bytes(b"synthetic-device-header\n")
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
        source_checkout = {
            "repository": install_identity.SOURCE_REPOSITORY,
            "commit": self.head,
            "dirty": False,
            "state_sha256": self.state_sha256,
        }
        compatibility = {
            "abi": {"major": 1, "minor": 0},
            "memory_models": [
                {"model": values["memory_model"],
                 "stack_auto": values["stack_auto"]}
                for values in install_identity.RUNTIME_MODELS.values()
            ],
        }
        identity = {
            "target": "stc32",
            "architecture": "mcs251",
            "chip": "STC32G12K128",
            "host_os": self.host_os,
            "host_arch": self.host_arch,
            "sdcc_version": "synthetic test toolchain",
            "source": {
                "root": dict(source_checkout),
                "upstream": dict(source_checkout),
            },
            "source_commit": self.head,
            "source_dirty": False,
            "source_state_sha256": self.state_sha256,
            "compatibility": compatibility,
            "built_at_utc": "2026-01-01T00:00:00+00:00",
        }
        artifact = {
            "schema": install_identity.ARTIFACT_SCHEMA,
            **identity,
            "files": files,
            "files_sha256": install_identity.records_sha256(files),
        }
        artifact_path = metadata_dir / "toolchain-artifacts.json"
        artifact_path.write_text(
            json.dumps(artifact, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        tools = {}
        for role, (name, _arguments, _codes, _marker) in \
                install_identity.TOOL_COMMANDS.items():
            record = install_identity.file_record(
                self.prefix / "bin" / name, self.prefix)
            record["version"] = f"synthetic {role} version"
            tools[role] = record
        runtimes = {}
        for model, attributes in install_identity.RUNTIME_MODELS.items():
            path = self.prefix / "share" / "sdcc" / "lib" / model / "libsdcc.lib"
            runtimes[model] = {
                **attributes,
                **install_identity.file_record(path, self.prefix),
            }
        header_records = [install_identity.file_record(
            self.prefix / "share" / "sdcc" / "include" / "mcs251" /
            "stc32g12k128.h", self.prefix)]
        toolchain = {
            "schema": install_identity.TOOLCHAIN_SCHEMA,
            **identity,
            "tools": tools,
            "runtimes": runtimes,
            "headers": {
                "root": "share/sdcc/include/mcs251",
                "files": header_records,
                "files_sha256": install_identity.records_sha256(header_records),
            },
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
        artifact = json.loads((
            self.prefix / "share/openstc32/toolchain-artifacts.json"
        ).read_text(encoding="utf-8"))
        self.assertEqual(verified, len(artifact["files"]))
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

    def test_generator_writes_v2_complete_identity(self) -> None:
        metadata = self.prefix / "share" / "openstc32"
        for name in ("toolchain.json", "toolchain-artifacts.json"):
            (metadata / name).unlink()

        def fake_tool_record(prefix, name, _arguments, _codes, _marker):
            record = install_identity.file_record(prefix / "bin" / name, prefix)
            record["version"] = f"synthetic {name} version"
            return record

        with mock.patch.object(
                install_identity, "tool_record", side_effect=fake_tool_record):
            manifest_path, artifact_path, count = install_identity.build_manifests(
                self.source, self.prefix)
        toolchain = json.loads(manifest_path.read_text(encoding="utf-8"))
        artifacts = json.loads(artifact_path.read_text(encoding="utf-8"))
        self.assertEqual(toolchain["schema"], install_identity.TOOLCHAIN_SCHEMA)
        self.assertEqual(artifacts["schema"], install_identity.ARTIFACT_SCHEMA)
        self.assertEqual(set(toolchain["tools"]), set(install_identity.TOOL_COMMANDS))
        self.assertEqual(set(toolchain["runtimes"]), set(install_identity.RUNTIME_MODELS))
        self.assertEqual(toolchain["source"]["root"], toolchain["source"]["upstream"])
        self.assertFalse(toolchain["source_dirty"])
        self.assertEqual(count, len(artifacts["files"]))

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

    def test_install_rejects_legacy_missing_and_incompatible_identity(self) -> None:
        metadata = self.prefix / "share" / "openstc32"
        toolchain_path = metadata / "toolchain.json"
        artifact_path = metadata / "toolchain-artifacts.json"

        toolchain = json.loads(toolchain_path.read_text(encoding="utf-8"))
        toolchain["schema"] = 1
        toolchain_path.write_text(json.dumps(toolchain), encoding="utf-8")
        with self.assertRaisesRegex(SystemExit, "unsupported toolchain manifest schema"):
            verify_install.verify(self.prefix, self.source)

        self._write_manifests()
        toolchain = json.loads(toolchain_path.read_text(encoding="utf-8"))
        toolchain["tools"].pop("linker")
        toolchain_path.write_text(json.dumps(toolchain), encoding="utf-8")
        with self.assertRaisesRegex(SystemExit, "incomplete host-tool identity"):
            verify_install.verify(self.prefix, self.source)

        self._write_manifests()
        toolchain = json.loads(toolchain_path.read_text(encoding="utf-8"))
        artifact = json.loads(artifact_path.read_text(encoding="utf-8"))
        toolchain["compatibility"]["abi"]["major"] = 2
        artifact["compatibility"]["abi"]["major"] = 2
        toolchain_path.write_text(json.dumps(toolchain), encoding="utf-8")
        artifact_path.write_text(json.dumps(artifact), encoding="utf-8")
        with self.assertRaisesRegex(SystemExit, "incompatible ABI"):
            verify_install.verify(self.prefix, self.source)

        self._write_manifests()
        artifact = json.loads(artifact_path.read_text(encoding="utf-8"))
        artifact["files_sha256"] = "0" * 64
        artifact_path.write_text(json.dumps(artifact), encoding="utf-8")
        with self.assertRaisesRegex(SystemExit, "artifact file-set digest mismatch"):
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
