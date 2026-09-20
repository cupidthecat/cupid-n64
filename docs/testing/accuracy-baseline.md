# Hardware accuracy baseline

This baseline records the results for emulator commit
`be366ff4ff157581ed7ecd5ea2f789a47f642480` on September 19, 2026.
It is a comparison point for subsequent fixes, not a release qualification.
The extended suite fails 11 timing cases. Rendering, audio, accessories, and
cartridge compatibility require testing beyond these suites.

Subsequent triangle-pipeline work exposes 11 additional failures in the pinned
experimental RDP fixtures. Their command packing and coverage expectations are
audited [separately](rdp-triangle-fixtures.md). The results below remain the
historical baseline for `be366ff`, not the current extended-suite totals.

## Inputs

The test source is `thelemmy/nemu64-test` commit
`9a8b9f7d94ee2f6f57d7feed70c98c22cdc30e6c`, version 3.0.0. Tracked source files
and expected results were unchanged. The default ROM enables `base` and its
`quick` dependency. The extended ROM also enables `timing`, `cycle`,
`cop0hazard`, `poorly_understood_quirk`, and `experimental_rdp`.

SHA-256 hashes of the inputs used in every run below:

| Input | SHA-256 |
| --- | --- |
| Default test ROM | `e9d93dc1f854af7c60b9d576e15615766ce9e693a9ce794140c3736603732769` |
| Extended test ROM | `9a85cf5b8ea89af4170abb0a14fb9b415232b3904e1257f99036485665478c5c` |
| NTSC PIF boot image, 1,984 bytes | `fa7b09795ef1e54461e59f6f2d902368133e3f1cd980e34383e6a780d74beffd` |

Boot firmware is supplied separately and is not committed. The test ROMs were
built on Windows with Rust `nightly-2026-07-16`: rustc
`1.99.0-nightly (d0babd8b6 2026-07-15)` and Cargo
`1.99.0-nightly (59800466c 2026-07-07)`. ROM conversion used `nust64 0.4.1`.
Rebuilding both images in new target directories produced the same hashes.

## Toolchains and results

All builds enabled strict warnings. Formatting used clang-format 22.1.0.
Windows builds used CMake 4.2.3 and Python 3.14.3; Ninja builds used Ninja
1.13.2. Linux builds used CMake 3.28.3, Ninja 1.11.1, and Python 3.12.3.

| Host and compiler | Configuration | Local regressions | Default ROM | Extended ROM |
| --- | --- | --- | --- | --- |
| Windows, Clang 22.1.0 | Release | 299 pass | 4,637 pass | 11 timing failures |
| Windows, Clang 22.1.0 | RelWithDebInfo, ASan/UBSan | 299 pass | 4,637 pass | Same 11 failures; no sanitizer diagnostics |
| Windows, MSVC 19.51.36246.0 | Release | 299 pass | 4,637 pass | Not run |
| Linux, GCC 13.3.0 | Release | 299 pass | 4,637 pass | Not run |
| Linux, Clang 18.1.3 | Release | 299 pass | 4,637 pass | 11 timing failures |

The Linux Clang run used a new build directory. The extended results separate
each reported category:

| Category | Passed | Failed | Default ROM coverage |
| --- | ---: | ---: | --- |
| Base | 4,649 | 0 | 4,637 cases |
| Timing | 1,593 | 11 | Not enabled |
| Cycle | 13 | 0 | Not enabled |
| CP0 hazards | 5 | 0 | Not enabled |
| Partially characterized hardware quirks | 2 | 0 | Not enabled |
| Experimental RDP | 12 | 0 | Not enabled |

The 12 experimental RDP cases are included in the extended base total; they
must not be counted twice. They exercise filled triangles, edge directions,
scissor boundaries, negative coordinates, and randomized coverage. Passing
them does not establish complete RDP rendering correctness.

No inputs were missing. The optional `*_stress_test`, `rcp_rsq_dump`, and
`live_progress` features were not enabled. This baseline does not claim results
for those modes. The default and extended runs completed their final summaries;
neither ended at the instruction limit.

## Remaining failures

The following names and parameters come from the extended ROM output.
Integer parameters are printed in hexadecimal; `(true, 24, 36.3)` therefore
contains an expected median of decimal 36. Cycle counts below are decimal.

