# Building and testing

Start with the [build guide](building.md) for compiler installation, Linux packages and a desktop-only build. Use Git, CMake 3.25 or newer and a C++20 compiler. Run the following commands from the repository root. Test fixtures generate their own inputs; the test suite does not require commercial game or PIF downloads.

## Build all targets and run tests

### Windows MSVC

Install Visual Studio 2026 with the C++ desktop workload, the Windows SDK and MSVC 19.51 or newer. Use CMake 4.2 or newer for its generator. CI currently builds this path with MSVC 19.51.36260.0; the large DIP fixture needs the [individual string-literal limit removed in MSVC 14.51](https://learn.microsoft.com/en-us/cpp/cpp/string-and-character-literals-cpp#size-of-string-literals).

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DCUPID_DESKTOP=ON -DBUILD_TESTING=ON -DCUPID_INTERPROCEDURAL_OPTIMIZATION=ON
cmake --build build --config Release --parallel 2 -- /p:CL_MP=true /p:CL_MPCount=2
ctest --test-dir build -C Release --output-on-failure --parallel 2
```

The full build includes test executables; building only `cupid_desktop` does not build the suite. Visual Studio 2022 can build the desktop app, but its MSVC toolset cannot compile that fixture. See [compiler troubleshooting](building.md#compiler-troubleshooting).

### Windows ClangCL

Install Visual Studio 2026's C++ Clang tools and MSBuild toolset integration, and use CMake 4.2 or newer. CI currently uses Clang 22.1.3 in this installation:

```powershell
cmake -S . -B build-clang -G "Visual Studio 18 2026" -A x64 -T ClangCL -DCUPID_DESKTOP=ON -DBUILD_TESTING=ON -DCUPID_INTERPROCEDURAL_OPTIMIZATION=ON
cmake --build build-clang --config Release --parallel 2
ctest --test-dir build-clang -C Release --output-on-failure --parallel 2
```

### Linux desktop

Install the [Linux build dependencies](building.md#linux). For headless desktop tests, also install Xvfb and xauth:

```sh
sudo apt-get install --yes xvfb xauth
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCUPID_DESKTOP=ON \
  -DBUILD_TESTING=ON -DCUPID_INTERPROCEDURAL_OPTIMIZATION=ON
cmake --build build --parallel 2
SDL_AUDIODRIVER=dummy xvfb-run -a ctest --test-dir build \
  -C Release --output-on-failure --parallel 2
```

The dummy audio driver keeps these tests independent of a physical output device. CI separately checks real PulseAudio device recovery:

```sh
sudo apt-get install --yes pulseaudio pulseaudio-utils python3
python3 .github/scripts/test-linux-audio.py build/linux_frontend_tests
```

That script creates a temporary PulseAudio server and needs permission to start local processes and create temporary files.

### Core and software-renderer build

This path avoids desktop dependencies and Vulkan initialization:

```sh
cmake -S . -B build-software -DCMAKE_BUILD_TYPE=Release \
  -DCUPID_DESKTOP=OFF -DCUPID_HARDWARE_RENDERER=OFF -DBUILD_TESTING=ON
cmake --build build-software --parallel 2
ctest --test-dir build-software --output-on-failure --parallel 2
```

On Windows, use a Visual Studio generator and add `--config Release` to the build and `-C Release` to CTest. Linux ARM64 CI uses this core/software path with GCC and Clang. The software renderer has limited drawing support, so passing these tests does not establish commercial game rendering.

## Build options

| CMake option | Default | Effect |
| --- | --- | --- |
| `CUPID_DESKTOP` | ON on Windows, OFF elsewhere | Build the Windows or Linux desktop frontend and its dependencies. |
| `CUPID_HARDWARE_RENDERER` | ON | Build the Vulkan renderer. OFF retains the limited software path. |
| `CUPID_REQUIRE_HARDWARE_RENDERER_TESTS` | OFF | Fail hardware-renderer tests when Vulkan initialization is unavailable instead of skipping them. Requires the hardware renderer. |
| `CUPID_INTERPROCEDURAL_OPTIMIZATION` | ON | Enable supported IPO in Release, RelWithDebInfo and MinSizeRel builds. CI checks both ON and OFF. |
| `CUPID_MINIMUM_CPU` | ON | Add the optimized GCC/Clang x64 architecture baseline. OFF leaves architecture flags unchanged; it does not guarantee compatibility with older CPUs. |
| `BUILD_TESTING` | ON | Build and register the CTest suite. |
| `CUPID_GROUP_CORE_TESTS` | ON | Group compatible core fixture builds where supported, notably optimized MSVC builds. CTest still runs separate registered tests. |

For a GPU qualification run, configure with `-DCUPID_REQUIRE_HARDWARE_RENDERER_TESTS=ON` and a working Vulkan driver. Linux CI installs its software Vulkan implementation through `.github/scripts/setup-vulkan.sh`; that script is a CI setup path rather than a prerequisite for every desktop user.

## Find and run a focused test

List registrations before selecting a group:

```sh
ctest --test-dir build -C Release -N
ctest --test-dir build -C Release -R '^desktop_' --output-on-failure
```

On a headless Linux desktop build, prefix the second command with `SDL_AUDIODRIVER=dummy xvfb-run -a`. Use CTest instead of invoking test executables directly so fixture dependencies, resource locks and working directories remain in effect. Run the full relevant suite before submitting a change; focused tests alone do not cover the CI matrix.

## Formatting

The repository's `.clang-format` applies to C++ headers and sources in `src/` and `tests/`. Install `clang-format`; CI uses the version available on its Ubuntu runner. Check without changing files:

```sh
find src tests -type f \( -name '*.hpp' -o -name '*.cpp' \) -print0 | \
  xargs -0 clang-format --dry-run --Werror
```

PowerShell equivalent:

```powershell
$sourceFiles = Get-ChildItem src,tests -Recurse -File | Where-Object { $_.Extension -in '.hpp', '.cpp' }
$sourceFiles | ForEach-Object { clang-format --dry-run --Werror $_.FullName }
```

Format only files you changed with `clang-format -i path/to/file.cpp`. Do not format fetched dependencies under the build directory.

## Linux sanitizers

Install Clang and the Vulkan runtime used for hardware tests. CI uses this core build with AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
cmake -S . -B build-sanitizers -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS_DEBUG="-O1 -g" \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer" \
  -DCUPID_REQUIRE_HARDWARE_RENDERER_TESTS=ON
cmake --build build-sanitizers --parallel 2
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir build-sanitizers --output-on-failure --parallel 2
```

Desktop defaults to OFF on this Linux configuration. Add desktop prerequisites and `-DCUPID_DESKTOP=ON` if your change needs sanitizer coverage of the frontend.

## Source and test map

| Source | Matching tests and purpose |
| --- | --- |
| `src/core/cpu/`, `src/core/rsp/` | `tests/cpu/`, `tests/rsp/`: processor execution, recompilation, caches and vector operations. |
| `src/core/devices/`, `src/core/cartridge/`, `src/core/controller/`, `src/core/disk/`, `src/core/arcade/` | `tests/devices/`: peripheral, storage, controller, disk and arcade behavior. |
| `src/core/system/`, `src/core/state/` | Integration cases in `tests/devices/` and checkpoints in `tests/state/`. |
| `src/renderer/` | `tests/renderer/`: rendering, frame output and renderer state. |
| `src/desktop/windows/`, `src/desktop/linux/` | Platform frontend code; `tests/desktop/windows/` and `tests/desktop/linux/` cover platform-specific components. |
| `src/desktop/session/`, `src/desktop/audio/`, `src/desktop/controls/`, `src/desktop/state/`, `src/desktop/storage/`, `src/desktop/replay/` | `tests/desktop/`: shared sessions, audio, input, state slots, save files and replay support. |
| `src/core/debug/`, `src/desktop/debug/` | `tests/debug/`: machine inspection, diagnostics, tracing and GDB support. |
| `cmake/`, `.github/workflows/ci.yml`, `.github/scripts/` | Build targets, CI matrix and CI support scripts. |

`src/desktop/session.cpp` connects game and firmware files to the core. `src/desktop/windows/main.cpp` and `src/desktop/linux/main.cpp` parse their respective command-line options. Start there for launch behavior, then follow the platform window implementation.

## CI checks and documentation changes

The [build workflow](https://github.com/cupidthecat/cupid-n64/actions/workflows/ci.yml) runs ten jobs: Windows and Linux Release builds with IPO ON/OFF, Windows ClangCL, software-renderer builds on Windows and Linux, Linux ARM64 GCC/Clang, and a Linux Clang formatting/sanitizer job. Linux hardware jobs require hardware-renderer tests; the desktop job also runs dummy-audio frontend tests and PulseAudio device recovery. Successful desktop jobs upload the artifacts named in the [build guide](building.md#ci-downloads).

Check the results for the exact PR head commit. A green older run does not cover new changes, and automated tests do not establish sustained full-speed game playback or audible quality on every host.

Keep the README option table aligned with both `main.cpp` parsers, build commands aligned with CMake and CI, and user-facing menu names aligned with the current window. Review desktop guide instructions against a running frontend when changing them. The wiki's desktop and contributor pages are published copies of `docs/user-guide.md` and this page; update the repository copies before publishing their wiki equivalents.
