# Profile-guided builds

Clang can use recorded execution counts to optimize `cupid_core` and `cupid_host`.
The profile changes compiler decisions about the existing emulation code. CPU and
device clocks, instruction behavior, rasterization, and strict floating-point
settings stay the same. The desktop and test executables link the optimized
libraries; their own source files do not receive profile-use flags.

This build mode requires upstream Clang's GNU-compatible driver, Python 3.10 or
later, a single-configuration Release build, and working interprocedural
optimization. Use the same Clang installation, target, build settings, and
`llvm-profdata` version for collection and use. GCC, MSVC, clang-cl, Apple Clang,
cross-compilation, compiler launchers, and sanitizer builds use the ordinary
build configuration instead.

## Collect a profile

Start with a fresh build and counter directory. These commands use the default
Clang tools on Linux; substitute their full paths when several versions are
installed. On Windows, run the commands in a Visual Studio developer shell with
`clang++.exe` and its matching `llvm-profdata.exe` available.

```sh
cmake -S . -B build-profile-generate -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ \
  -DCUPID_IPO=ON -DCUPID_PROFILE_GENERATE="$PWD/.work/profile/raw"
cmake --build build-profile-generate --parallel 4
ctest --test-dir build-profile-generate --output-on-failure
```

Configuration records the profiled source files and build settings in
`.work/profile/raw/context.json` before the instrumented libraries are built.
Every linked consumer receives the profile runtime needed to write counters.
The counters use atomic updates because rendering can run on several threads.

Exercise the generated executable with representative cartridge workloads before
merging the counters. A desktop training build can use `-DCUPID_DESKTOP=ON` and
the [documented SDL source selection](../frontend/desktop.md). Retain the input
hashes and successful process results for each training run. Keep firmware and
commercial cartridge images out of published artifacts. Instrumented execution
is substantially slower and is not a performance measurement.

After every intended training process has exited successfully:

```sh
llvm-profdata merge -o .work/profile/core.profdata .work/profile/raw/*.profraw
python3 tools/ci/profile.py create --source-root . \
  --profile .work/profile/core.profdata \
  --manifest .work/profile/core.json \
  --build-cache build-profile-generate/CMakeCache.txt \
  --llvm-profdata llvm-profdata
```

The package command checks the recorded training context against the current
source and compiler settings. It also verifies that `llvm-profdata` matches the
Clang version when that tool is supplied. An existing manifest is never
overwritten. A changed training context requires a fresh counter directory;
do not combine old counters with counts collected after an implementation or
toolchain change.

In PowerShell, expand the raw file list before invoking `llvm-profdata`, because
native Windows commands do not expand wildcard arguments:

```powershell
$profileFiles = Get-ChildItem -LiteralPath .work/profile/raw -Filter *.profraw |
    Select-Object -ExpandProperty FullName
& llvm-profdata.exe merge -o .work/profile/core.profdata $profileFiles
```

## Validate and use the profile

Use a separate build directory for the optimized executable:

```sh
python3 tools/ci/validate.py --build-dir build-profile-use --compiler clang++ \
  --profile-use .work/profile/core.profdata \
  --profile-manifest .work/profile/core.json
```

Add `--rom`, `--extended-rom`, `--pif`, and `--test-source` to run the complete
cartridge checks. Add `--desktop` and `--sdl-source` for desktop validation. The
validator records and rechecks both profile files alongside its ordinary source
and cartridge inputs. `--profile-generate <directory>` selects instrumented
validation instead. Omitting the profile options explicitly selects an ordinary
build, including when a build directory previously used a profile.

Direct CMake builds use `CUPID_PROFILE_USE` and `CUPID_PROFILE_MANIFEST` for the
same files. Missing profiles, changed payloads, stale implementation files,
different compiler binaries or targets, and mismatched build settings fail
configuration. Core and host source membership is checked against CMake's actual
source list. Documentation, tests, and desktop-only source changes do not by
themselves require retraining.

Replacing a package at the same path triggers configuration again. The build uses
a copy named with the payload's SHA-256 hash, so a changed profile also changes
the compiler command and rebuilds the affected objects. Profile mismatch
diagnostics remain errors. Use a separate ordinary sanitizer build; profile
optimization does not replace memory and undefined-behavior checks.

## Measure the result

Compare ordinary and profile-use Release builds with the same compiler, inputs,
controller sequence, and interprocedural optimization settings. Keep compilation
and other benchmarks out of the measurement interval. Record complete execution
and audio/video observations as well as elapsed time, and include a workload
that was not used for training. A faster replay does not establish sustained
desktop speed or normal audio playback; those require separate acceptance runs.
