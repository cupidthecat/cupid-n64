# Desktop application

`cupid-desktop` runs the hardware core in a resizable SDL3 window. Firmware and
cartridges are supplied separately. The desktop target is optional; the
`cupid-n64` runner and hardware tests also build without SDL or a display.

## Build and start

On Windows with Visual Studio 2022:

```powershell
cmake -S . -B build-desktop -A x64 -DCUPID_DESKTOP=ON
cmake --build build-desktop --config Release
ctest --test-dir build-desktop -C Release --output-on-failure
.\build-desktop\Release\cupid-desktop.exe
```

With Ninja and a C++20 compiler:

```sh
cmake -S . -B build-desktop -G Ninja -DCMAKE_BUILD_TYPE=Release -DCUPID_DESKTOP=ON
cmake --build build-desktop
ctest --test-dir build-desktop --output-on-failure
build-desktop/cupid-desktop
```

CMake accepts an installed SDL3 package at exactly version 3.4.16. Otherwise it
downloads that version's source archive and checks its SHA-256 digest. An
existing source tree can be supplied with
`-DFETCHCONTENT_SOURCE_DIR_SDL3=/path/to/SDL3-3.4.16`. The Windows build copies
`SDL3.dll` beside the executables when SDL is shared. Keep those files together.

Launching without cartridge arguments opens **Hardware**. The **Cartridge**
and **Firmware** buttons open native file choosers. Files can also be dropped
onto the window. Small `.rom` and `.bin` files are treated as firmware, Game Boy
images as Transfer Pak cartridges, and `.mpk` or `.pak` files as Controller Pak
images. Other files are selected as N64 cartridges and validated when loaded.

Choose the correct save device before loading. Save hardware is explicit;
the frontend does not guess a cartridge's SRAM, EEPROM, or FlashRAM type.
The hardware page also selects region, installed RAM, CIC, FlashRAM part, RTC,
and the devices attached to each of four ports. Changes take effect on the
next **Load cartridge** operation. The current machine pauses while hardware
or controller settings are edited.

The [hardware configuration guide](../hardware/configuration.md) describes
the same options for a command-line launch. For example, a cartridge known to
use a 4 Kbit EEPROM can be launched with:

```sh
cupid-desktop cartridge.z64 --pif pif.ntsc.rom --save eeprom4k
```

## Controls and playback

Port one starts with keyboard and gamepad input enabled. **Controls** shows
the current mappings and connected gamepad for each port. Click a key or
gamepad binding, then press the replacement key, button, or axis. Escape
cancels capture; Backspace clears a keyboard binding. Analog axes, inversion,
and dead zone can be changed on the same page. Successful changes are saved
to preferences. A failed save is shown in the window.

The default keyboard uses X for A, Z for B, Enter for Start, Left Shift for Z,
Q/E for L/R, WASD for the stick, arrow keys for the D-pad, and I/K/J/L for the
C buttons. The [input guide](input.md) records the gamepad mapping and input
range rules. SDL's Windows build includes Xbox/XInput support. Virtual
gamepad tests cover polling and device changes; physical-controller gameplay
requires a separate recorded check.

Escape pauses or resumes. F5 requests the console's reset-button sequence.
F11 toggles fullscreen; F12 captures the window to a new BMP beside the
preferences file. **Stop game** flushes persistent storage and returns to an
empty session. A reset preserves the core's warm-reset behavior and does not
replace the loaded cartridge with a fresh machine.

The picture is fitted to 4:3. **Sharp pixels** uses nearest-neighbor scaling;
**Smooth pixels** uses linear scaling. Core VI filtering still follows the
emulated registers. The speed display compares emulated CPU time with host
time and identifies sustained operation below full speed. The frontend does
not skip guest instructions or alter hardware clocks to hide a slow host.

Volume and mute affect host playback. **Retry audio** reopens the current
default device after a failure or device change. Missing audio does not stop
the game window. Pause, reset, cartridge replacement, and stop discard stale
audio. Held controls are released on focus loss and gamepad disconnection.

## Settings and saves

The default settings directory comes from SDL's per-user application path.
Use `--preferences path/to/preferences.conf` to choose another file. The
directory must be writable; the application reports failed replacements.
The [preferences guide](preferences.md) documents the versioned format and
the automatic save names derived from loaded cartridge contents.

Cartridge and accessory files normally live in a `saves` directory beside
preferences. Command-line storage paths override automatic selection. The
[storage guide](../hardware/storage.md) specifies the exact device sizes and
replacement behavior. Saving a preference or device file over an input ROM
or another device's storage is rejected.

When a stop or quit cannot save a loaded machine, the machine remains paused.
Correct the reported path problem and choose **Retry save**, or choose
**Stay open**. **Exit without saving** explicitly discards the retained machine.
An invalid replacement cartridge leaves the previous machine available.

## Reproducible runs and validation

`--run-for SECONDS` bounds a session by wall-clock time. It accepts 0.05 through
86400 seconds and saves before exit. Add `--capture final.bmp` to retain the
last window image. Existing capture files are preserved and cause failure.
`--paused` loads the machine without executing it; `--renderer software`
selects SDL's software renderer for diagnosis.

Timed runs print executed instructions, CPU cycles, delivered fields,
presented frames, DMA sample count, audio queue/drop counts, and a display
frame hash. A run with no nonblank fields fails unless it was explicitly
paused. A completed timed run or an image alone does not establish correct
gameplay or audio.

Run the shared local checks before publishing:

```sh
python tools/ci/validate.py --desktop --compiler clang++ --build-dir build-check
python tools/ci/validate.py --desktop --compiler clang++ --sanitizers \
  --config RelWithDebInfo --build-dir build-check-sanitize
```

Use the matching Windows generator and formatter path where required. Add
the default and extended ROM/PIF options from [hardware validation](../testing.md)
to include the complete cartridge suites. The desktop CI workflow uses this
same entry point. Its dummy audio/video drivers make automated tests usable
without a desktop session; the application itself uses native drivers normally.

Automated coverage includes real SDL software-rendered pixels, virtual
gamepads, asynchronous dialog callbacks, audio queue and device lifecycle,
session commands, failed-save recovery, and native Unicode command-line
round trips. The runner test uses synthetic media while paused and verifies
exact retained save bytes across process restarts. It does not execute a game.

Release acceptance still requires visible and audible cartridge gameplay,
physical gamepad input, save/reload, reset, and a measured sustained session.
Those checks remain separate from unit tests and the hardware-test cartridge.
