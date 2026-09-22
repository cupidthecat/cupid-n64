"""Check that cartridge corrections preserve source provenance and reject unexpected inputs."""

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from cartridge import prepare


def sha(data):
    return hashlib.sha256(data).hexdigest()


class CartridgeCorrectionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="cartridge source ")
        self.directory = Path(self.temporary.name)
        self.root = self.directory / "source with spaces"
        self.root.mkdir()
        self.git("init", "--quiet")
        self.git("config", "user.name", "Validation Fixture")
        self.git("config", "user.email", "validation@example.invalid")
        self.git("config", "core.autocrlf", "false")
        self.git("config", "core.hooksPath", str(self.root / ".git/no-hooks"))
        (self.root / "src").mkdir()
        self.first = self.root / "src/first.rs"
        self.second = self.root / "src/second.rs"
        self.first.write_bytes(b"fn first() { old(); }\n")
        self.second.write_bytes(b"fn second() { old(); }\n")
        self.git("add", ".")
        self.git("commit", "--quiet", "-m", "Create source fixture")
        self.bundle = self.directory / "corrections"
        self.bundle.mkdir()
        self.patch = self.bundle / "fixture.patch"
        self.patch.write_bytes(
            b"diff --git a/src/first.rs b/src/first.rs\n"
            b"--- a/src/first.rs\n+++ b/src/first.rs\n@@ -1 +1 @@\n"
            b"-fn first() { old(); }\n+fn first() { corrected(); }\n"
            b"diff --git a/src/second.rs b/src/second.rs\n"
            b"--- a/src/second.rs\n+++ b/src/second.rs\n@@ -1 +1 @@\n"
            b"-fn second() { old(); }\n+fn second() { corrected(); }\n"
        )
        self.manifest_path = self.bundle / "manifest.json"
        self.manifest = {
            "schema": 1, "revision": self.git("rev-parse", "HEAD"),
            "patches": [{"path": "fixture.patch", "sha256": sha(self.patch.read_bytes())}],
            "files": {
                "src/first.rs": {"before": sha(self.first.read_bytes()), "after": sha(b"fn first() { corrected(); }\n")},
                "src/second.rs": {"before": sha(self.second.read_bytes()), "after": sha(b"fn second() { corrected(); }\n")},
            },
        }
        self.report = self.directory / "reports/prepared.json"
        self.save_manifest()

    def tearDown(self):
        self.temporary.cleanup()

    def git(self, *arguments):
        environment = {name: value for name, value in os.environ.items() if not name.startswith("GIT_")}
        return subprocess.run(
            ["git", "-C", str(self.root), *arguments], check=True, capture_output=True,
            text=True, encoding="utf-8", env=environment,
        ).stdout.strip()

    def save_manifest(self):
        self.manifest_path.write_text(json.dumps(self.manifest), encoding="utf-8")

    def reject_without_changes(self, message):
        before = (self.first.read_bytes(), self.second.read_bytes(), self.git("status", "--porcelain"))
        with self.assertRaisesRegex((ValueError, OSError, subprocess.CalledProcessError), message):
            prepare(self.root, self.manifest_path, self.report)
        self.assertEqual(before, (self.first.read_bytes(), self.second.read_bytes(), self.git("status", "--porcelain")))
        self.assertFalse(self.report.exists())

    def test_prepares_exact_files_and_retains_original_revision_and_index(self):
        record = prepare(self.root, self.manifest_path, self.report)
        self.assertEqual(self.first.read_bytes(), b"fn first() { corrected(); }\n")
        self.assertEqual(self.second.read_bytes(), b"fn second() { corrected(); }\n")
        self.assertEqual(record, json.loads(self.report.read_text(encoding="utf-8")))
        self.assertEqual(record["status"], "prepared")
        self.assertEqual(record["files"], self.manifest["files"])
        self.assertEqual(record["source_before"]["revision"], self.manifest["revision"])
        self.assertEqual(record["source_after"]["revision"], self.manifest["revision"])
        self.assertEqual(record["source_before"]["index_sha256"], record["source_after"]["index_sha256"])
        self.assertNotEqual(record["source_before"]["content_sha256"], record["source_after"]["content_sha256"])
        self.assertEqual(self.git("diff", "--cached", "--name-only"), "")
        self.assertEqual(self.git("show", "HEAD:src/first.rs"), "fn first() { old(); }")

    def test_rejects_a_different_commit_with_identical_files(self):
        self.git("commit", "--quiet", "--allow-empty", "-m", "Advance fixture")
        self.reject_without_changes("source must be at")

    def test_rejects_unrelated_tracked_edits(self):
        self.second.write_bytes(b"fn second() { user_change(); }\n")
        self.reject_without_changes("clean source checkout")

    def test_rejects_untracked_source_files(self):
        (self.root / "src/added.rs").write_bytes(b"fn additional() {}\n")
        self.reject_without_changes("clean source checkout")

    def test_rejects_staged_edits(self):
        self.first.write_bytes(b"fn first() { user_change(); }\n")
        self.git("add", "src/first.rs")
        self.reject_without_changes("clean source checkout")

    def test_rejects_a_changed_patch_before_applying_any_file(self):
        self.patch.write_bytes(self.patch.read_bytes().replace(b"corrected", b"other"))
        self.reject_without_changes("patch digest differs")

    def test_rejects_a_wrong_original_file_digest(self):
        self.manifest["files"]["src/first.rs"]["before"] = "0" * 64
        self.save_manifest()
        self.reject_without_changes("Original cartridge file digest differs")

    def test_prechecks_all_hunks_before_applying_any_file(self):
        self.patch.write_bytes(self.patch.read_bytes().replace(b"-fn second() { old(); }", b"-fn second() { absent(); }"))
        self.manifest["patches"][0]["sha256"] = sha(self.patch.read_bytes())
        self.save_manifest()
        self.reject_without_changes("returned non-zero exit status")

    def test_rejects_patch_paths_not_declared_in_the_manifest(self):
        del self.manifest["files"]["src/second.rs"]
        self.save_manifest()
        self.reject_without_changes("patch paths differ")

    def test_rejects_a_wrong_result_digest_without_a_prepared_report(self):
        self.manifest["files"]["src/first.rs"]["after"] = "0" * 64
        self.save_manifest()
        with self.assertRaisesRegex(ValueError, "Corrected cartridge file digest differs"):
            prepare(self.root, self.manifest_path, self.report)
        self.assertFalse(self.report.exists())

    def test_existing_report_and_corrected_source_cannot_be_reused_as_new_evidence(self):
        prepare(self.root, self.manifest_path, self.report)
        original_report = self.report.read_bytes()
        with self.assertRaisesRegex(FileExistsError, "report already exists"):
            prepare(self.root, self.manifest_path, self.report)
        self.assertEqual(self.report.read_bytes(), original_report)
        self.report = self.directory / "new-report.json"
        self.reject_without_changes("clean source checkout")

    def test_report_cannot_be_written_into_the_cartridge_source(self):
        self.report = self.root / "record.json"
        self.reject_without_changes("report must be outside")

    def test_manifest_cannot_address_files_outside_the_source(self):
        self.manifest["files"]["../outside.rs"] = self.manifest["files"].pop("src/first.rs")
        self.save_manifest()
        self.reject_without_changes("path must be relative")


if __name__ == "__main__":
    unittest.main()
