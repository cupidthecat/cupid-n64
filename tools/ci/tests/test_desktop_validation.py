"""Verify that desktop selection is explicit and failed checks propagate."""

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
        self.commands = {}
        self.fail_on = fail_on

    def run(self, name, command, _root):
        self.commands[name] = [str(value) for value in command]
        if name == self.fail_on:
            raise subprocess.CalledProcessError(8, command)

    def add_build(self, _build):
        pass


class DesktopValidationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.args = SimpleNamespace(
            clang_format="clang-format", config="Release", generator="Ninja",
            sanitizers=False, desktop=False, sdl_source=None, compiler="clang++",
            rom=None, pif=None, extended_rom=None, jobs=2,
        )

    def test_headless_selection_overrides_an_existing_desktop_cache(self):
        commands = Commands()
        validate(self.args, self.root, self.root / "build", commands)
        self.assertIn("-DCUPID_DESKTOP=OFF", commands.commands["configure"])
        self.assertFalse(any("SDL3" in value for value in commands.commands["configure"]))

    def test_desktop_and_source_paths_remain_separate_literal_arguments(self):
        self.args.desktop = True
        self.args.sdl_source = self.root / "SDL source with spaces"
        commands = Commands()
        validate(self.args, self.root, self.root / "build", commands)
        self.assertIn("-DCUPID_DESKTOP=ON", commands.commands["configure"])
        self.assertIn(f"-DFETCHCONTENT_SOURCE_DIR_SDL3={self.args.sdl_source.resolve()}",
                      commands.commands["configure"])
        self.assertIn("--no-tests=error", commands.commands["ctest"])
        self.assertNotIn("-R", commands.commands["ctest"])
        self.assertNotIn("-E", commands.commands["ctest"])

    def test_desktop_validation_does_not_hide_ctest_failures(self):
        self.args.desktop = True
        with self.assertRaises(subprocess.CalledProcessError) as raised:
            validate(self.args, self.root, self.root / "build", Commands("ctest"))
        self.assertEqual(raised.exception.returncode, 8)


if __name__ == "__main__":
    unittest.main()
