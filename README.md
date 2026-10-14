# Cupid-N64

Cupid-N64 emulates Nintendo 64, 64DD and Aleck64 hardware. The desktop app runs on Windows and Linux. Bring your own legally obtained game images and PIF firmware; the repository and CI artifacts do not include them.

## Try the desktop app

Open a successful run in [GitHub Actions](https://github.com/cupidthecat/cupid-n64/actions/workflows/ci.yml) for the branch or commit you want to test. Download and extract `cupid-n64-windows` or `cupid-n64-windows-clang`, then run `cupid-n64.exe`. GitHub requires you to sign in to download an artifact. Linux runs also provide `cupid-n64-linux`; see the [Linux prerequisites](docs/building.md#linux) before running it.

Windows builds require Microsoft's [current x64 Visual C++ Redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist) and a working Vulkan driver. Install the runtime if Windows reports a missing `MSVCP140.dll` or `VCRUNTIME140.dll` when opening the app.

On Windows:

1. Choose **File > Local firmware settings...** and select your Nintendo 64 PIF image. Ordinary Nintendo 64 and 64DD launches require a 1,984-byte PIF file.
2. Choose **File > Open ROM...** and select your `.z64`, `.n64` or `.v64` cartridge image. You can also use **File > Launch setup...** to select the cartridge and firmware together.
3. Press **Enter** for Start. The default controls use **WASD** for the stick, **J** for A, **K** for B and **Space** for Z. **F1** opens the controls help.

On Linux, choose **File > Select PIF firmware...**, then **File > Open game...**. Keyboard controls are listed under **Emulation > Keyboard controls**.

The hardware renderer needs a working Vulkan driver. The software renderer is useful for tests but does not draw all commands used by commercial games. A successful launch does not establish full-speed playback or compatibility with every game.

## Build from source

Install Git, CMake 3.25 or newer and a compiler with C++20 support. The first configure downloads pinned build dependencies, so it needs network access.

```sh
git clone https://github.com/cupidthecat/cupid-n64.git
cd cupid-n64
```

On Windows, install current Visual Studio 2022 with **Desktop development with C++**, including the Windows SDK. Run these commands in PowerShell:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCUPID_DESKTOP=ON
cmake --build build --config Release --target cupid_desktop --parallel 2 -- /p:CL_MP=true /p:CL_MPCount=2
& .\build\Release\cupid-n64.exe
```

On Ubuntu, install the packages listed in [Building](docs/building.md#linux), then:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCUPID_DESKTOP=ON
cmake --build build --target cupid_desktop --parallel 2
./build/cupid-n64
```

The [build guide](docs/building.md) covers dependencies, Clang on Windows and compiler troubleshooting. The [contributor guide](docs/contributing.md) covers the full build, tests, formatting and CI.

## Launch with your own files

Quote paths that contain spaces. This PowerShell command opens a cartridge directly:

```powershell
& .\build\Release\cupid-n64.exe --rom "C:\Games\My Game.z64" --pif "C:\Firmware\pif.bin"
```

The equivalent Linux command is:

```sh
./build/cupid-n64 --rom "$HOME/Games/My Game.z64" --pif "$HOME/Firmware/pif.bin"
```

| Option | Purpose |
| --- | --- |
| `--rom FILE` | Load a cartridge image. |
| `--pif FILE` | Load the matching PIF firmware image. |
| `--ipl FILE` | Enable the 64DD drive with an IPL firmware image. |
| `--disk FILE` | Insert a 64DD disk image; requires `--ipl`. The drive can also start empty. |
| `--arcade GAME` | Select an Aleck64 profile; requires a cartridge and PIF. See [profile names](docs/building.md#command-line-details). |
| `--dip NAME=VALUE` | Set an arcade DIP option; repeat for more than one selection. |
| `--frames N` | Run a positive number of video fields, then exit. Requires a game or IPL and PIF. |
| `--capture FILE` | Write a P6 PPM capture and a text report; requires `--frames`. |
| `--test-input` | Use the built-in repeatable input script; requires `--frames`. |
| `--input-replay FILE` | Windows only: replay recorded controller input; requires `--frames`. |
| `--field-trace FILE` | Windows only: write per-field observations; requires `--frames`. |
| `--capture-fields FILE` | Windows only: select fields to capture; requires `--frames` and `--capture`. |
| `--settings FILE` | Linux only: use an alternate settings file. |
| `--software` | Linux only: select the limited software renderer. |

The executable does not have `--help` or `--version` switches. The [build guide](docs/building.md#command-line-details) explains bounded runs and the platform-specific options.

## Controls and saves

Read the [desktop user guide](docs/user-guide.md) for menus, platform-specific shortcuts, focus behavior and save locations. The same guides are available in the [wiki](https://github.com/cupidthecat/cupid-n64/wiki): [playing and keeping saves](https://github.com/cupidthecat/cupid-n64/wiki/Desktop-user-guide) and [building and testing](https://github.com/cupidthecat/cupid-n64/wiki/Building-and-testing).

Audio and video synchronization are separate settings. See [Playback synchronization](docs/playback-synchronization.md) for their behavior.
