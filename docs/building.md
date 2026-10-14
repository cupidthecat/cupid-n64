# Building and launching

## Prerequisites

Use Git, CMake 3.25 or newer and a C++20 compiler. The desktop frontend supports Windows and Linux. CI builds x64 desktop apps and checks the core on Linux ARM64 with the software renderer.

CMake fetches the pinned JIT, graphics and SDL dependencies. You need internet access during the first configure; a separate SDL development kit or Vulkan SDK is not required. Hardware rendering still needs an installed Vulkan runtime and a compatible GPU driver. Optimized GCC/Clang x64 builds use the x86-64-v2 CPU baseline.

Clone the repository and run all commands from its root:

```sh
git clone https://github.com/cupidthecat/cupid-n64.git
cd cupid-n64
```

To build a pull request instead of the default branch:

```sh
git fetch origin pull/272/head
git switch --detach FETCH_HEAD
```

Use a separate build directory for each compiler, generator or renderer configuration.

## Windows

Install current Visual Studio 2022 with the **Desktop development with C++** workload, an x64 MSVC toolset and the Windows SDK. CMake can locate this installation from ordinary PowerShell. The commands below build only the desktop app:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCUPID_DESKTOP=ON
cmake --build build --config Release --target cupid_desktop --parallel 2 -- /p:CL_MP=true /p:CL_MPCount=2
& .\build\Release\cupid-n64.exe
```

The executable is `build\Release\cupid-n64.exe`. With Visual Studio's multi-configuration generator, select Release with `--config Release` when building and `-C Release` when testing.

### Clang on Windows

Install Visual Studio's **C++ Clang tools for Windows** component, including its MSBuild integration. A standalone `clang-cl` on PATH does not install the `ClangCL` Visual Studio toolset.

```powershell
cmake -S . -B build-clang -G "Visual Studio 17 2022" -A x64 -T ClangCL -DCUPID_DESKTOP=ON
cmake --build build-clang --config Release --target cupid_desktop --parallel 2
& .\build-clang\Release\cupid-n64.exe
```

### Compiler troubleshooting

If an older MSVC toolset treats warning C4127 in the JIT dependency as an error, update the toolset or reconfigure with the complete default C++ flags plus that warning suppression:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCUPID_DESKTOP=ON "-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /W3 /GR /EHsc /wd4127"
```

Keep `/EHsc` and `/GR`; replacing all flags with `/wd4127` removes exception and RTTI settings. MSVC 19.43 can also fail with C2026 while compiling the large DIP test fixture. Building the `cupid_desktop` target avoids that fixture, but the full test build needs a newer toolset or the ClangCL path used by CI. An MSB8020 error for `ClangCL` means the Visual Studio Clang component is missing.

## Linux

For Ubuntu, install the desktop build dependencies:

```sh
sudo apt-get update
sudo apt-get install --yes build-essential git cmake pkg-config \
  libgtk-3-dev libepoxy-dev libx11-dev libasound2-dev libpulse-dev \
  libudev-dev libdbus-1-dev
```

Check `cmake --version`. If your distribution ships a version older than 3.25, install a newer CMake before configuring. Install your GPU's Vulkan driver; for Mesa-based systems the runtime packages are:

```sh
sudo apt-get install --yes libvulkan1 mesa-vulkan-drivers
```

