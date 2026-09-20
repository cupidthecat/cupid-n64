# Cupid-N64

Cupid-N64 is a Nintendo 64 hardware emulator written in C++20. Its command-line runner boots a cartridge through a supplied PIF boot ROM and captures hardware-test output through the cartridge debug interface.

## Build

The project requires CMake 3.22 or newer and a C++20 compiler. Compiler warnings are treated as errors by default.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

For Visual Studio on Windows:

```powershell
cmake -S . -B build-vs -A x64
cmake --build build-vs --config Release
ctest --test-dir build-vs -C Release --output-on-failure
```

The local regression suite exercises instruction results, exceptions, address translation, caches, RAM transactions, signal processing, floating point, peripheral registers, and test-result parsing. Tests remain active in release builds.

## Run a cartridge

```sh
build/cupid-n64 cartridge.z64 --pif pif.ntsc.rom --max-instructions 4000000000
```

The PIF file must contain 1984 bytes of boot code or a 2048-byte boot image. Cartridge images may use big-endian, byte-swapped, or word-swapped storage. Boot firmware and cartridge images are supplied separately.

For hardware-test ROMs, add `--require-test-success`. The process succeeds only after receiving a complete test summary with no failures. An exception in the host, a stalled CPU bus, or an incomplete test run produces a failure result. An incomplete run includes the CPU registers and recent instruction addresses for diagnosis.

## ROM accuracy tests

Build the default `nemu64-test` ROM, then configure both image paths:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCUPID_TEST_ROM=/absolute/path/to/n64-systemtest.z64 \
  -DCUPID_PIF_ROM=/absolute/path/to/pif.ntsc.rom
cmake --build build
ctest --test-dir build --output-on-failure
```

This adds the complete ROM run to CTest alongside the local regressions. The ROM runs through the same CPU, caches, memory bus, DMA engines, and peripheral registers as other cartridges.

See [the testing guide](docs/testing.md) for the pinned test input, optional test groups, and sanitizer commands.

Before committing or pushing, run `python tools/ci/validate.py` with your ROM and
PIF paths. This checks clang-format output and runs the strict build and tests
used by CI. The testing guide lists the compiler and sanitizer options.

## Core layout

Hardware behavior is documented in [CPU timing](docs/hardware/cpu-timing.md),
[instruction fetch ordering](docs/hardware/instruction-fetch.md),
[video timing](docs/hardware/video-timing.md),
[RCP event scheduling](docs/hardware/rcp-scheduling.md),
[RDRAM interface state](docs/hardware/rdram-interface.md),
[RDP fill rectangles](docs/hardware/rdp-fill.md),
[PI transfer timing](docs/hardware/peripheral-interface.md),
[cartridge bus transactions](docs/hardware/cartridge-bus.md),
[FlashRAM](docs/hardware/flash-memory.md), and
[the serial interface](docs/hardware/serial-interface.md), including current
timing limits and the regressions that cover each path.

`System` connects the VR4300 CPU, signal processor, and physical bus. CPU instructions advance the RCP at its 2:3 clock ratio. CPU caches issue separate bus transactions, and DMA accesses reach the physical memory interface directly.

The memory model tracks individual RAM chips, their initialization registers, current calibration, address mapping, and hidden coverage bits. Cartridge boot uses the security part's seed and checksum, with separate handling for challenge responses. Tests exercise these components through their public interfaces and encoded machine instructions.
