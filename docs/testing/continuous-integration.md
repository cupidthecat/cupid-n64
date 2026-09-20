# Continuous integration and retained results

`tools/ci/validate.py` runs the validation-tool regressions, checks C++ formatting,
builds with strict warnings, and runs every configured CTest entry. Linux jobs
use GCC and Clang; the Windows job uses MSVC. The accuracy job builds the
default and extended cartridges from the pinned test checkout in separate
target directories, then runs both cartridges in release and sanitizer builds.

The extended cartridge enables `timing`, `cycle`, `cop0hazard`,
`poorly_understood_quirk`, and `experimental_rdp` alongside the default features.
CTest passes `--require-extended-tests` for that image. The runner requires
all five result categories and all four feature flags printed in the summary.
A default-only image, missing quirk results, a truncated summary, or a failed
case makes the run fail. The experimental graphics cases remain in the base
category, with their original assertions.

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

Each configured validation run creates a separate directory under `BUILD/validation/`.
`--report-dir DIRECTORY` selects another parent directory. The JSON report
records the source revision and working-tree status, optional test-source
revision, host, compiler version, configuration, sanitizer settings, input
sizes and SHA-256 hashes, command lines, and exit codes. Input digests are
checked again before reporting success. A report marked `running` has no
completed result and cannot establish a pass.

Every command has a UTF-8 log containing standard output and diagnostics.
CTest runs verbosely so successful cartridge summaries are retained alongside
failures. The report and logs survive subsequent invocations; CTest's usual
`Testing/Temporary/LastTest.log` alone does not have that property. Input image
files are not copied into the report directory.

## Hosted checks

The workflow retains these report directories as artifacts for 14 days, even
after a validation failure. It does not upload the working directory or input
images. Missing report files also fail the artifact step.

Cartridge checks require the private `N64_PIF_NTSC` repository secret. An
untrusted fork run cannot receive that secret and will report the missing
firmware as a failed accuracy check. A maintainer must run the reviewed source
in a trusted context with the supplied firmware to establish cartridge results.
The independent compiler jobs still exercise the local regressions.

The historical results and outstanding fixture discrepancies are recorded in
[the accuracy baseline](accuracy-baseline.md) and
[the experimental triangle audit](rdp-triangle-fixtures.md). A successful local
regression job alone does not resolve an extended-cartridge failure.
