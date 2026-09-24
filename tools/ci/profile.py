"""Create and verify Clang profile-guided optimization packages."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys


SCHEMA = 1
SOURCE_SCHEMA = 1
CODEGEN_SCHEMA = "clang-release-ipo-strict-fp-v1"
STRICT_FP_FLAGS = ["-fno-fast-math", "-frounding-math"]
PROFILED_SOURCE_DIRECTORIES = (
    "cpu", "rcp", "rsp", "rdram", "bus", "cartridge", "rdp", "vi", "tasks", "host", "storage",
)
HEADER_SUFFIXES = {".h", ".hh", ".hpp", ".inc", ".inl"}


def file_digest(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def resolve_tool(path: Path) -> Path:
    resolved = shutil.which(str(path))
    if resolved is None:
        raise ValueError(f"Profile tool was not found: {path}")
    return Path(resolved).resolve(strict=True)


def compiled_sources(root: Path) -> list[str]:
    root = root.resolve()
    sources = [path for path in (root / "src").glob("*.cpp") if path.name != "main.cpp"]
    for directory in PROFILED_SOURCE_DIRECTORIES:
        sources.extend((root / "src" / directory).glob("*.cpp"))
    return sorted(path.relative_to(root).as_posix() for path in sources)


def source_files(root: Path) -> list[str]:
    root = root.resolve()
    files = set(compiled_sources(root))
    include = root / "include"
    desktop_include = include / "cupid" / "desktop"
    for path in include.rglob("*"):
        if path.is_file() and path.suffix.lower() in HEADER_SUFFIXES and desktop_include not in path.parents:
            files.add(path.relative_to(root).as_posix())
    source = root / "src"
    desktop_source = source / "desktop"
    for path in source.rglob("*"):
        if path.is_file() and path.suffix.lower() in HEADER_SUFFIXES and desktop_source not in path.parents:
            files.add(path.relative_to(root).as_posix())
    return sorted(files)


def source_identity(root: Path) -> dict:
    root = root.resolve()
    files = {name: file_digest(root / name) for name in source_files(root)}
    encoded = json.dumps(files, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return {
        "schema": SOURCE_SCHEMA,
        "sha256": hashlib.sha256(encoded).hexdigest(),
        "files": files,
        "compiled_sources": compiled_sources(root),
    }


def read_cache(path: Path) -> dict[str, str]:
    result = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line or line.startswith(("#", "//")) or "=" not in line or ":" not in line.split("=", 1)[0]:
            continue
        key_type, value = line.split("=", 1)
        key, _ = key_type.split(":", 1)
        result[key] = value
    return result


def compiler_identity(compiler: Path) -> dict[str, str]:
    compiler = compiler.resolve(strict=True)
    version_output = subprocess.run(
        [str(compiler), "--version"], check=True, capture_output=True, text=True, encoding="utf-8",
        errors="replace",
    ).stdout
    match = re.search(r"(?:Apple )?clang version ([^\s]+)", version_output)
    if not match or version_output.lstrip().startswith("Apple clang"):
        raise ValueError("Profile packages currently require upstream Clang")
    target = subprocess.run(
        [str(compiler), "--print-target-triple"], check=True, capture_output=True, text=True,
        encoding="utf-8", errors="replace",
    ).stdout.strip()
    if not target:
        raise ValueError("Clang did not report a target triple")
    return {"id": "Clang", "version": match.group(1), "target_triple": target,
            "sha256": file_digest(compiler)}


def build_identity(cache: dict[str, str], compiler: dict[str, str], target_settings=None) -> dict:
    build_type = cache.get("CMAKE_BUILD_TYPE", "")
    ipo = cache.get("CUPID_IPO", "").upper() == "ON"
    sanitizers = cache.get("CUPID_SANITIZERS", "").upper() == "ON"
    if build_type != "Release":
        raise ValueError("Profile packages require a Release training build")
    if not ipo:
        raise ValueError("Profile packages require CUPID_IPO=ON")
    if sanitizers:
        raise ValueError("Profile packages cannot be trained with sanitizers")
    return {
        "schema": CODEGEN_SCHEMA,
        "compiler": compiler,
        "build_type": "Release",
        "ipo": True,
        "sanitizers": False,
        "cxx_standard": 20,
        "cxx_flags": cache.get("CMAKE_CXX_FLAGS", ""),
        "release_flags": cache.get("CMAKE_CXX_FLAGS_RELEASE", ""),
        "linker_flags": cache.get("CMAKE_EXE_LINKER_FLAGS", ""),
        "release_linker_flags": cache.get("CMAKE_EXE_LINKER_FLAGS_RELEASE", ""),
        "strict_fp": STRICT_FP_FLAGS,
        "target_settings": target_settings or {},
    }


def read_compiled_sources(path: Path) -> list[str]:
    return sorted(line.strip().replace("\\", "/") for line in path.read_text(encoding="utf-8").splitlines()
                  if line.strip())


def first_source_difference(expected: dict[str, str], actual: dict[str, str]) -> str:
    for name in sorted(set(expected) | set(actual)):
        if name not in expected:
            return f"new profiled source: {name}"
        if name not in actual:
            return f"missing profiled source: {name}"
        if expected[name] != actual[name]:
            return f"changed profiled source: {name}"
    return "profiled source identity changed"


def verify_package(manifest: dict, root: Path, profile: Path, actual_build: dict,
                   actual_compiled_sources: list[str] | None = None) -> None:
    if not isinstance(manifest, dict) or manifest.get("schema") != SCHEMA:
        raise ValueError("Unsupported profile manifest schema")
    expected_profile = manifest.get("profile", {})
    actual_profile = {"bytes": profile.stat().st_size, "sha256": file_digest(profile)}
    if actual_profile != expected_profile:
        raise ValueError("Profile payload does not match its manifest")
    expected_source = manifest.get("source", {})
    actual_source = source_identity(root)
    if not isinstance(expected_source, dict) or expected_source.get("schema") != SOURCE_SCHEMA:
        raise ValueError("Unsupported profile source schema")
    if expected_source != actual_source:
        detail = first_source_difference(expected_source.get("files", {}), actual_source["files"])
        raise ValueError(f"Profile source is stale: {detail}")
    expected_compiled = expected_source.get("compiled_sources", [])
    if expected_compiled != actual_source["compiled_sources"]:
        raise ValueError("Profile source membership is stale")
    if actual_compiled_sources is not None and expected_compiled != sorted(actual_compiled_sources):
        raise ValueError("Current CMake core/host source membership does not match the profile")
    if manifest.get("build") != actual_build:
        raise ValueError("Profile toolchain or code-generation identity is stale")


def create_manifest(args) -> dict:
    root = args.source_root.resolve()
    profile = args.profile.resolve()
    cache = read_cache(args.build_cache.resolve())
    compiler_path = cache.get("CMAKE_CXX_COMPILER", "")
    if not compiler_path:
        raise ValueError("The training CMake cache does not identify its C++ compiler")
    compiler = compiler_identity(Path(compiler_path))
    context_path = args.training_context
    if context_path is None:
        directory = cache.get("CUPID_PROFILE_GENERATE", "")
        if not directory:
            raise ValueError("The training build has no profile generation context")
        context_path = (root / directory).resolve() / "context.json"
    context = json.loads(context_path.read_text(encoding="utf-8"))
    if not isinstance(context, dict) or context.get("schema") != SCHEMA:
        raise ValueError("Unsupported training context schema")
    source = source_identity(root)
    if context.get("source") != source:
        raise ValueError("Profile training source changed; collect a fresh profile")
    context_build = context.get("build")
    if not isinstance(context_build, dict):
        raise ValueError("Missing training build settings")
    target_settings = context_build.get("target_settings", {})
    build = build_identity(cache, compiler, target_settings)
    if context.get("build") != build:
        raise ValueError("Profile training compiler or build settings changed")
    manifest = {
        "schema": SCHEMA,
        "profile": {"bytes": profile.stat().st_size, "sha256": file_digest(profile)},
        "source": source,
        "build": build,
        "provenance": {"training_context_sha256": file_digest(context_path),
                       "build_cache_sha256": file_digest(args.build_cache)},
    }
    if args.llvm_profdata:
        profdata_tool = resolve_tool(args.llvm_profdata)
        output = subprocess.run(
            [str(profdata_tool), "--version"], check=True, capture_output=True, text=True,
            encoding="utf-8", errors="replace",
        ).stdout
        match = re.search(r"LLVM version ([^\s]+)", output)
        if match is None or match[1] != compiler["version"]:
            raise ValueError("llvm-profdata and the training compiler must have the same LLVM version")
        subprocess.run([str(profdata_tool), "show", str(profile)], check=True,
                       capture_output=True)
        manifest["provenance"]["llvm_profdata_version"] = match[1]
        manifest["provenance"]["llvm_profdata_sha256"] = file_digest(profdata_tool)
    if source_identity(root) != source or file_digest(profile) != manifest["profile"]["sha256"]:
        raise ValueError("Source or profile changed while creating the package")
    return manifest


def command_create(args) -> None:
    manifest = create_manifest(args)
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    with args.manifest.open("x", encoding="utf-8") as stream:
        json.dump(manifest, stream, indent=2, sort_keys=True)
        stream.write("\n")


def current_build(args) -> dict:
    compiler = compiler_identity(args.compiler)
    if compiler["id"] != args.compiler_id or compiler["version"] != args.compiler_version or \
            compiler["target_triple"] != args.target_triple:
        raise ValueError("The current compiler does not match its CMake identity")
    settings = {}
    for item in args.target_setting:
        name, separator, value = item.partition("=")
        if not separator or name in settings:
            raise ValueError("Invalid or repeated target setting")
        settings[name] = value
    return {
        "schema": CODEGEN_SCHEMA,
        "compiler": compiler,
        "build_type": args.build_type,
        "ipo": args.ipo == "ON",
        "sanitizers": args.sanitizers == "ON",
        "cxx_standard": 20,
        "cxx_flags": args.cxx_flags,
        "release_flags": args.release_flags,
        "linker_flags": args.linker_flags,
        "release_linker_flags": args.release_linker_flags,
        "strict_fp": STRICT_FP_FLAGS,
        "target_settings": settings,
    }


def command_verify(args) -> None:
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    verify_package(
        manifest, args.source_root.resolve(), args.profile.resolve(), current_build(args),
        read_compiled_sources(args.compiled_sources),
    )
    print(manifest["profile"]["sha256"])


def command_snapshot(args) -> None:
    source = source_identity(args.source_root)
    if source["compiled_sources"] != read_compiled_sources(args.compiled_sources):
        raise ValueError("CMake and profile tooling disagree about core/host source membership")
    snapshot = {"schema": SCHEMA, "source": source, "build": current_build(args)}
    if source_identity(args.source_root) != source:
        raise ValueError("Source changed while recording the training context")
    if args.manifest.exists():
        if json.loads(args.manifest.read_text(encoding="utf-8")) != snapshot:
            raise ValueError("Training source or settings changed; select a fresh profile directory")
        return
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    with args.manifest.open("x", encoding="utf-8") as stream:
        json.dump(snapshot, stream, sort_keys=True, indent=2)
        stream.write("\n")


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    commands = result.add_subparsers(dest="command", required=True)
    create = commands.add_parser("create", help="Bind a merged Clang profile to its source and training build")
    create.add_argument("--source-root", type=Path, required=True)
    create.add_argument("--profile", type=Path, required=True)
    create.add_argument("--manifest", type=Path, required=True)
    create.add_argument("--build-cache", type=Path, required=True)
    create.add_argument("--training-context", type=Path)
    create.add_argument("--llvm-profdata", type=Path)
    create.set_defaults(function=command_create)
    verify = commands.add_parser("verify", help="Reject a missing or stale profile package")
    verify.add_argument("--profile", type=Path, required=True)
    verify.set_defaults(function=command_verify)
    snapshot = commands.add_parser("snapshot", help="Record source and settings before collecting counts")
    snapshot.set_defaults(function=command_snapshot)
    for command in (verify, snapshot):
        command.add_argument("--source-root", type=Path, required=True)
        command.add_argument("--manifest", type=Path, required=True)
        command.add_argument("--compiled-sources", type=Path, required=True)
        command.add_argument("--compiler", type=Path, required=True)
        command.add_argument("--compiler-id", required=True)
        command.add_argument("--compiler-version", required=True)
        command.add_argument("--target-triple", required=True)
        command.add_argument("--build-type", required=True)
        command.add_argument("--ipo", choices=("ON", "OFF"), required=True)
        command.add_argument("--sanitizers", choices=("ON", "OFF"), required=True)
        command.add_argument("--cxx-flags", default="")
        command.add_argument("--release-flags", default="")
        command.add_argument("--linker-flags", default="")
        command.add_argument("--release-linker-flags", default="")
        command.add_argument("--target-setting", action="append", default=[])
    return result


def main() -> int:
    args = parser().parse_args()
    try:
        args.function(args)
    except (OSError, ValueError, json.JSONDecodeError, subprocess.CalledProcessError) as error:
        print(f"Profile package error: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
