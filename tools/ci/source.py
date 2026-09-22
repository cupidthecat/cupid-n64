"""Identify the checkout contents used by a validation run."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess


def repository_state(path, output_directories=()):
    root = Path(path).resolve()
    environment = os.environ.copy()
    for name in ("GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE", "GIT_COMMON_DIR",
                 "GIT_OBJECT_DIRECTORY", "GIT_ALTERNATE_OBJECT_DIRECTORIES", "GIT_NAMESPACE"):
        environment.pop(name, None)
    environment["GIT_OPTIONAL_LOCKS"] = "0"

    def git(*arguments):
        return subprocess.run(
            ["git", "-C", str(root), *arguments], check=True,
            capture_output=True, env=environment,
        ).stdout

    def metadata():
        return (git("rev-parse", "HEAD").strip(), git("ls-files", "--stage", "-z"),
                git("ls-files", "--others", "--exclude-standard", "-z"),
                git("status", "--porcelain", "--untracked-files=no"))

    top = Path(os.fsdecode(git("rev-parse", "--show-toplevel").strip())).resolve()
    if top != root:
        raise ValueError(f"Source directory is not a repository root: {root}")
    excluded = tuple(Path(directory).resolve() for directory in output_directories)
    if any(directory == root or directory in root.parents for directory in excluded):
        raise ValueError(f"Validation output directory overlaps source root: {root}")
    before = metadata()
    revision, index, untracked, changes = before
    files = []
    for entry in index.split(b"\0"):
        if not entry:
            continue
        attributes, separator, raw_name = entry.partition(b"\t")
        mode, _, stage = attributes.split()
        if not separator or stage != b"0":
            raise ValueError("Source index contains an unresolved merge")
        name = os.fsdecode(raw_name)
        candidate = root / name
        if any(candidate == directory or directory in candidate.parents for directory in excluded):
            raise ValueError(f"Validation output directory contains tracked source: {candidate}")
        if mode == b"160000":
            content = {"submodule": repository_state(candidate, output_directories)}
        else:
            content = file_state(candidate)
        files.append({"path": name, "mode": mode.decode("ascii"), **content})

    extra_names = []
    for raw_name in untracked.split(b"\0"):
        if not raw_name:
            continue
        name = os.fsdecode(raw_name)
        candidate = root / name
        if any(candidate == directory or directory in candidate.parents for directory in excluded):
            continue
        extra_names.append(name)
        files.append({"path": name, "mode": "untracked", **file_state(candidate)})

    after = metadata()

    def relevant_untracked(raw):
        return tuple(name for name in raw.split(b"\0") if name and not any(
            root / os.fsdecode(name) == directory or directory in (root / os.fsdecode(name)).parents
            for directory in excluded))

    if before[:2] != after[:2] or before[3] != after[3] or relevant_untracked(before[2]) != relevant_untracked(after[2]):
        raise ValueError(f"Source changed while recording its contents: {root}")
    contents = json.dumps(sorted(files, key=lambda item: item["path"]),
                          sort_keys=True, separators=(",", ":")).encode("utf-8")
    status = changes.decode("utf-8", errors="replace").strip()
    status = "\n".join(filter(None, [status, *(f"?? {name}" for name in extra_names)]))
    return {"path": str(root), "revision": revision.decode("ascii"), "changes": status,
            "index_sha256": hashlib.sha256(index).hexdigest(),
            "content_sha256": hashlib.sha256(contents).hexdigest(), "files": len(files)}


def file_state(path):
    try:
        before = path.lstat()
    except FileNotFoundError:
        return {"kind": "missing"}
    if stat.S_ISLNK(before.st_mode):
        target = os.readlink(path)
        if not path.is_file():
            raise ValueError(f"Cannot verify source link target: {path}")
        contents = regular_file_state(path)
        after = path.lstat()
        if (before.st_ino, before.st_mtime_ns) != (after.st_ino, after.st_mtime_ns) or target != os.readlink(path):
            raise ValueError(f"Source link changed while reading: {path}")
        return {"kind": "link", "target": target, "contents": contents}
    if not stat.S_ISREG(before.st_mode):
        raise ValueError(f"Cannot verify source file type: {path}")
    return {"kind": "file", **regular_file_state(path)}


def regular_file_state(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        before = os.fstat(source.fileno())
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
        after = os.fstat(source.fileno())
    current = path.stat()

    def identity(value):
        return (value.st_dev, value.st_ino, value.st_size, value.st_mtime_ns)

    if identity(before) != identity(after) or identity(after) != identity(current):
        raise ValueError(f"Source changed while reading: {path}")
    return {"bytes": current.st_size, "sha256": digest.hexdigest(),
            "executable": bool(current.st_mode & 0o111)}
