"""Check that validation failures and their evidence remain visible."""

from contextlib import redirect_stdout
import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from evidence import Evidence, input_digest


class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def read_report(self, evidence):
        return json.loads((evidence.directory / "report.json").read_text(encoding="utf-8"))

    def test_input_digest_records_content_without_copying_firmware(self):
        firmware = self.root / "firmware.bin"
        firmware.write_bytes(bytes(range(256)) * 8)
        digest = input_digest(firmware)
        self.assertEqual(digest["bytes"], 2048)
        self.assertEqual(digest["sha256"], hashlib.sha256(firmware.read_bytes()).hexdigest())
        self.assertEqual(set(digest), {"path", "bytes", "sha256"})

    def test_failed_process_preserves_its_exit_code_and_both_output_streams(self):
        evidence = Evidence(self.root / "reports", {"extended_suite": True})
        with redirect_stdout(io.StringIO()):
            with self.assertRaises(subprocess.CalledProcessError) as raised:
                evidence.run("failure", [sys.executable, "-c",
                                        "import sys; print('test output'); print('diagnostic', file=sys.stderr); sys.exit(8)"], self.root)
            evidence.finish(raised.exception.returncode, raised.exception)
        report = self.read_report(evidence)
        self.assertEqual(report["exit_code"], 8)
        self.assertEqual(report["status"], "failed")
        self.assertEqual(report["commands"][0]["exit_code"], 8)
        self.assertEqual(report["commands"][0]["status"], "failed")
        log = (evidence.directory / report["commands"][0]["log"]).read_text(encoding="utf-8")
        self.assertIn("test output", log)
        self.assertIn("diagnostic", log)

    def test_changed_input_cannot_be_verified_by_its_old_digest(self):
        image = self.root / "image.bin"
        image.write_bytes(b"original")
        evidence = Evidence(self.root / "reports", {})
        evidence.add_input("default_rom", image)
        image.write_bytes(b"replaced")
        with self.assertRaisesRegex(ValueError, "default_rom"):
            evidence.verify_inputs()
        report = self.read_report(evidence)
        self.assertNotIn("inputs_verified", report)
        self.assertEqual(report["changed_input"]["name"], "default_rom")
        self.assertNotEqual(report["inputs"]["default_rom"]["sha256"],
                            report["changed_input"]["after"]["sha256"])

    def test_missing_executable_cannot_leave_a_passing_record(self):
        evidence = Evidence(self.root / "reports", {})
        with redirect_stdout(io.StringIO()):
            with self.assertRaises(OSError) as raised:
                evidence.run("missing", [self.root / "missing-executable"], self.root)
            evidence.finish(1, raised.exception)
        report = self.read_report(evidence)
        self.assertEqual(report["status"], "failed")
        self.assertEqual(report["commands"][0]["status"], "failed")
        self.assertNotIn("exit_code", report["commands"][0])

    def test_reruns_have_separate_evidence_and_start_without_success(self):
        first = Evidence(self.root / "reports", {})
        with redirect_stdout(io.StringIO()):
            first.finish(0)
        second = Evidence(self.root / "reports", {"sanitizers": True})
        self.assertNotEqual(first.directory, second.directory)
        self.assertEqual(self.read_report(first)["status"], "passed")
        self.assertEqual(self.read_report(second)["status"], "running")
        self.assertNotIn("exit_code", self.read_report(second))

    def test_successful_command_retains_log_and_build_compiler_version(self):
        evidence = Evidence(self.root / "reports", {})
        compiler = self.root / "build/CMakeFiles/4.2.3/CMakeCXXCompiler.cmake"
        compiler.parent.mkdir(parents=True)
        compiler.write_text('set(CMAKE_CXX_COMPILER "c:/compiler with spaces/clang++")\n'
                            'set(CMAKE_CXX_COMPILER_ID "Clang")\n'
                            'set(CMAKE_CXX_COMPILER_VERSION "22.1.0")\n', encoding="utf-8")
        with redirect_stdout(io.StringIO()):
            evidence.run("success", [sys.executable, "-c", "print('passed')"], self.root)
            evidence.add_build(self.root / "build")
            evidence.finish(0)
        report = self.read_report(evidence)
        self.assertEqual(report["status"], "passed")
        self.assertEqual(report["commands"][0]["exit_code"], 0)
        self.assertEqual(report["compiler"]["CMAKE_CXX_COMPILER_VERSION"], "22.1.0")
        self.assertEqual(report["compiler"]["CMAKE_CXX_COMPILER"], "c:/compiler with spaces/clang++")


if __name__ == "__main__":
    unittest.main()
