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

Use `--build-dir` for separate compiler configurations, `--jobs` to limit parallel
compilation, and `--sanitizers --config RelWithDebInfo` for Clang sanitizer checks.
On Windows, `--generator "Visual Studio 17 2022"` selects MSVC when that version
of Visual Studio is installed. Choose the generator matching your installation.

Without `--rom`, the script runs only local regressions. Add `--extended-rom`
to run the optional cartridge groups as well. Both ROM options require `--pif`.
A failing optional group makes validation fail; the script does not suppress it.

## Test input

The CI workflow pins `thelemmy/nemu64-test` to commit `9a8b9f7d94ee2f6f57d7feed70c98c22cdc30e6c`. That checkout selects Rust `nightly-2026-07-16` through its toolchain file. `nust64` version `0.4.1` converts its ELF executable into a cartridge image.

From the test checkout:

```sh
cargo build --release --locked
cargo +stable install nust64 --version 0.4.1 --locked
nust64 --elf target/mips-nintendo64-none/release/n64-systemtest
```

The generated ROM is `target/mips-nintendo64-none/release/n64-systemtest.z64`. Keep the original test source and its expected results intact when investigating a failure.

The default build enables the base group. A separate build can include the upstream timing, cycle, CPU-hazard, partially characterized hardware, and experimental rendering tests:

```sh
cargo build --release --locked --target-dir target-extended \
  --features timing,cycle,cop0hazard,poorly_understood_quirk,experimental_rdp
nust64 --elf target-extended/mips-nintendo64-none/release/n64-systemtest
```

Record the ROM revision and enabled features with results. A successful default run does not establish that the optional groups passed.

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

## Interpreting failures

The cartridge runner prints the ROM's output unchanged. It checks the aggregate counts, requires a result for each enabled category, and waits for the final summary line. Truncated output, malformed counts, duplicate categories, an empty test run, and a reported panic all fail validation.

The ROM formats integer test parameters in hexadecimal. For example, the `24`
in `(true, 24, 36.3)` means decimal 36; the floating-point value stays decimal.
Account for that formatting when comparing a failure with its source parameters.

The instruction limit bounds a run that stalls inside guest software. A limit failure includes the PC, pending next PC, exception state, registers, and the last 32 instruction addresses. Use those addresses with the matching test ELF when examining a failing sequence.

A passing regression should demonstrate the affected behavior, including observable results and side effects. For a DMA change, this can include transferred bytes, untouched memory, address postincrements, busy state, and interrupt delivery. For an instruction change, this can include the destination register, flags, exception state, delay-slot behavior, and register aliases.

## CI boot firmware

The accuracy job reads an NTSC boot ROM from the `N64_PIF_NTSC` repository secret, stored as base64. The preparation script checks its length and writes it into the temporary `.work` directory. The job requires this secret before it can run cartridge tests.

The workflow builds with GCC and Clang on Linux and MSVC on Windows. A separate Clang job runs the local regressions and complete default ROM suite, followed by the same checks with sanitizers.
