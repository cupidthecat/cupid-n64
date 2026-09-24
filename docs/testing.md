# Hardware validation

The local regressions and cartridge tests serve different purposes. Local tests isolate a hardware rule and check the result directly. The cartridge suite also exercises instruction sequencing, exception handlers, memory initialization, cache maintenance, DMA, and communication between components.

The [recorded accuracy baseline](testing/accuracy-baseline.md) pins an emulator
revision, input hashes, toolchains, category results, and the remaining timing
failures. Use it when comparing accuracy changes; a passing default suite does
not cover the optional groups.

## Local validation before publishing

Run the same validation entry point used by CI before committing or pushing:

```sh
python tools/ci/validate.py --compiler clang++ \
  --rom /absolute/path/to/n64-systemtest.z64 \
  --pif /absolute/path/to/pif.ntsc.rom
```

The script checks all C++ files with `clang-format --dry-run --Werror`, configures
a strict build, compiles it, and runs CTest. CI installs clang-format 22.1.0;
use the same version locally. `--clang-format` accepts a formatter path.
The formatter runs from the source root with relative source filenames, reducing
the command length in nested Windows checkouts. An explicit relative formatter
path is resolved from the directory where the validator was invoked; a bare
executable name is found through `PATH`. Formatting failures stop validation.

Use `--build-dir` for separate compiler configurations, `--jobs` to limit parallel
compilation, and `--sanitizers --config RelWithDebInfo` for Clang sanitizer checks.
On Windows, `--generator "Visual Studio 17 2022"` selects MSVC when that version
of Visual Studio is installed. Choose the generator matching your installation.

Without `--rom`, the script runs only local regressions. Add `--extended-rom`
to run the optional cartridge groups as well. Both ROM options require `--pif`.
A failing optional group makes validation fail; the script does not suppress it.

## Test input

The CI workflow pins `thelemmy/nemu64-test` to commit `9a8b9f7d94ee2f6f57d7feed70c98c22cdc30e6c`. Before compiling, it applies the [cartridge fixture corrections](testing/cartridge-fixtures.md) for color packing, coverage, and clock sampling. The preparation report records the base revision and exact source changes. That checkout selects Rust `nightly-2026-07-16` through its toolchain file. `nust64` version `0.4.1` converts its ELF executable into a cartridge image.

Prepare a fresh checkout with LF line endings from the Cupid-N64 root:

```sh
git clone --no-checkout -c core.autocrlf=false \
  https://github.com/thelemmy/nemu64-test .work/nemu64-test
git -C .work/nemu64-test checkout --detach 9a8b9f7d94ee2f6f57d7feed70c98c22cdc30e6c
python tools/ci/cartridge.py .work/nemu64-test \
  --report .work/test-fixture-corrections.json
```

From the test checkout:

```sh
cargo build --release --locked --target-dir target-default
cargo +stable install nust64 --version 0.4.1 --locked
nust64 --elf target-default/mips-nintendo64-none/release/n64-systemtest
```

The generated ROM is `target-default/mips-nintendo64-none/release/n64-systemtest.z64`. Keep an unmodified checkout and the original ROM when investigating a failure from an earlier run. Results from corrected fixtures must include their correction manifest alongside the base revision and ROM hash.

The default build enables the base group. A separate build can include the upstream timing, cycle, CPU-hazard, partially characterized hardware, and experimental rendering tests:

```sh
cargo build --release --locked --target-dir target-extended \
  --features timing,cycle,cop0hazard,poorly_understood_quirk,experimental_rdp
nust64 --elf target-extended/mips-nintendo64-none/release/n64-systemtest
```

Record the ROM revision, correction manifest, and enabled features with results. Pass `--test-source .work/nemu64-test` to the validator so it verifies the prepared source throughout the run. A successful default run does not establish that the optional groups passed.

The [validation results](testing/validation-results.md) record the tested inputs,
platforms, and remaining failures. The [triangle fixture audit](testing/rdp-triangle-fixtures.md)
describes the command-packing and coverage disagreements. These tests remain
enabled, and an unresolved failure makes the extended validation command fail.

Clang Release builds can collect and use execution profiles for the core and host
libraries. The [profile build guide](testing/profile-guided-builds.md) covers
training, source/compiler compatibility, validation, and performance comparisons.
Keep sanitizer runs in a separate ordinary build.