The NVIDIA proprietary driver supplies its own Vulkan implementation. Desktop execution needs a graphical session. CMake uses pkg-config to find GTK 3, Epoxy, X11, ALSA, PulseAudio, udev and D-Bus.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCUPID_DESKTOP=ON
cmake --build build --target cupid_desktop --parallel 2
./build/cupid-n64
```

`CUPID_DESKTOP` defaults to OFF outside Windows, so specify ON for a Linux desktop build. The executable is `build/cupid-n64` with this single-configuration generator. To use Clang, install `clang` and add `-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++` in a new build directory.

## Open a game

Use your own legally obtained cartridge and firmware images. Ordinary Nintendo 64 and 64DD PIF images must contain 1,984 bytes. Aleck64 accepts 1,984 or 2,048 bytes. A 64DD launch also needs a valid IPL image.

On Windows, start the app and choose **File > Local firmware settings...**. Set **Nintendo 64 and 64DD PIF**, then choose **File > Open ROM...**. **File > Launch setup...** lets you choose the system, cartridge, PIF, IPL and optional disk in one dialog. Firmware changes apply when you reload or open a game.

On Linux, choose **File > Select PIF firmware...**, then **File > Open game...**. Use the command line to supply a 64DD IPL or select an arcade profile.

Keep the game in a writable folder if you want the default save location beside the image. You can choose a separate save folder in the desktop settings; see [save files](user-guide.md#save-files-and-backups).

## Command-line details

The [README option table](../README.md#launch-with-your-own-files) lists every switch accepted by the current frontends. Windows replay and trace switches are not accepted by Linux; Linux `--settings` and `--software` switches are not accepted by Windows.

For a cartridge on Windows:

```powershell
& .\build\Release\cupid-n64.exe --rom "C:\Games\My Game.z64" --pif "C:\Firmware\pif.bin"
```

For a 64DD drive on Linux:

```sh
./build/cupid-n64 --pif "$HOME/Firmware/pif.bin" \
  --ipl "$HOME/Firmware/ipl.bin" --disk "$HOME/Games/My Disk.ndd"
```

`--disk` requires `--ipl`; omitting `--disk` starts the drive empty. You may also supply a cartridge with `--rom`. Arcade launches require `--rom`, `--pif` and `--arcade`, and cannot combine arcade mode with IPL or disk arguments. The accepted profile names are `generic`, `11beat`, `starsldr`, `doncdoon`, `kurufev`, `mayjin3`, `vivdolls`, `twrshaft`, `hipai`, `hipai2`, `srmvs`, `srmvsa` and `mtetrisc`. Use `--dip NAME=VALUE` for the selected game's named DIP options; selections appear in the desktop settings and use the catalog in `src/core/arcade/dip_switches/`.

### Bounded runs and captures

```powershell
& .\build\Release\cupid-n64.exe --rom "C:\Games\My Game.z64" --pif "C:\Firmware\pif.bin" --frames 600 --capture "C:\Captures\frame.ppm" --test-input
```

Create the destination folder first. `--frames N` counts video fields and exits after the requested positive count. `--capture` and `--test-input` require `--frames`. Captures use P6 PPM bytes regardless of the filename extension. The accompanying report is `FILE.txt`; a failed bounded run writes `FILE.error.txt`. This capture format differs from the BMP screenshots saved through the desktop menu.

Bounded runs still load and write game saves. Copy your game and existing saves into a scratch folder when testing repeatable inputs. The built-in input script is a test sequence, not a recording of your keyboard play.

Windows also accepts `--input-replay FILE`, `--field-trace FILE` and `--capture-fields FILE`, all with `--frames`. `--capture-fields` additionally requires `--capture`. Replay and capture-list parsers live in `src/desktop/replay/`; replay examples and validation cases are in `tests/desktop/input_replay.cpp`. Use a replay instead of `--test-input` when supplying recorded input.

On Linux, `--settings FILE` selects an alternate INI file. `--software` overrides the hardware-renderer selection for that run. The software path does not implement all drawing commands used by commercial games.

## CI downloads

Choose a successful run of the [build workflow](https://github.com/cupidthecat/cupid-n64/actions/workflows/ci.yml) for the desired branch and commit. Sign in to GitHub, open the run's **Artifacts** section and download `cupid-n64-windows`, `cupid-n64-windows-clang` or `cupid-n64-linux`. Extract the archive before launching. On Linux, make the downloaded binary executable if necessary with `chmod +x cupid-n64` and install the runtime libraries and graphics driver described above.

These are build artifacts, not bundled game packages. For full build and test commands, continue with [Building and testing](contributing.md).
