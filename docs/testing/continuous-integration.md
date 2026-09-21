# Continuous integration and retained results

`tools/ci/validate.py` runs the validation-tool regressions, checks C++ formatting,
builds with strict warnings, and runs every configured CTest entry. Linux jobs
use GCC and Clang; the Windows job uses MSVC. The accuracy job builds the
default and extended cartridges from the pinned test checkout and maintained
[fixture corrections](cartridge-fixtures.md) in separate target directories,
then runs both cartridges in release and sanitizer builds.

The extended cartridge enables `timing`, `cycle`, `cop0hazard`,
`poorly_understood_quirk`, and `experimental_rdp` alongside the default features.
CTest passes `--require-extended-tests` for that image. The runner requires
all five result categories and all four feature flags printed in the summary.
A default-only image, missing quirk results, a truncated summary, or a failed
case makes the run fail. The experimental graphics cases remain in the base
category, using the corrected command packing and CPU oracle described in
[triangle fixtures](rdp-triangle-fixtures.md).

The sanitizer step runs even when the release tests fail, provided cartridge
building and formatter installation succeeded. Both outcomes contribute to the
job result. Existing failures remain failures; the workflow has no expected-
failure list or `continue-on-error` setting.

## Local runs

Run from the emulator checkout, using the same input images and toolchain as
the comparison run:

```sh
python3 tools/ci/validate.py --build-dir build-local --compiler clang++ \
  --clang-format /path/to/clang-format-22.1.0 \
  --test-source .work/nemu64-test \
  --rom /path/to/default.z64 --extended-rom /path/to/extended.z64 \
  --pif /path/to/pif.ntsc.rom
```

For the sanitizer build, add `--sanitizers --config RelWithDebInfo` and choose
a different build directory. Set `ASAN_OPTIONS` and `UBSAN_OPTIONS` to the
values in the workflow when reproducing its runtime checks. A run without
cartridge arguments tests the local regressions only; its report records that
scope explicitly.

On native Windows, use `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1` because
the runtime does not support leak detection. Address and undefined-behavior
checks remain enabled. Keep leak detection enabled for the Linux run.

Each configured validation run creates a separate directory under `BUILD/validation/`.
`--report-dir DIRECTORY` selects another parent directory. The JSON report
records the source revision and working-tree status, optional test-source
revision, host, compiler version, configuration, sanitizer settings, input
sizes and SHA-256 hashes, command lines, and exit codes. A report marked
`running` has no completed result and cannot establish a pass.

Schema 2 reports also identify the Git index and checkout contents with
SHA-256 digests. The content digest includes tracked files, nonignored
untracked files, file modes, regular-file link targets and their contents,
and initialized submodules. A second edit to an already modified file is
detectable even when the revision and Git status text stay the same. Stable
uncommitted changes are allowed and remain visible in the report.

Source, optional test-source, and image checks run again when validation
finishes, including after a failed build or test. Changed or unavailable
source state makes a successful command sequence fail validation. Reports
retain the later source state and integrity errors; an earlier command
failure keeps its original exit code. `sources_verified` and
`inputs_verified` record which checks completed successfully.

Git metadata must be readable from the validation environment. Git-ignored
outputs and the selected build and report directories are excluded from
untracked content. Output directories cannot contain tracked source or be
ancestors of a source checkout. The digests identify content without copying
source, firmware, or cartridges into the report.

These checks compare the initial and final state; they do not lock the
checkout or detect every edit that is reverted during a command. Run release
validation in a dedicated checkout and leave it unchanged until the command
finishes.

Every command has a UTF-8 log containing standard output and diagnostics.
CTest runs verbosely so successful cartridge summaries are retained alongside
failures. The report and logs survive subsequent invocations; CTest's usual
`Testing/Temporary/LastTest.log` alone does not have that property. Input image
files are not copied into the report directory.

## Hosted checks

The workflow retains these report directories as artifacts for 14 days, even
after a validation failure. Missing report files also fail the artifact step.

After building the cartridges, `accuracy-test-inputs` retains the default and
extended ROMs and their ELF executables for 14 days. It includes the test
license, Cargo manifests and lockfile, target/linker configuration, pinned
toolchain selection, and compiler/converter versions. The correction report,
patches, manifest, and four modified Rust files identify the effective source
used to build those images. The explicit artifact
paths exclude PIF firmware. Uploading before validation preserves these inputs
even when a later test fails.

Download the cartridge artifact from the same run as the validation report.
Extraction preserves paths relative to the repository root, including
`.work/nemu64-test/` and `tests/cartridge/fixtures/`. Keep the leading `.work/`
directory when locating the ROMs and preparation report.
Check each ROM's SHA-256 against `inputs` in `report.json` before replaying it.
Use the matching ELF when disassembling a failed test. Rebuilding the same
source revision does not by itself establish identical machine code or data
layout; see [cartridge build layout](cartridge-build-layout.md).

Cartridge checks require the private `N64_PIF_NTSC` repository secret. An
untrusted fork run cannot receive that secret and will report the missing
firmware as a failed accuracy check. A maintainer must run the reviewed source
in a trusted context with the supplied firmware to establish cartridge results.
The independent compiler jobs still exercise the local regressions.

The historical results and fixture definitions are recorded in
[the accuracy baseline](accuracy-baseline.md) and
[the experimental triangle guide](rdp-triangle-fixtures.md). A successful local
regression job alone does not resolve an extended-cartridge failure.