Rebuild both images after updating the test checkout. A ROM left in an older
output directory can contain different assertions even when the checkout is
clean. Use a fresh `--target-dir` when checking a ROM's source revision.

On Windows, keep the rustup proxies ahead of other Rust installations in the
build shell's `PATH`. Cargo and rustc must use the same pinned toolchain. A
standalone Cargo installation can ignore the toolchain file or invoke a rustc
that cannot compile the selected standard library sources.

## Sanitizers

Clang builds support address and undefined-behavior sanitizers:

```sh
cmake -S . -B build-sanitize -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCUPID_SANITIZERS=ON -DCUPID_STRICT=ON \
  -DCUPID_TEST_ROM=/absolute/path/to/n64-systemtest.z64 \
  -DCUPID_PIF_ROM=/absolute/path/to/pif.ntsc.rom
cmake --build build-sanitize
ctest --test-dir build-sanitize --output-on-failure
```

On Windows, the Clang configuration copies the compiler's address-sanitizer runtime beside the executables. The MSVC sanitizer option enables its address sanitizer.

## Parallel range tasks

The RDP row path and parallel VI scanout share `src/tasks/parallel_ranges.cpp`.
Each calling host thread owns a thread-local worker group that is created on
first use and reused by later jobs. If no worker thread can be created, the
caller executes the range directly. Nested range work from an existing task
runs inline with that task's index, while independent calling threads keep
separate worker groups. Destroying the calling thread signals its workers,
wakes them, and joins them before the thread-local group is released.

A submitted job publishes its range and callback before waking workers, and a
new job is not published until every range from the previous job has finished.
On SSE2 targets, workers briefly spin with a processor pause instruction before
blocking when a job or completion count has not changed. The spin has a fixed
limit; longer gaps use the atomic wait. Both paths acquire the same published
job state and wait for every worker before allowing the next submission.
If the caller range or a worker range throws, the dispatcher still waits for all
other ranges before propagating the exception. The worker group remains usable
for a later job after that propagation. `tests/tasks/test_parallel_ranges.cpp`
checks complete non-overlapping range coverage, boundary arithmetic, repeated
worker reuse, finish-before-rethrow behavior, nested calls, and concurrent
callers.

## Interpreting failures

The cartridge runner prints the ROM's output unchanged. It checks the aggregate counts, requires a result for each enabled category, and waits for the final summary line. Truncated output, malformed counts, duplicate categories, an empty test run, and a reported panic all fail validation.

The ROM formats integer test parameters in hexadecimal. For example, the `24`
in `(true, 24, 36.3)` means decimal 36; the floating-point value stays decimal.
Account for that formatting when comparing a failure with its source parameters.

The instruction limit bounds a run that stalls inside guest software. A limit failure includes the PC, pending next PC, exception state, registers, and the last 32 instruction addresses. Use those addresses with the matching test ELF when examining a failing sequence.

A passing regression should demonstrate the affected behavior, including observable results and side effects. For a DMA change, this can include transferred bytes, untouched memory, address postincrements, busy state, and interrupt delivery. For an instruction change, this can include the destination register, flags, exception state, delay-slot behavior, and register aliases.

## CI boot firmware

The accuracy job reads an NTSC boot ROM from the `N64_PIF_NTSC` repository secret, stored as base64. The preparation script checks its length and writes it into the temporary `.work` directory. The job requires this secret before it can run cartridge tests.

The workflow builds with GCC and Clang on Linux and MSVC on Windows. A separate
Clang job builds both pinned cartridge variants in separate target directories
and runs the local regressions, default suite, and extended suite in release
and sanitizer builds. Missing extended groups and failed cases make the job
fail. Each run retains its logs, input hashes, compiler details, and exit codes;
see [continuous integration](testing/continuous-integration.md).

When a default test ROM is configured, CTest also runs `nemu64_warm_reset`.
This check completes the entire cartridge suite, presses and releases the reset
button, and completes the same suite again through the supplied PIF firmware.
It requires a preserved RAM marker and warm-start flag at NMI entry, a full
pre-NMI delay, matching test counts, and a reset button that rearms after reboot.
The pinned cartridge requires the 8 MiB configuration because its heap endpoint
is fixed at 7 MiB. See [warm-reset behavior](hardware/warm-reset.md) for the
separate coverage of 4 MiB machines and the remaining hardware timing limits.
