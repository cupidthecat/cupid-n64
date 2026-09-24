"""Exercise source, toolchain, and training-context binding for PGO packages."""

from __future__ import annotations

import copy
import importlib.util
import json
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
from unittest.mock import patch


PROFILE_PATH = Path(__file__).resolve().parents[1] / "profile.py"
PROFILE_SPEC = importlib.util.spec_from_file_location("cupid_profile_tool", PROFILE_PATH)
if PROFILE_SPEC is None or PROFILE_SPEC.loader is None:
    raise RuntimeError(f"Unable to load profile helper from {PROFILE_PATH}")
profile = importlib.util.module_from_spec(PROFILE_SPEC)
PROFILE_SPEC.loader.exec_module(profile)


class ProfilePackageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="profile package fixture ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / "checkout"
        self.root.mkdir()

        self.write("src/core.cpp", "int core() { return 1; }\n")
        self.write("src/main.cpp", "int main() { return 0; }\n")
        self.write("src/cpu/cpu.cpp", "int cpu() { return 2; }\n")
        self.write("src/host/host.cpp", "int host() { return 3; }\n")
        self.write("src/storage/save.cpp", "int storage() { return 4; }\n")
        self.write("src/rcp/detail.hpp", "#pragma once\n")
        self.write("include/cupid/core.hpp", "#pragma once\n")

        self.write("src/desktop/ui.cpp", "int desktop() { return 5; }\n")
        self.write("src/desktop/internal.hpp", "#pragma once\n")
        self.write("include/cupid/desktop/window.hpp", "#pragma once\n")
        self.write("tests/test_core.cpp", "// test fixture\n")
        self.write("docs/profile.md", "fixture docs\n")

        self.expected_compiled = [
            "src/core.cpp",
            "src/cpu/cpu.cpp",
            "src/host/host.cpp",
            "src/storage/save.cpp",
        ]
        self.expected_profiled_files = [
            "include/cupid/core.hpp",
            "src/core.cpp",
            "src/cpu/cpu.cpp",
            "src/host/host.cpp",
            "src/rcp/detail.hpp",
            "src/storage/save.cpp",
        ]

        self.profile_payload = self.root / ".work/profiles/merged.profdata"
        self.profile_payload.parent.mkdir(parents=True)
        self.profile_payload.write_bytes(b"merged profile data v1\n")

        self.compiler_file = self.root / ".work/toolchain/clang++.exe"
        self.compiler_file.parent.mkdir(parents=True)
        self.compiler_file.write_bytes(b"fixture clang binary\n")
        self.compiler = {
            "id": "Clang",
            "version": "18.1.0",
            "target_triple": "x86_64-pc-windows-msvc",
            "sha256": profile.file_digest(self.compiler_file),
        }
        self.target_settings = {
            "cupid_core.COMPILE_OPTIONS": "2d666e6f2d666173742d6d6174683b2d66726f756e64696e672d6d617468",
            "cupid_core.COMPILE_DEFINITIONS": "",
            "cupid_host.COMPILE_OPTIONS": "2d666e6f2d666173742d6d6174683b2d66726f756e64696e672d6d617468",
            "cupid_host.COMPILE_DEFINITIONS": "",
        }
        self.build = {
            "schema": profile.CODEGEN_SCHEMA,
            "compiler": copy.deepcopy(self.compiler),
            "build_type": "Release",
            "ipo": True,
            "sanitizers": False,
            "cxx_standard": 20,
            "cxx_flags": "-Wall",
            "release_flags": "-O3 -DNDEBUG",
            "linker_flags": "",
            "release_linker_flags": "",
            "strict_fp": ["-fno-fast-math", "-frounding-math"],
            "target_settings": copy.deepcopy(self.target_settings),
        }

        self.compiled_sources_file = self.root / ".work/build/cupid-profile-compiled-sources.txt"
        self.compiled_sources_file.parent.mkdir(parents=True)
        self.compiled_sources_file.write_text("\n".join(self.expected_compiled) + "\n", encoding="utf-8")
        self.build_cache = self.root / ".work/build/CMakeCache.txt"
        self.write_build_cache()

    def write(self, relative: str, contents: str) -> Path:
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(contents, encoding="utf-8")
        return path

    def write_build_cache(self, *, release_flags: str = "-O3 -DNDEBUG") -> None:
        self.build_cache.write_text(
            "\n".join([
                "CMAKE_BUILD_TYPE:STRING=Release",
                "CUPID_IPO:BOOL=ON",
                "CUPID_SANITIZERS:BOOL=OFF",
                f"CMAKE_CXX_COMPILER:FILEPATH={self.compiler_file}",
                "CMAKE_CXX_FLAGS:STRING=-Wall",
                f"CMAKE_CXX_FLAGS_RELEASE:STRING={release_flags}",
                "CUPID_PROFILE_GENERATE:PATH=",
                "",
            ]),
            encoding="utf-8",
        )

    def manifest(self) -> dict:
        return {
            "schema": profile.SCHEMA,
            "profile": {
                "bytes": self.profile_payload.stat().st_size,
                "sha256": profile.file_digest(self.profile_payload),
            },
            "source": profile.source_identity(self.root),
            "build": copy.deepcopy(self.build),
        }

    def verify(self, manifest: dict | None = None, *, build: dict | None = None,
               compiled_sources: list[str] | None = None) -> None:
        profile.verify_package(
            manifest or self.manifest(),
            self.root,
            self.profile_payload,
            build or copy.deepcopy(self.build),
            self.expected_compiled if compiled_sources is None else compiled_sources,
        )

    def write_training_context(self, *, source: dict | None = None,
                               build: dict | None = None) -> Path:
        context = self.root / ".work/training/context.json"
        context.parent.mkdir(parents=True, exist_ok=True)
        context.write_text(
            json.dumps({
                "schema": profile.SCHEMA,
                "source": source or profile.source_identity(self.root),
                "build": build or copy.deepcopy(self.build),
            }, sort_keys=True, indent=2) + "\n",
            encoding="utf-8",
        )
        return context

    def create_args(self, training_context: Path | None) -> SimpleNamespace:
        return SimpleNamespace(
            source_root=self.root,
            profile=self.profile_payload,
            build_cache=self.build_cache,
            training_context=training_context,
            llvm_profdata=None,
        )

    def assert_build_change_rejected(self, actual_build: dict) -> None:
        with self.assertRaisesRegex(ValueError, "toolchain or code-generation identity is stale"):
            self.verify(build=actual_build)

    def test_valid_package_accepts_literal_core_host_membership(self):
        source = profile.source_identity(self.root)
        self.assertEqual(source["compiled_sources"], self.expected_compiled)
        self.assertEqual(sorted(source["files"]), self.expected_profiled_files)
        self.verify()

    def test_changed_profile_payload_rejected(self):
        manifest = self.manifest()
        self.profile_payload.write_bytes(b"merged profile data v2\n")
        with self.assertRaisesRegex(ValueError, "Profile payload does not match its manifest"):
            self.verify(manifest)

    def test_changed_profiled_source_rejected(self):
        manifest = self.manifest()
        self.write("src/cpu/cpu.cpp", "int cpu() { return 99; }\n")
        with self.assertRaisesRegex(ValueError, r"changed profiled source: src/cpu/cpu\.cpp"):
            self.verify(manifest)

    def test_added_profiled_source_rejected(self):
        manifest = self.manifest()
        self.write("src/rcp/new.cpp", "int added() { return 1; }\n")
        with self.assertRaisesRegex(ValueError, r"new profiled source: src/rcp/new\.cpp"):
            self.verify(manifest)

    def test_removed_profiled_source_rejected(self):
        manifest = self.manifest()
        (self.root / "src/host/host.cpp").unlink()
        with self.assertRaisesRegex(ValueError, r"missing profiled source: src/host/host\.cpp"):
            self.verify(manifest)

    def test_added_profiled_header_rejected(self):
        manifest = self.manifest()
        self.write("include/cupid/new.hpp", "#pragma once\n")
        with self.assertRaisesRegex(ValueError, r"new profiled source: include/cupid/new\.hpp"):
            self.verify(manifest)

    def test_removed_profiled_header_rejected(self):
        manifest = self.manifest()
        (self.root / "src/rcp/detail.hpp").unlink()
        with self.assertRaisesRegex(ValueError, r"missing profiled source: src/rcp/detail\.hpp"):
            self.verify(manifest)

    def test_current_cmake_membership_mismatch_rejected(self):
        with self.assertRaisesRegex(
                ValueError, "Current CMake core/host source membership does not match the profile"):
            self.verify(compiled_sources=self.expected_compiled[:-1])

    def test_compiler_binary_identity_mismatch_rejected(self):
        actual = copy.deepcopy(self.build)
        actual["compiler"]["sha256"] = "0" * 64
        self.assert_build_change_rejected(actual)

    def test_compiler_version_mismatch_rejected(self):
        actual = copy.deepcopy(self.build)
        actual["compiler"]["version"] = "18.1.1"
        self.assert_build_change_rejected(actual)

    def test_target_triple_mismatch_rejected(self):
        actual = copy.deepcopy(self.build)
        actual["compiler"]["target_triple"] = "aarch64-pc-windows-msvc"
        self.assert_build_change_rejected(actual)

    def test_codegen_flag_mismatch_rejected(self):
        for field, value in (("cxx_flags", "-Wall -fwrapv"),
                             ("release_flags", "-O2 -DNDEBUG"),
                             ("linker_flags", "-fuse-ld=lld"),
                             ("release_linker_flags", "-Wl,--gc-sections")):
            with self.subTest(field=field):
                actual = copy.deepcopy(self.build)
                actual[field] = value
                self.assert_build_change_rejected(actual)

    def test_target_settings_mismatch_rejected(self):
        actual = copy.deepcopy(self.build)
        actual["target_settings"]["cupid_core.COMPILE_DEFINITIONS"] = "43555049445f54455354"
        self.assert_build_change_rejected(actual)

    def test_docs_tests_and_desktop_edits_are_tolerated(self):
        manifest = self.manifest()
        self.write("docs/profile.md", "updated docs\n")
        self.write("tests/test_core.cpp", "// updated test fixture\n")
        self.write("src/desktop/ui.cpp", "int desktop() { return 55; }\n")
        self.write("src/desktop/internal.hpp", "#pragma once\n// desktop-only edit\n")
        self.write("include/cupid/desktop/window.hpp", "#pragma once\n// desktop-only edit\n")
        self.verify(manifest)

    def test_training_snapshot_cannot_be_replaced(self):
        context = self.root / ".work/training/context.json"
        args = SimpleNamespace(
            source_root=self.root,
            compiled_sources=self.compiled_sources_file,
            manifest=context,
        )
        with patch.object(profile, "current_build", return_value=copy.deepcopy(self.build)):
            profile.command_snapshot(args)
        frozen = context.read_bytes()

        self.write("src/core.cpp", "int core() { return 42; }\n")
        with patch.object(profile, "current_build", return_value=copy.deepcopy(self.build)):
            with self.assertRaisesRegex(
                    ValueError, "Training source or settings changed; select a fresh profile directory"):
                profile.command_snapshot(args)
        self.assertEqual(context.read_bytes(), frozen)

    def test_matching_training_context_creates_verifiable_manifest(self):
        context = self.write_training_context()
        with patch.object(profile, "compiler_identity", return_value=copy.deepcopy(self.compiler)):
            manifest = profile.create_manifest(self.create_args(context))

        self.assertEqual(manifest["source"], profile.source_identity(self.root))
        self.assertEqual(manifest["build"], self.build)
        self.assertEqual(
            manifest["provenance"]["training_context_sha256"], profile.file_digest(context))
        self.verify(manifest)

    def test_missing_training_context_rejected(self):
        missing = self.root / ".work/training/missing-context.json"
        with patch.object(profile, "compiler_identity", return_value=copy.deepcopy(self.compiler)):
            with self.assertRaises(FileNotFoundError):
                profile.create_manifest(self.create_args(missing))

    def test_stale_training_source_context_rejected(self):
        context = self.write_training_context()
        self.write("src/storage/save.cpp", "int storage() { return 88; }\n")
        with patch.object(profile, "compiler_identity", return_value=copy.deepcopy(self.compiler)):
            with self.assertRaisesRegex(
                    ValueError, "Profile training source changed; collect a fresh profile"):
                profile.create_manifest(self.create_args(context))

    def test_stale_training_build_context_rejected(self):
        context = self.write_training_context()
        self.write_build_cache(release_flags="-O2 -DNDEBUG")
        with patch.object(profile, "compiler_identity", return_value=copy.deepcopy(self.compiler)):
            with self.assertRaisesRegex(
                    ValueError, "Profile training compiler or build settings changed"):
                profile.create_manifest(self.create_args(context))

    def test_package_manifest_create_does_not_overwrite(self):
        context = self.write_training_context()
        package_manifest = self.root / ".work/package/manifest.json"
        package_manifest.parent.mkdir(parents=True)
        package_manifest.write_text("frozen package manifest\n", encoding="utf-8")
        frozen = package_manifest.read_bytes()
        args = self.create_args(context)
        args.manifest = package_manifest

        with patch.object(profile, "compiler_identity", return_value=copy.deepcopy(self.compiler)):
            with self.assertRaises(FileExistsError):
                profile.command_create(args)
        self.assertEqual(package_manifest.read_bytes(), frozen)

    def test_named_profile_tool_resolves_from_path(self):
        with patch.object(profile.shutil, "which", return_value=str(self.compiler_file)) as lookup:
            self.assertEqual(profile.resolve_tool(Path("llvm-profdata")), self.compiler_file.resolve())
        lookup.assert_called_once_with("llvm-profdata")

    def test_missing_profile_tool_reports_the_requested_name(self):
        with patch.object(profile.shutil, "which", return_value=None):
            with self.assertRaisesRegex(ValueError, "Profile tool was not found: llvm-profdata"):
                profile.resolve_tool(Path("llvm-profdata"))


if __name__ == "__main__":
    unittest.main()
