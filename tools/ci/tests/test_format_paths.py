"""Check formatter path anchoring and source-relative validation arguments."""

from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from validate import validate


class Commands:
    def __init__(self, fail_on=None):
        self.calls = []
        self.fail_on = fail_on

    def run(self, name, command, root):
        command = [str(value) for value in command]
        self.calls.append((name, command, Path(root)))
        if name == self.fail_on:
            raise subprocess.CalledProcessError(8, command)

    def add_build(self, _build):
        pass


class FormatPathTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="format path fixture ")
        self.addCleanup(self.temporary.cleanup)
        base = Path(self.temporary.name)
        self.root = base / "source root with spaces"
        self.caller = base / "caller cwd with spaces"
        self.root.mkdir()
        self.caller.mkdir()
        for folder in ("src", "include", "tests"):
            (self.root / folder / "nested").mkdir(parents=True)
        self.formatter = self.caller / "formatter tools" / "clang-format.exe"
        self.args = SimpleNamespace(
            clang_format=str(Path("formatter tools") / "clang-format.exe"),
            config="Release", generator="Ninja", sanitizers=False, desktop=False,
            sdl_source=None, compiler="clang++", rom=None, pif=None, extended_rom=None, jobs=2,
            profile_generate=None, profile_use=None, profile_manifest=None,
        )

    def populate_sources(self):
        expected = []
        for folder in ("src", "include", "tests"):
            suffix = ".hpp" if folder == "include" else ".cpp"
            for index in range(112):
                relative = Path(folder) / "nested" / f"source_{index:03d}{suffix}"
                (self.root / relative).write_text("// fixture\n", encoding="utf-8")
                expected.append(str(relative))
        (self.root / "src/nested/ignored.txt").write_text("ignored\n", encoding="utf-8")
        return sorted(expected)

    def test_format_uses_source_root_cwd_relative_files_and_caller_relative_executable(self):
        expected = self.populate_sources()
        commands = Commands()

        validate(self.args, self.root, self.root / "build", commands, caller_cwd=self.caller)

        format_calls = [call for call in commands.calls if call[0] == "format"]
        self.assertEqual(len(format_calls), 1)
        _, command, cwd = format_calls[0]
        self.assertEqual(cwd, self.root)
        self.assertEqual(command[0], str(self.formatter.resolve()))
        self.assertEqual(command[1:3], ["--dry-run", "--Werror"])
        self.assertEqual(sorted(command[3:]), expected)
        self.assertEqual(len(command[3:]), 336)
        self.assertTrue(all(not Path(path).is_absolute() for path in command[3:]))
        self.assertNotEqual(self.caller, self.root)

    def test_format_failure_propagates_without_skipping_to_configure(self):
        self.populate_sources()
        commands = Commands("format")

        with self.assertRaises(subprocess.CalledProcessError) as raised:
            validate(self.args, self.root, self.root / "build", commands, caller_cwd=self.caller)

        self.assertEqual(raised.exception.returncode, 8)
        self.assertEqual([name for name, _, _ in commands.calls], ["format"])


if __name__ == "__main__":
    unittest.main()