| Failing test | Parameter | Measured | Expected | Issue |
| --- | --- | ---: | --- | --- |
| Timing: Load Miss (with VI enabled) | `true` | 41.299 average | 43.25 +/- 1.0 | [#5](https://github.com/cupidthecat/cupid-n64/issues/5) |
| Timing: Load Miss (with VI enabled) | `false` | 41.555 average | 43.25 +/- 1.0 | [#5](https://github.com/cupidthecat/cupid-n64/issues/5) |
| Timing: Load Miss (with VI disabled) | `80000000` | 41.619 average | 42.5 +/- 0.5 | [#5](https://github.com/cupidthecat/cupid-n64/issues/5) |
| Timing: Load Miss (with VI disabled) | `80100000` | 41.828 average | 42.5 +/- 0.5 | [#5](https://github.com/cupidthecat/cupid-n64/issues/5) |
| Timing: Load Miss (with VI disabled) | `80200000` | 41.752 average | 42.5 +/- 0.5 | [#5](https://github.com/cupidthecat/cupid-n64/issues/5) |
| Timing: Load Miss (with VI disabled) | `80300000` | 41.880 average | 42.5 +/- 0.5 | [#5](https://github.com/cupidthecat/cupid-n64/issues/5) |
| Timing: Load Miss (with VI disabled) | `80400000` | 41.688 average | 42.5 +/- 0.5 | [#5](https://github.com/cupidthecat/cupid-n64/issues/5) |
| Timing: Load Miss (with VI disabled) | `80500000` | 41.545 average | 42.5 +/- 0.5 | [#5](https://github.com/cupidthecat/cupid-n64/issues/5) |
| Timing: Load Miss (with VI disabled) | `80600000` | 41.439 average | 42.5 +/- 0.5 | [#5](https://github.com/cupidthecat/cupid-n64/issues/5) |
| Timing: Load Miss (with VI disabled) | `80700000` | 41.552 average | 42.5 +/- 0.5 | [#5](https://github.com/cupidthecat/cupid-n64/issues/5) |
| Timing: Load from uncached (with VI enabled) | `(true, 24, 36.3)` | 32 median | 36 +/- 1 | [#6](https://github.com/cupidthecat/cupid-n64/issues/6) |

Shared RDRAM scheduling is tracked in
[#4](https://github.com/cupidthecat/cupid-n64/issues/4). The release tracker,
[#2](https://github.com/cupidthecat/cupid-n64/issues/2), covers subsystem audits
and missing behavior that these tests do not establish.

## Reproduce

Use a separate checkout at the emulator revision above and the pinned test
revision. Follow the [test ROM build instructions](../testing.md#test-input),
using fresh target directories for the default and extended images. Check their
hashes before comparing results. On Windows, keep the rustup proxies first in
`PATH` so Cargo and rustc select the same nightly toolchain.

The following commands select the recorded toolchain and features explicitly.
Run them from the pinned test checkout, with `nust64 0.4.1` on `PATH`:

```sh
cargo +nightly-2026-07-16 build --release --locked --target-dir target-pinned-base
nust64 --elf target-pinned-base/mips-nintendo64-none/release/n64-systemtest
cargo +nightly-2026-07-16 build --release --locked --target-dir target-pinned-extended \
  --features timing,cycle,cop0hazard,poorly_understood_quirk,experimental_rdp
nust64 --elf target-pinned-extended/mips-nintendo64-none/release/n64-systemtest
```

From the emulator checkout, with the two images in `.work/nemu64-test/`:

```sh
python3 tools/ci/validate.py \
  --build-dir build-baseline-be366ff --compiler clang++ --jobs 2 \
  --clang-format /absolute/path/to/clang-format-22.1.0 \
  --rom .work/nemu64-test/target-pinned-base/mips-nintendo64-none/release/n64-systemtest.z64 \
  --extended-rom .work/nemu64-test/target-pinned-extended/mips-nintendo64-none/release/n64-systemtest.z64 \
  --pif /absolute/path/to/pif.ntsc.rom
```

The recorded command used an unused build directory and Clang 18.1.3 on Linux.
CTest returned 8 because `nemu64_extended` failed; that exit status must remain
a failure. The other two CTest entries passed. The cartridge runner uses
`--max-instructions 4000000000 --require-test-success` for each ROM.

For Windows Clang sanitizer validation, use the same ROM arguments with
`--compiler clang++ --config RelWithDebInfo --sanitizers` and a separate build
directory. The compiler used for the recorded sanitizer run was Clang 22.1.0.

Local logs are retained outside the source directories in
`.work/baseline-be366ff-linux-clang.log` and
`.work/local-ci-cache-refresh-{clang,sanitize,msvc,gcc,linux-clang}.log`.
Each build directory also contains the full cartridge output in
`Testing/Temporary/LastTest.log`. Preserve these files before rerunning CTest,
which replaces its previous log.

CI runs the local regressions and default cartridge suite, including sanitizer
validation. It does not run the extended image at this revision. A successful
CI result therefore does not imply that the 11 timing failures are resolved.
