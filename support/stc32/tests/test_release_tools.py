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
import validate_package_install  # noqa: E402
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

    def _run_packager(self, output: Path | None = None) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                sys.executable,
                str(TOOLS / "package_install.py"),
                str(self.prefix),
                str(output or self.output),
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

    def _run_verifier(self, commit: str | None = None,
                      require_validation: bool = False) -> subprocess.CompletedProcess[str]:
        command = [
                sys.executable,
                str(TOOLS / "verify_release_assets.py"),
                str(self.output),
                "--tag",
                "test-release",
                "--source-commit",
                commit or self.head,
                "--platform",
                self.platform,
            ]
        if require_validation:
            command.append("--require-validation")
        return subprocess.run(
            command,
            check=False,
            text=True,
            capture_output=True,
        )

    def _write_validation(self) -> Path:
        stem = f"openstc32-toolchain-test-release-{self.platform}"
        release = json.loads((self.output / f"{stem}.release.json").read_text())
        toolchain = json.loads((self.output / f"{stem}.toolchain.json").read_text())
        validation = {
            "schema": validate_package_install.SCHEMA,
            "roadmap_id": "MT-5D",
            "validated_at_utc": "2026-01-01T00:00:00+00:00",
            "platform": self.platform,
            "package": release["package"],
            "package_sha256": release["package_sha256"],
            "source_commit": self.head,
            "source_state_sha256": release["source_state_sha256"],
            "host": {
                "system": self.host_os,
                "machine": self.host_arch,
                "release": "synthetic-release",
                "version": "synthetic-version",
                "platform": "synthetic-platform",
            },
            "native_compiler": {
                "command": ["<native-compiler>/cc", "--version"],
                "returncode": 0,
                "version": "synthetic native compiler",
            },
            "installed_compiler": {
                "command": ["<unpacked-prefix>/bin/sdcc", "--version"],
                "returncode": 0,
                "version": toolchain["sdcc_version"],
                "sha256": toolchain["tools"]["compiler"]["sha256"],
            },
            "commands": [
                {
                    "name": "installed-compiler-version",
                    "argv": ["<unpacked-prefix>/bin/sdcc", "--version"],
                    "returncode": 0,
                },
                {
                    "name": "native-compiler-version",
                    "argv": ["<native-compiler>/cc", "--version"],
                    "returncode": 0,
                },
                {
                    "name": "compile-clean-room-example",
                    "argv": [
                        "<unpacked-prefix>/bin/sdcc", "-mstc32", "--model-small",
                        "--code-loc", "0xFF0000", "<work-dir>/mt5d_smoke.c",
                        "-o", "<work-dir>/mt5d_smoke.ihx",
                    ],
                    "returncode": 0,
                },
            ],
            "example": {
                "source": "support/stc32/tests/package/mt5d_smoke.c",
                "source_sha256": sha256_bytes(
                    validate_package_install.EXAMPLE.read_bytes()),
                "output": "<work-dir>/mt5d_smoke.ihx",
                "output_sha256": "2" * 64,
                "code_start": 0xFF0000,
            },
        }
        path = self.output / f"{stem}.validation.json"
        path.write_text(json.dumps(validation), encoding="utf-8")
        return path

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

    def test_package_is_deterministic_and_rejects_build_paths(self) -> None:
        self._run_packager()
        second = self.root / "dist-second"
        self._run_packager(second)
        first_package = next(self.output.glob("*.tar.gz"), None) or next(
            self.output.glob("*.zip"))
        second_package = second / first_package.name
        self.assertEqual(first_package.read_bytes(), second_package.read_bytes())

        members = verify_release_assets.read_archive(first_package)
        executables = validate_package_install.executable_members(first_package)
        self.assertIn(PurePosixPath("bin/sdcc"), executables)
        validate_package_install.verify_package_hygiene(members, [str(self.source)])
        polluted = dict(members)
        polluted[PurePosixPath("bin/polluted")] = str(self.source).encode()
        with self.assertRaisesRegex(SystemExit, "absolute build path"):
            validate_package_install.verify_package_hygiene(
                polluted, [str(self.source)])
        polluted = dict(members)
        polluted[PurePosixPath("share/openstc32/build.log")] = b"log"
        with self.assertRaisesRegex(SystemExit, "temporary file"):
            validate_package_install.verify_package_hygiene(polluted, [])
        polluted = dict(members)
        polluted[PurePosixPath("lib/stale.la")] = b"libdir='/tmp/stale'"
        with self.assertRaisesRegex(SystemExit, "build metadata"):
            validate_package_install.verify_package_hygiene(polluted, [])

    def test_validation_sidecar_is_required_and_bound(self) -> None:
        self._run_packager()
        missing = self._run_verifier(require_validation=True)
        self.assertNotEqual(missing.returncode, 0)
        self.assertIn("cannot read JSON", missing.stderr)

        validation_path = self._write_validation()
        valid = self._run_verifier(require_validation=True)
        self.assertEqual(valid.returncode, 0, valid.stderr)

        validation = json.loads(validation_path.read_text())
        validation["commands"][2]["returncode"] = 1
        validation_path.write_text(json.dumps(validation), encoding="utf-8")
        failed = self._run_verifier(require_validation=True)
        self.assertNotEqual(failed.returncode, 0)
        self.assertIn("failed package command evidence", failed.stderr)

        validation["commands"][2]["returncode"] = 0
        validation["commands"][2]["argv"][0] = "/tmp/stale/sdcc"
        validation_path.write_text(json.dumps(validation), encoding="utf-8")
        failed = self._run_verifier(require_validation=True)
        self.assertNotEqual(failed.returncode, 0)
        self.assertIn("absolute path in package command evidence", failed.stderr)

        validation = json.loads(validation_path.read_text())
        validation["commands"][2]["argv"][0] = (
            validation["commands"][0]["argv"][0])
        validation["commands"][2]["argv"][1] = "-mmcs51"
        validation_path.write_text(json.dumps(validation), encoding="utf-8")
        failed = self._run_verifier(require_validation=True)
        self.assertNotEqual(failed.returncode, 0)
        self.assertIn("unexpected clean-room compile command", failed.stderr)

    def test_package_smoke_ihex_parser_is_fail_closed(self) -> None:
        path = self.root / "sample.ihx"
        path.write_text(":0200000400FFFB\n:010000005AA5\n:00000001FF\n")
        self.assertEqual(validate_package_install.parse_ihex_start(path), 0xFF0000)
        path.write_text(":0200000400FFFB\n:010000005AA4\n:00000001FF\n")
        with self.assertRaisesRegex(SystemExit, "invalid IHX record"):
            validate_package_install.parse_ihex_start(path)

    def test_generator_writes_v2_complete_identity(self) -> None:
        metadata = self.prefix / "share" / "openstc32"
        for name in ("toolchain.json", "toolchain-artifacts.json"):
            (metadata / name).unlink()

        def fake_tool_record(prefix, name, _arguments, _codes, _marker):
            record = install_identity.file_record(prefix / "bin" / name, prefix)
            record["version"] = f"synthetic {name} version"
            return record

        with mock.patch.dict(
                os.environ, {"STC32_SOURCE_COMMIT": self.head}), \
                mock.patch.object(
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
        self.assertIn("validate_package_install.py", workflow)
        self.assertIn("--require-validation", workflow)
        for name in ("ci.yml", "platforms.yml"):
            branch_workflow = (REPOSITORY / ".github/workflows" / name).read_text(
                encoding="utf-8"
            )
            self.assertIn("validate_package_install.py", branch_workflow)
            self.assertIn("--require-validation", branch_workflow)
            self.assertIn('--forbid-path "$PREFIX"', branch_workflow)
            self.assertIn('--forbid-path "$RUNNER_TEMP"', branch_workflow)

        build_script = (
            REPOSITORY / "support/stc32/scripts/build-toolchain.sh"
        ).read_text(encoding="utf-8")
        self.assertIn('CONFIGURE_PREFIX="/opt/openstc32"', build_script)
        self.assertIn('CONFIGURE_POLICY="relocatable-v2-disable-nls-zstd"',
                      build_script)
        self.assertIn('--prefix="$CONFIGURE_PREFIX"', build_script)
        self.assertIn('--disable-nls', build_script)
        self.assertIn('--without-zstd', build_script)
        self.assertIn('install DESTDIR="$INSTALL_STAGE"', build_script)
        self.assertIn('cpp_configargs="$BUILD_DIR/support/cpp/gcc/configargs.h"',
                      build_script)
        self.assertIn('if [[ "${CFLAGS+x}" == x ]]; then', build_script)
        self.assertIn('host_cflags="-g -O2"', build_script)
        self.assertIn('host_cxxflags="-g -O2"', build_script)
        self.assertIn('command rm -rf "$INSTALL_STAGE"', build_script)
        self.assertIn('unset COMPILER_PATH', build_script)
        self.assertIn('export COMPILER_PATH="$saved_compiler_path"',
                      build_script)
        self.assertIn("-name '*.la' -delete", build_script)
        self.assertIn("refusing to replace non-empty install prefix", build_script)

        windows_fixups = (
            REPOSITORY / "support/stc32/scripts/windows-build-fixups.sh"
        ).read_text(encoding="utf-8")
        post_host_tools = windows_fixups.split("post-host-tools)", 1)[1].split(
            "post-install)", 1
        )[0]
        self.assertIn('extensionless_copies "$BUILD_DIR/bin"',
                      post_host_tools)
        self.assertIn('copy_exact "$BUILD_DIR/bin/sdcc.exe" "$BUILD_DIR/bin/sdcc"',
                      post_host_tools)
        self.assertIn('copy_exact "$BUILD_DIR/bin/sdcpp.exe" "$BUILD_DIR/bin/sdcpp"',
                      post_host_tools)
        self.assertIn('[System.IO.File]::WriteAllBytes', windows_fixups)
        self.assertNotIn('rm -f "$BUILD_DIR/bin/sdcc"', post_host_tools)
        for runtime_dll in (
            "zlib1.dll",
            "libzstd.dll",
            "libgcc_s_seh-1.dll",
            "libstdc++-6.dll",
            "libwinpthread-1.dll",
            "libiconv-2.dll",
        ):
            self.assertIn(runtime_dll, windows_fixups)
        self.assertIn('for binary in "$dir"/*.exe "$dir"/*.dll',
                      windows_fixups)
        self.assertIn("windows-runtime-components.tsv", windows_fixups)
        self.assertIn('/ucrt64/share/licenses/$license_name', windows_fixups)
        self.assertIn('cp -a "$license_source"/. "$license_target/"',
                      windows_fixups)
        self.assertIn("mingw-w64-ucrt-x86_64-gcc-libs", windows_fixups)
        self.assertIn("mingw-w64-ucrt-x86_64-libwinpthread", windows_fixups)
        self.assertIn("mingw-w64-ucrt-x86_64-libiconv", windows_fixups)
        self.assertIn("mingw-w64-ucrt-x86_64-zlib", windows_fixups)

        posix_gates = (
            REPOSITORY / "support/stc32/scripts/run-posix-gates.sh"
        ).read_text(encoding="utf-8")
        self.assertIn('export SDCC_HOME="$PREFIX"', posix_gates)
        self.assertIn('SIM_TIMEOUT="${SIM_TIMEOUT:-30}"', posix_gates)

        windows_build = (
            REPOSITORY / "support/stc32/scripts/build-windows.sh"
        ).read_text(encoding="utf-8")
        self.assertIn('SIM_TIMEOUT="${SIM_TIMEOUT:-30}"', windows_build)
        self.assertIn('SIM_TIMEOUT="$SIM_TIMEOUT"', windows_build)
        self.assertIn('library_jobs=1', build_script)
        self.assertIn('-j"$library_jobs" model-mcs251', build_script)


if __name__ == "__main__":
    unittest.main()
