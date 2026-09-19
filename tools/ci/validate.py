"""Run formatting, a strict build, and CTest before publishing changes."""

import argparse
from pathlib import Path
import subprocess
import sys


def run(command, root):
    print("+ " + subprocess.list2cmdline([str(part) for part in command]), flush=True)
    subprocess.run(command, cwd=root, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path("build-local"))
    parser.add_argument("--compiler")
    parser.add_argument("--generator", default="Ninja")
    parser.add_argument("--config", default="Release")
    parser.add_argument("--sanitizers", action="store_true")
    parser.add_argument("--rom", type=Path)
    parser.add_argument("--pif", type=Path)
    parser.add_argument("--extended-rom", type=Path)
    parser.add_argument("--clang-format", default="clang-format")
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    if (args.rom or args.extended_rom) and not args.pif:
        parser.error("--rom and --extended-rom require --pif")
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    for path in (args.rom, args.pif, args.extended_rom):
        if path is not None and not path.is_file():
            parser.error(f"File not found: {path}")

    root = Path(__file__).resolve().parents[2]
    build = args.build_dir.resolve()
    sources = sorted(
        path
        for folder in ("src", "include", "tests")
        for path in (root / folder).rglob("*")
        if path.suffix in (".cpp", ".hpp")
    )
    run([args.clang_format, "--dry-run", "--Werror", *sources], root)
    configure = [
        "cmake", "-S", root, "-B", build, "-G", args.generator,
        f"-DCMAKE_BUILD_TYPE={args.config}", "-DCUPID_STRICT=ON",
        f"-DCUPID_SANITIZERS={'ON' if args.sanitizers else 'OFF'}",
        f"-DCUPID_TEST_ROM={args.rom.resolve() if args.rom else ''}",
        f"-DCUPID_PIF_ROM={args.pif.resolve() if args.pif else ''}",
        f"-DCUPID_EXTENDED_TEST_ROM={args.extended_rom.resolve() if args.extended_rom else ''}",
    ]
    if args.compiler:
        configure.append(f"-DCMAKE_CXX_COMPILER={args.compiler}")
    run(configure, root)
    run(["cmake", "--build", build, "--config", args.config, "--parallel", str(args.jobs)], root)
    run(["ctest", "--test-dir", build, "-C", args.config, "--output-on-failure"], root)


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        sys.exit(error.returncode)
    except OSError as error:
        print(error, file=sys.stderr)
        sys.exit(1)
