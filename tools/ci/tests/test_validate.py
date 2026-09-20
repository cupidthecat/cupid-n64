"""Exercise source integrity at the validation entry point."""

from contextlib import redirect_stderr, redirect_stdout
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import validate


class ValidationSourceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name) / "checkout"
        self.root.mkdir()
        self.git("init", "--quiet")
        self.git("config", "user.name", "Validation Fixture")
        self.git("config", "user.email", "validation@example.invalid")
        self.git("config", "core.hooksPath", str(self.root / ".git/no-hooks"))
        (self.root / ".gitignore").write_text(".work/\nbuild*/\n", encoding="utf-8")
        (self.root / "src").mkdir()
        self.source = self.root / "src/example.cpp"
        self.source.write_text("int value = 1;\n", encoding="utf-8")
        self.git("add", ".")
        self.git("commit", "--quiet", "-m", "Create source fixture")

    def tearDown(self):
        self.temporary.cleanup()

    def git(self, *arguments):
        environment = os.environ.copy()
        for name in tuple(environment):
            if name.startswith("GIT_"):
                del environment[name]
        return subprocess.run(
            ["git", "-C", str(self.root), *arguments], check=True,
            capture_output=True, text=True, encoding="utf-8", env=environment,
        ).stdout.strip()

    def run_validation(self, during_run, *extra_arguments):
        reports = self.root / ".work/reports"
        if "--report-dir" in extra_arguments:
            reports = Path(extra_arguments[extra_arguments.index("--report-dir") + 1])
        arguments = ["validate.py", "--build-dir", str(self.root / "build"),
                     "--report-dir", str(reports), *extra_arguments]
        with patch.object(validate, "__file__", str(self.root / "tools/ci/validate.py")), \
                patch.object(sys, "argv", arguments), \
                patch.object(validate.Evidence, "run"), \
                patch.object(validate, "validate", side_effect=lambda *args: during_run()), \
                redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            result = validate.main()
        report_files = list(reports.glob("*/report.json"))
        self.assertEqual(len(report_files), 1)
        report = json.loads(report_files[0].read_text(encoding="utf-8"))
        self.assertEqual(report["exit_code"], result)
        return result, report

    def assert_rejected(self, during_run, *extra_arguments):
        result, report = self.run_validation(during_run, *extra_arguments)
        self.assertNotEqual(result, 0)
        self.assertEqual(report["status"], "failed")
        self.assertIn("source", report["error"].lower())
        return report

    def test_tracked_edit_during_validation_rejects_success(self):
        self.assert_rejected(lambda: self.source.write_text("int value = 2;\n", encoding="utf-8"))

    def test_second_edit_with_identical_dirty_status_rejects_success(self):
        self.source.write_text("int value = 2;\n", encoding="utf-8")
        before = self.git("status", "--porcelain")
        self.assert_rejected(lambda: self.source.write_text("int value = 3;\n", encoding="utf-8"))
        self.assertEqual(self.git("status", "--porcelain"), before)

    def test_staged_replacement_with_identical_status_rejects_success(self):
        self.source.write_text("int value = 2;\n", encoding="utf-8")
        self.git("add", "src/example.cpp")
        before = self.git("status", "--porcelain")

        def replace_staged_source():
            self.source.write_text("int value = 3;\n", encoding="utf-8")
            self.git("add", "src/example.cpp")

        self.assert_rejected(replace_staged_source)
        self.assertEqual(self.git("status", "--porcelain"), before)

    def test_changed_revision_with_identical_contents_rejects_success(self):
        self.assert_rejected(lambda: self.git("commit", "--quiet", "--allow-empty", "-m", "Advance fixture"))

    def test_index_change_with_unchanged_working_contents_rejects_success(self):
        self.source.write_text("int value = 2;\n", encoding="utf-8")
        self.git("add", "src/example.cpp")
        self.source.write_text("int value = 3;\n", encoding="utf-8")
        before = self.git("status", "--porcelain")

        def change_only_index():
            self.source.write_text("int value = 4;\n", encoding="utf-8")
            self.git("add", "src/example.cpp")
            self.source.write_text("int value = 3;\n", encoding="utf-8")

        report = self.assert_rejected(change_only_index)
        self.assertEqual(self.git("status", "--porcelain"), before)
        self.assertEqual(report["source"]["content_sha256"],
                         report["source_checks"]["source"]["after"]["content_sha256"])
        self.assertNotEqual(report["source"]["index_sha256"],
                            report["source_checks"]["source"]["after"]["index_sha256"])

    def test_existing_untracked_file_edit_rejects_success(self):
        extra = self.root / "src/new.cpp"
        extra.write_text("int added = 1;\n", encoding="utf-8")
        self.assert_rejected(lambda: extra.write_text("int added = 2;\n", encoding="utf-8"))

    def test_new_file_in_existing_untracked_directory_rejects_success(self):
        extra = self.root / "src/new"
        extra.mkdir()
        (extra / "first.cpp").write_text("int first;\n", encoding="utf-8")
        before = self.git("status", "--porcelain", "--untracked-files=normal")
        self.assert_rejected(lambda: (extra / "second.cpp").write_text("int second;\n", encoding="utf-8"))
        self.assertEqual(self.git("status", "--porcelain", "--untracked-files=normal"), before)

    def test_removed_source_rejects_success(self):
        self.assert_rejected(self.source.unlink)

    def test_missing_git_metadata_rejects_success(self):
        (self.root / ".git").rename(self.root / ".work-git")
        self.assert_rejected(lambda: None)

    def test_lost_git_metadata_during_run_rejects_success(self):
        report = self.assert_rejected(lambda: (self.root / ".git").rename(self.root / ".work-git"))
        self.assertFalse(report["sources_verified"])
        self.assertEqual(report["source_checks"]["source"]["status"], "unavailable")

    def test_test_source_changes_are_checked_independently(self):
        test_root = self.root / ".work/test-source"
        test_root.parent.mkdir()
        self.git("clone", "--quiet", str(self.root), str(test_root))
        report = self.assert_rejected(
            lambda: (test_root / "src/example.cpp").write_text("int value = 7;\n", encoding="utf-8"),
            "--test-source", str(test_root))
        self.assertEqual(report["source_checks"]["source"]["status"], "unchanged")
        self.assertEqual(report["source_checks"]["test_source"]["status"], "changed")
        self.assertIn("test_source", report["error"])

    def test_failed_command_keeps_exit_code_and_source_diagnostics(self):
        def fail_and_change_source():
            self.source.write_text("int value = 5;\n", encoding="utf-8")
            raise subprocess.CalledProcessError(8, ["ctest"])

        result, report = self.run_validation(fail_and_change_source)
        self.assertEqual(result, 8)
        self.assertIn("ctest", report["error"])
        self.assertFalse(report["sources_verified"])
        self.assertEqual(report["source_checks"]["source"]["status"], "changed")
        self.assertTrue(any("source" in error for error in report["integrity_errors"]))

    def test_stable_preexisting_edits_are_validated(self):
        self.source.write_text("int value = 2;\n", encoding="utf-8")
        result, report = self.run_validation(lambda: None)
        self.assertEqual(result, 0)
        self.assertEqual(report["status"], "passed")
        self.assertTrue(report["source"]["changes"])
        self.assertTrue(report["sources_verified"])

    def test_stable_staged_edits_are_validated(self):
        self.source.write_text("int value = 2;\n", encoding="utf-8")
        self.git("add", "src/example.cpp")
        result, report = self.run_validation(lambda: None)
        self.assertEqual(result, 0)
        self.assertTrue(report["sources_verified"])

    def test_custom_output_directories_are_excluded(self):
        output = self.root / "compile-output"
        reports = self.root / "validation-output"

        def write_outputs():
            output.mkdir()
            (output / "test-results.txt").write_text("passed", encoding="utf-8")

        result, report = self.run_validation(write_outputs, "--build-dir", str(output),
                                             "--report-dir", str(reports))
        self.assertEqual(result, 0)
        self.assertTrue(report["sources_verified"])

    def test_build_directory_cannot_hide_source_changes(self):
        self.assert_rejected(
            lambda: (self.root / "src/new.cpp").write_text("int added;\n", encoding="utf-8"),
            "--build-dir", str(self.root / "src"))

    def test_generated_ignored_outputs_do_not_change_source(self):
        def write_outputs():
            output = self.root / "build/result.log"
            output.parent.mkdir(exist_ok=True)
            output.write_text("test output\n", encoding="utf-8")

        result, report = self.run_validation(write_outputs)
        self.assertEqual(result, 0)
        self.assertEqual(report["status"], "passed")


if __name__ == "__main__":
    unittest.main()
