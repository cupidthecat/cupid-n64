# Runner hardware configuration

The command-line runner constructs the emulated machine from explicit host options. Cartridge and PIF files are validated before the machine starts, and the validated PIF bytes are passed directly into the system without reopening the file.

## Cartridge and console

`--region auto|ntsc|pal` selects the console video standard. `auto` uses a recognized cartridge header region. An unknown header region requires an explicit selection. `--pif-region auto|ntsc|pal` can declare the region of firmware whose image is not recognized; a declaration that disagrees with recognized firmware is rejected. The selected console region must match the PIF firmware.

`--ram 4|8` selects the installed RDRAM capacity in MiB. `--cic auto|6101|6102|7102|6103|6105|6106|5101|5167|8303|8401|ddus` selects the cartridge security part. `auto` requires boot code that the emulator can identify. An explicit CIC override also updates the seed bytes seen by PIF boot. CIC and console region combinations that the selected security part cannot represent are rejected.

Cartridge images must contain a complete 0x1000-byte header and boot-code area, have a length divisible by four, fit in the cartridge ROM window, and use one of the supported big-endian, byte-swapped, or word-swapped layouts. The runner normalizes the image once before loading it and rejects an unknown byte order or an unaligned entry point.

## Save and RTC hardware

Cartridge save hardware is never selected from the product code alone. Every run must choose one of:

- `--save none`
- `--save sram32`, `sram96`, or `sram128`
- `--save eeprom4k` or `eeprom16k`
- `--save flash --flash-chip PART`

FlashRAM supports `mx29l0000`, `mx29l0001`, `mx29l1100`, `mx29l1101a`, `mx29l1101b`, `mx29l1101c`, and `mn63f81mpn`. `--rtc` attaches the cartridge real-time clock independently of the save device.

Storage file loading and flushing are described in [persistent storage](storage.md); selecting hardware does not change the device protocol itself.

## Controllers and accessories

Ports start disconnected with no accessory. `--controller PORT:gamepad|mouse|none` selects the device on a port. Gamepad accessories use `--accessory PORT:none|controller-pak|rumble-pak|bio-sensor|transfer-pak`. Accessories require a connected gamepad.

A Controller Pak uses one 32 KiB bank by default. `--pak-banks PORT:COUNT`
selects from 1 through 62 banks and requires that port to use a connected
gamepad with `--accessory PORT:controller-pak`. `--pak-file PORT:FILE` loads and
flushes the complete configured Pak image. Banked protocol behavior and the
core configuration API are described in [Controller Pak behavior](controller-pak.md);
the raw file layout is described in [persistent storage](storage.md).

For example, this configures a four-bank Pak on controller port one and uses a
raw 128 KiB Pak image:

```sh
build/cupid-n64 cartridge.z64 --pif pif.ntsc.rom --save none --controller 1:gamepad --accessory 1:controller-pak --pak-banks 1:4 --pak-file 1:controller.pak
```

Programmatic hosts can set `PortOptions::pak_banks` to the same 1 through 62
range. `create_system` applies that capacity before it loads the configured Pak
file, so persistence is checked against the selected size.

A Transfer Pak uses `--transfer-rom PORT:FILE`. Recognized Game Boy headers provide mapper and RAM defaults when their ROM length agrees with the header. Unsupported or ambiguous boards require explicit `--transfer-mapper` and `--transfer-ram`. The runner also accepts `--transfer-rtc PORT:on|off` and `--transfer-rumble PORT:on|off` to override those cartridge features. Mapper choices are `linear`, `mbc1`, `mbc2`, `mbc3`, `mbc30`, and `mbc5`. `--transfer-save PORT:FILE` and `--transfer-rtc-file PORT:FILE` select persistent RAM and clock-state files.

Host paths are stored as filesystem paths and preserve UTF-8 text supplied through the runner option parser. Media tests cover non-ASCII cartridge paths on Windows.

## Validation behavior

`--require-test-success` requires a complete hardware-test report with no failures. `--require-extended-tests` also requires every extended category and feature flag and implies `--require-test-success`. CTest passes `--save none` explicitly for the hardware-test ROMs and adds the extended requirement only to the extended image.
