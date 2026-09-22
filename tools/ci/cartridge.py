"""Apply reviewed cartridge-fixture corrections and record their exact source inputs."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import subprocess
import sys

from source import repository_state


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def git(root: Path, *arguments: str) -> bytes:
    environment = os.environ.copy()
    for name in tuple(environment):
        if name.startswith("GIT_"):
            del environment[name]
    return subprocess.run(
        ["git", "-C", str(root), *arguments], check=True, capture_output=True, env=environment,
    ).stdout


def relative_file(root: Path, name: str) -> Path:
    relative = PurePosixPath(name)
    if not name or relative.is_absolute() or ".." in relative.parts or "\\" in name or ":" in name:
        raise ValueError(f"Correction path must be relative: {name}")
    candidate = root.joinpath(*relative.parts)
    if candidate.is_symlink() or root not in candidate.resolve().parents or not candidate.is_file():
        raise ValueError(f"Correction file must exist inside its source directory: {name}")
    return candidate


def prepare(source: Path, manifest_path: Path, report_path: Path) -> dict:
    source = source.resolve()
    manifest_path = manifest_path.resolve()
    report_path = report_path.resolve()
    if report_path.exists():
        raise FileExistsError(f"Correction report already exists: {report_path}")
    if report_path == source or source in report_path.parents:
        raise ValueError("The correction report must be outside the cartridge source directory")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("schema") != 1 or not manifest.get("files") or not manifest.get("patches"):
        raise ValueError("Invalid cartridge correction manifest")
    before = repository_state(source)
    if before["revision"] != manifest["revision"]:
        raise ValueError(f"Cartridge source must be at {manifest['revision']}")
    if before["changes"]:
        raise ValueError("Cartridge corrections require a clean source checkout")

    files = {}
    for name, expected in manifest["files"].items():
        path = relative_file(source, name)
        actual = digest(path)
        if actual != expected["before"]:
            raise ValueError(f"Original cartridge file digest differs: {name}")
        files[name] = path
    patches = []
    patch_records = []
    for entry in manifest["patches"]:
        path = relative_file(manifest_path.parent, entry["path"])
        actual = digest(path)
        if actual != entry["sha256"]:
            raise ValueError(f"Cartridge patch digest differs: {entry['path']}")
        patches.append(str(path))
        patch_records.append({"path": entry["path"], "sha256": actual})
    names = set()
    for entry in git(source, "apply", "--numstat", "-z", *patches).split(b"\0"):
        if entry:
            columns = entry.split(b"\t", 2)
            if len(columns) != 3 or not columns[2]:
                raise ValueError("Cartridge patches must modify existing files without renaming")
            names.add(columns[2].decode("utf-8"))
    if names != set(files):
        raise ValueError("Cartridge patch paths differ from the correction manifest")
    git(source, "apply", "--check", *patches)
    if repository_state(source) != before:
        raise ValueError("Cartridge source changed while checking its corrections")
    git(source, "apply", *patches)

    after = repository_state(source)
    if after["revision"] != before["revision"] or after["index_sha256"] != before["index_sha256"]:
        raise ValueError("Cartridge correction changed the source revision or index")
    changed = {os.fsdecode(name) for name in git(source, "diff", "--name-only", "-z").split(b"\0") if name}
    if changed != set(files):
        raise ValueError("Corrected cartridge paths differ from the correction manifest")
    verified_files = {}
    for name, path in files.items():
        actual = digest(path)
        if actual != manifest["files"][name]["after"]:
            raise ValueError(f"Corrected cartridge file digest differs: {name}")
        verified_files[name] = {"before": manifest["files"][name]["before"], "after": actual}
    record = {
        "schema": 1, "status": "prepared", "manifest_sha256": digest(manifest_path),
        "source_before": before, "source_after": after, "patches": patch_records, "files": verified_files,
    }
    report_path.parent.mkdir(parents=True, exist_ok=True)
    with report_path.open("x", encoding="utf-8", newline="\n") as output:
        json.dump(record, output, indent=2)
        output.write("\n")
    return record


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--report", type=Path, required=True)
    arguments = parser.parse_args()
    manifest = Path(__file__).resolve().parents[2] / "tests/cartridge/fixtures/manifest.json"
    try:
        record = prepare(arguments.source, manifest, arguments.report)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f"Cartridge correction failed: {error}", file=sys.stderr)
        if isinstance(error, subprocess.CalledProcessError) and error.stderr:
            print(error.stderr.decode("utf-8", errors="replace"), file=sys.stderr)
        return 1
    print(f"Prepared {len(record['files'])} corrected fixture files at {record['source_before']['revision']}")
    print(f"Correction report: {arguments.report}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
