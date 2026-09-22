"""Retain validation commands, exit codes, logs, and input digests."""

from __future__ import annotations

from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import sys
import uuid

from source import repository_state


def timestamp():
    return datetime.now(timezone.utc).isoformat()


def input_digest(path):
    path = Path(path).resolve()
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return {"path": str(path), "bytes": path.stat().st_size, "sha256": digest.hexdigest()}


class Evidence:
    def __init__(self, directory, configuration):
        self.directory = Path(directory).resolve() / uuid.uuid4().hex
        self.directory.mkdir(parents=True, exist_ok=False)
        self.sources = {}
        self.data = {
            "schema": 2, "started_at": timestamp(), "status": "running",
            "host": platform.platform(), "python": sys.version,
            "configuration": configuration, "inputs": {}, "commands": [],
        }
        self.save()

    def save(self):
        temporary = self.directory / "report.json.tmp"
        temporary.write_text(json.dumps(self.data, indent=2) + "\n", encoding="utf-8")
        os.replace(temporary, self.directory / "report.json")

    def add_input(self, name, path):
        self.data["inputs"][name] = input_digest(path)
        self.save()

    def add_source(self, name, path, output_directories=()):
        self.sources[name] = (Path(path).resolve(), tuple(output_directories))
        try:
            self.data[name] = repository_state(path, output_directories)
        except (OSError, ValueError, subprocess.CalledProcessError) as error:
            self.data[name] = {"unavailable": str(error)}
            raise ValueError(f"Cannot record validation source {name}: {error}") from error
        finally:
            self.save()

    def verify_sources(self):
        self.data["sources_verified"] = False
        checks = {}
        failures = []
        for name, (path, outputs) in self.sources.items():
            try:
                actual = repository_state(path, outputs)
                if actual != self.data[name]:
                    checks[name] = {"status": "changed", "after": actual}
                    failures.append(f"Validation source changed during the run: {name}")
                else:
                    checks[name] = {"status": "unchanged"}
            except (OSError, ValueError, subprocess.CalledProcessError) as error:
                checks[name] = {"status": "unavailable", "error": str(error)}
                failures.append(f"Cannot verify validation source {name}: {error}")
        self.data["source_checks"] = checks
        self.data["sources_verified"] = not failures and bool(self.sources)
        self.save()
        if failures:
            raise ValueError("; ".join(failures))

    def verify_inputs(self):
        self.data.pop("inputs_verified", None)
        for name, expected in self.data["inputs"].items():
            actual = input_digest(expected["path"])
            if actual != expected:
                self.data["changed_input"] = {"name": name, "after": actual}
                self.save()
                raise ValueError(f"Validation input changed during the run: {name}")
        self.data["inputs_verified"] = True
        self.save()

    def run(self, name, command, root):
        command = [str(part) for part in command]
        log_name = f"{len(self.data['commands']) + 1:02d}-{name}.log"
        record = {"name": name, "command": command, "started_at": timestamp(),
                  "log": log_name, "status": "running"}
        self.data["commands"].append(record)
        self.save()
        print("+ " + subprocess.list2cmdline(command), flush=True)
        try:
            with (self.directory / log_name).open("w", encoding="utf-8") as log:
                with subprocess.Popen(
                    command, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                    text=True, encoding="utf-8", errors="replace",
                ) as process:
                    for line in process.stdout:
                        log.write(line)
                        log.flush()
                        print(line, end="", flush=True)
                    code = process.wait()
            record["exit_code"] = code
            if code != 0:
                raise subprocess.CalledProcessError(code, command)
            record["status"] = "passed"
        except BaseException as error:
            record["status"] = "failed"
            record["error"] = str(error)
            raise
        finally:
            record["finished_at"] = timestamp()
            self.save()

    def add_build(self, build):
        build = Path(build)
        compiler_files = sorted(build.glob("CMakeFiles/*/CMakeCXXCompiler.cmake"),
                                key=lambda path: path.stat().st_mtime_ns)
        if compiler_files:
            content = compiler_files[-1].read_text(encoding="utf-8", errors="replace")
            compiler = {}
            for name in ("CMAKE_CXX_COMPILER", "CMAKE_CXX_COMPILER_ID", "CMAKE_CXX_COMPILER_VERSION"):
                match = re.search(r'set\(' + name + r' "([^"\n]*)"\)', content)
                if match:
                    compiler[name] = match.group(1)
            self.data["compiler"] = compiler
        self.save()

    def finish(self, exit_code, error=None):
        self.data["status"] = "passed" if exit_code == 0 else "failed"
        self.data["exit_code"] = exit_code
        self.data["finished_at"] = timestamp()
        if error:
            self.data["error"] = str(error)
        self.save()
        print(f"Validation report: {self.directory / 'report.json'}", flush=True)
