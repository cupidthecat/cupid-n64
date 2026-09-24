"""Run formatting, a strict build, and CTest before publishing changes."""

import argparse
import os
from pathlib import Path
import subprocess
import sys

from evidence import Evidence


def resolve_formatter_executable(executable, caller_cwd):
    """Keep explicit relative formatter paths anchored to the invoking directory."""
    text = str(executable)
    path = Path(text)
    if path.is_absolute():
        return text
    separators = tuple(separator for separator in (os.sep, os.altsep) if separator)
    if any(separator in text for separator in separators):
        return str((Path(caller_cwd) / path).resolve())
    return text


def main():
    caller_cwd = Path.cwd()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path("build-local"))
    parser.add_argument("--compiler")
    parser.add_argument("--generator", default="Ninja")
    parser.add_argument("--config", default="Release")
    parser.add_argument("--sanitizers", action="store_true")
    parser.add_argument("--desktop", action="store_true", help="Build and test the SDL desktop application")
    parser.add_argument("--sdl-source", type=Path, help="Use an existing SDL 3.4.16 source directory")
    parser.add_argument("--rom", type=Path)
    parser.add_argument("--pif", type=Path)
    parser.add_argument("--extended-rom", type=Path)
    parser.add_argument("--clang-format", default="clang-format")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--report-dir", type=Path)
    parser.add_argument("--test-source", type=Path)
    profiles = parser.add_mutually_exclusive_group()
    profiles.add_argument("--profile-generate", type=Path, help="Collect core/host Clang profile counts")
    profiles.add_argument("--profile-use", type=Path, help="Use a source-bound merged Clang profile")
    parser.add_argument("--profile-manifest", type=Path, help="Manifest for --profile-use")
    args = parser.parse_args()
    if (args.rom or args.extended_rom) and not args.pif:
        parser.error("--rom and --extended-rom require --pif")
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    if bool(args.profile_use) != bool(args.profile_manifest):
        parser.error("--profile-use and --profile-manifest require each other")
    if (args.profile_use or args.profile_generate) and (args.sanitizers or args.config != "Release"):
        parser.error("Profile builds require Release without sanitizers")
    if args.sdl_source and not args.desktop:
        parser.error("--sdl-source requires --desktop")
    if args.sdl_source and not (args.sdl_source / "include/SDL3/SDL_version.h").is_file():
        parser.error("--sdl-source must contain the SDL3 source headers")
    for path in (args.rom, args.pif, args.extended_rom, args.profile_use, args.profile_manifest):
        if path is not None and not path.is_file():
            parser.error(f"File not found: {path}")
    args.clang_format = resolve_formatter_executable(args.clang_format, caller_cwd)

    root = Path(__file__).resolve().parents[2]
    build = args.build_dir.resolve()
    evidence = Evidence(args.report_dir or build / "validation", {
        "build_directory": str(build), "generator": args.generator,
        "configuration": args.config, "requested_compiler": args.compiler,
        "strict": True, "sanitizers": args.sanitizers, "jobs": args.jobs,
        "desktop": args.desktop,
        "asan_options": os.environ.get("ASAN_OPTIONS"),
        "ubsan_options": os.environ.get("UBSAN_OPTIONS"),
        "default_suite": args.rom is not None, "extended_suite": args.extended_rom is not None,
        "profile_mode": "generate" if args.profile_generate else "use" if args.profile_use else "off",
    })
    exit_code = 0
    failure = None
    outputs = (build, evidence.directory.parent)
    try:
        evidence.add_source("source", root, outputs)
        if args.test_source:
            evidence.add_source("test_source", args.test_source.resolve(), outputs)
        if args.sdl_source:
            evidence.add_input("sdl_version", args.sdl_source / "include/SDL3/SDL_version.h")
        for name, path in (("default_rom", args.rom), ("extended_rom", args.extended_rom), ("pif", args.pif)):
            if path is not None:
                evidence.add_input(name, path)
        for name, path in (("profile", args.profile_use), ("profile_manifest", args.profile_manifest)):
            if path is not None:
                evidence.add_input(name, path)
        evidence.run("validation-tests", [sys.executable, "-m", "unittest", "discover", "-s",
                                         root / "tools/ci/tests", "-p", "test_*.py"], root)
        evidence.run("formatter-version", [args.clang_format, "--version"], root)
        evidence.run("cmake-version", ["cmake", "--version"], root)
        validate(args, root, build, evidence)
    except subprocess.CalledProcessError as error:
        exit_code, failure = error.returncode, error
    except (OSError, ValueError, KeyboardInterrupt) as error:
        exit_code, failure = 1, error
    integrity_errors = []
    for verify in (evidence.verify_sources, evidence.verify_inputs):
        try:
            verify()
        except (OSError, ValueError, subprocess.CalledProcessError) as error:
            integrity_errors.append(str(error))
            if exit_code == 0:
                exit_code, failure = 1, error
    if integrity_errors:
        evidence.data["integrity_errors"] = integrity_errors
    evidence.finish(exit_code, failure)
    if failure:
        print(failure, file=sys.stderr)
    return exit_code


def validate(args, root, build, evidence, caller_cwd=None):
    formatter = resolve_formatter_executable(args.clang_format, caller_cwd or Path.cwd())
    sources = sorted(
        path.relative_to(root)
        for folder in ("src", "include", "tests")
        for path in (root / folder).rglob("*")
        if path.suffix in (".cpp", ".hpp")
    )
    evidence.run("format", [formatter, "--dry-run", "--Werror", *sources], root)
    configure = [
        "cmake", "-S", root, "-B", build, "-G", args.generator,
        f"-DCMAKE_BUILD_TYPE={args.config}", "-DCUPID_STRICT=ON",
        f"-DCUPID_SANITIZERS={'ON' if args.sanitizers else 'OFF'}",
        f"-DCUPID_DESKTOP={'ON' if args.desktop else 'OFF'}",
        f"-DCUPID_TEST_ROM={args.rom.resolve() if args.rom else ''}",
        f"-DCUPID_PIF_ROM={args.pif.resolve() if args.pif else ''}",
        f"-DCUPID_EXTENDED_TEST_ROM={args.extended_rom.resolve() if args.extended_rom else ''}",
        f"-DCUPID_PROFILE_GENERATE={args.profile_generate.resolve() if args.profile_generate else ''}",
        f"-DCUPID_PROFILE_USE={args.profile_use.resolve() if args.profile_use else ''}",
        f"-DCUPID_PROFILE_MANIFEST={args.profile_manifest.resolve() if args.profile_manifest else ''}",
    ]
    if args.compiler:
        configure.append(f"-DCMAKE_CXX_COMPILER={args.compiler}")
    if args.sdl_source:
        configure.append(f"-DFETCHCONTENT_SOURCE_DIR_SDL3={args.sdl_source.resolve()}")
    evidence.run("configure", configure, root)
    if args.profile_generate:
        evidence.add_input("profile_context", args.profile_generate.resolve() / "context.json")
    evidence.add_build(build)
    evidence.run("build", ["cmake", "--build", build, "--config", args.config,
                           "--parallel", str(args.jobs)], root)
    evidence.run("ctest", ["ctest", "--test-dir", build, "-C", args.config,
                           "--output-on-failure", "--no-tests=error", "--verbose"], root)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except subprocess.CalledProcessError as error:
        sys.exit(error.returncode)
    except OSError as error:
        print(error, file=sys.stderr)
        sys.exit(1)
