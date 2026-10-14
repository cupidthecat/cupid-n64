# Desktop user guide

Use your own legally obtained game and firmware files. The [setup guide](building.md) explains how to build or download the app and supply PIF firmware. This page describes the Windows desktop first; [Linux differences](#linux-desktop) are listed separately.

## Open and play on Windows

Choose **File > Local firmware settings...** and select your Nintendo 64 PIF image, then use **File > Open ROM...** to open a cartridge. **File > Launch setup...** also lets you select a Nintendo 64 cartridge, a 64DD setup or an Aleck64 profile with the appropriate local files. Cartridge images may use `.z64`, `.n64` or `.v64` byte order.

Click the game window before using the keyboard. The default mapping for player 1 is:

| N64 input | Keyboard |
| --- | --- |
| Analog stick | W, A, S, D |
| Slower stick movement | Hold Shift |
| A | J |
| B | K |
| Z | Space |
| Start | Enter |
| L / R | Q / E |
| C buttons | Arrow keys |
| D-pad up / left / down / right | I / U / O / P |

Use **View > Input mappings for four players...** to change each player's host input source and bindings. **Emulation > Controller ports for this game...** selects the emulated controllers and accessories. Reload the game when the app marks controller or memory changes as pending.

## Windows menus

### File

- **Open ROM...** opens a cartridge. **Recent ROMs** reopens a previous selection or clears the recent list.
- **Launch setup...** selects the system and its local game and firmware files. **Local firmware settings...** stores firmware paths for future launches.
- **Output and firmware folders...** selects existing folders for saves, screenshots, firmware browsing and debugging output. Output folders must be writable.
- **Insert / change 64DD disk...** and **Eject 64DD disk** manage a disk when the loaded system has a 64DD drive.
- **Screenshot...** opens a file dialog and saves the displayed image as a BMP. It needs a displayed frame.
- **Desktop settings > Import settings... / Export settings...** transfers desktop configuration. Game save contents are separate files.
- **Exit** closes the app and flushes changed game saves.

### Emulation

**Pause** stops or resumes the game. **Frame advance** runs one video field while paused. **Reset** resets the loaded machine. **Mute** changes host sound output.

**Audio** selects volume, stereo balance, output latency and output device. **Playback synchronization** has independent **Synchronize audio** and **Synchronize video** choices. The default is audio on and video off. Audio synchronization waits for output capacity; video synchronization waits for presentation. **Fast-forward** temporarily disables both waits while keeping audio output enabled, then restores your selected synchronization settings. See [Playback synchronization](playback-synchronization.md) for details.

Enable **Rewind > Enable rewind** before holding the rewind shortcut. The submenu controls snapshot frequency, history length, memory limit and muting during rewind. Rewind history is held in memory.

**When unfocused** chooses whether switching to another window pauses the game, continues while blocking game input, or continues while allowing game input. The default is Pause. Returning focus resumes a game stopped by focus loss; an explicit pause remains in effect.

**Memory settings** selects the Expansion Pak and each Controller Pak's capacity. These changes apply after reloading the game. Larger Paks use separate files and keep the old save files. **Controller ports for this game...** sets each port's controller and accessories.

### States

Select one of ten slots, then choose **Save state** or **Load state**. **Undo save** restores the previous slot file or removes a newly created one. **Undo load** restores the machine state from before the last successful load. Undo information lasts until another game is opened or the app closes.

Save states capture the running machine and are separate from the game's EEPROM, SRAM, FlashRAM and Controller Pak saves. Keep ordinary save backups as well as state files; loading a state can change the save memory later written to disk.

### View

**Fullscreen** toggles fullscreen on the monitor selected under **Fullscreen monitor**. Use F11 again to return to the window. Escape normally pauses; when the mouse pointer is captured, Escape releases it.

**Display scaling** selects Best fit, Integer scaling or Stretch to fill. Integer scaling uses whole output-height multiples and falls back to best fit when the window is smaller than one frame. **Aspect ratio** selects Standard N64 (4:3) or Original pixel dimensions. **Window size** selects a viewport preset or a custom size.

The graphics and shader submenus control rendering and presentation. **Input mappings for four players...** changes input bindings, **Shortcuts...** changes desktop shortcuts, and **Capture mouse pointer** is available when an emulated mouse can use it. **Controls / help** displays the default keyboard layout and save-file hints.

The **Developer** menu opens memory inspection, device properties, disassembly, instruction tracing, core diagnostics and GDB connection settings.

## Windows shortcuts

These are the defaults. **View > Shortcuts...** can change them, and menus display the active bindings.

| Action | Shortcut |
| --- | --- |
| Pause / resume | F5 or Escape |
| Advance one field while paused | F4 |
| Reset | F6 |
| Mute | F8 |
| Fullscreen | F11 |
| Controls / help | F1 |
| Save / load selected state slot | F2 / F3 |
| Undo state save / load | Ctrl+F2 / Ctrl+F3 |
| Select slots 1 through 9 / slot 10 | Ctrl+1 through Ctrl+9 / Ctrl+0 |
| Toggle fast-forward | F9 |
| Hold fast-forward | Tab |
| Hold rewind, after enabling it | F7 |
| Save screenshot | F12 |
| Capture / release mouse pointer | Ctrl+M |

## Settings and output folders

Windows stores settings at `%LOCALAPPDATA%\Cupid-N64\settings.ini`. Paste `%LOCALAPPDATA%\Cupid-N64` into Explorer's address bar to find it. The graphics pipeline cache is `cache\pipelines.bin` below that folder.

Game saves and screenshots default to the game image's folder. Debugging reports default beside the capture file when one is specified, otherwise beside the game. Firmware browsing defaults to the app's folder. If an image path has no parent folder, output falls back to the application folder.

Choose **File > Output and firmware folders...** to use other existing directories. A separate save directory adds a content fingerprint to the image's stem, such as `game-<hex-fingerprint>.eep`, so different images with the same name keep separate saves. Changing the folder selection does not move existing saves. Close the app, copy the matching files and state folder into the new location, and preserve the filenames expected by that location. A switch between the default location and a selected folder changes the expected stem.

## Save files and backups

The app loads saves when you open the game. It periodically flushes changed saves while running and saves them when unloading or closing the game. Save writes can fail if the destination is not writable; check the status or error message. Close the app before copying files for a backup.

For a cartridge named `game.z64` in the default location:

| File or folder | Contents |
| --- | --- |
| `game.eep` | Cartridge EEPROM, when present. |
| `game.sra` | Cartridge SRAM, when present. |
| `game.fla` | Cartridge FlashRAM, when present. |
| `game.rtc` | Cartridge clock data, when present. |
| `game.pak` | Port 1's standard 32 KiB Controller Pak. |
| `game.p2.pak`, `game.p3.pak`, `game.p4.pak` | Standard Controller Paks on ports 2, 3 and 4. |
| `game.128k.pak`, `game.p2.128k.pak` | Examples of separate larger Controller Pak files. The capacity suffix is the selected size in KiB. |
| `game.z64.states/slot-1.state` through `slot-10.state` | The ten save-state slots. The folder retains the game image's extension. |

Only devices attached to that game produce their respective saves, and unchanged save memory may not produce a new file. Transfer Pak cartridge saves use a port and cartridge fingerprint in their names, with `.ram`, `.eep`, `.fla` or `.rtc` extensions as appropriate.

64DD disk changes go into a `.disk` save beside the disk image by default. The drive's `.rtc` file is based on the IPL filename. Its clock file has a different layout from a cartridge clock file, so keep each with the image that created it. A selected save folder applies the same fingerprint naming rule to disks and IPL images.

Back up the matching ordinary save files and the complete `.states` folder. Settings export does not include these files. Retain the image and save filenames when restoring a backup; changing a game's name or save-folder selection can change where the app looks. Save files of the wrong size are rejected rather than silently loaded.

## Linux desktop

Linux stores settings in `$XDG_CONFIG_HOME/cupid-n64/settings.ini`, or `~/.config/cupid-n64/settings.ini` when `XDG_CONFIG_HOME` is unset. `--settings FILE` selects another file. Game save extensions and default locations follow the same rules as Windows.

The Linux menu names and shortcuts differ:

| Menu | Actions |
| --- | --- |
| File | Open game..., Select PIF firmware..., Insert disk..., Eject disk, Close game, Quit. |
| Emulation | Pause, Advance frame, Reset, Settings..., Keyboard controls. |
| State | Save state, Load state, Undo save, Undo load and slots 1 through 10. |
| View | Fullscreen, Save screenshot... |

Use **Emulation > Settings...** for audio, synchronization, focus behavior, controller mappings, memory, graphics and file folders. The default game keys match the table at the start of this page. Linux uses **F8** for pause, **F9** for frame advance while paused, **F5** for save state, **F7** for load state and **F11** for fullscreen. The Windows shortcut customization and fast-forward keys do not apply to the Linux frontend.

## If a game will not start or keep its save

Confirm the selected PIF has the required size and matches the system. Check that the cartridge, disk and IPL paths point to readable local files. Hardware rendering requires a Vulkan driver; a software test build can reach the window while failing to draw commands used by a game.

For a missing save, check the active save folder, image name, content fingerprint and Controller Pak capacity. Look for a permissions or save-size error before renaming files. If reporting a problem, include the commit or CI run, platform, command line without private paths, and relevant error text. Do not attach copyrighted game or firmware images.
