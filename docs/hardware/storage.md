# Persistent host storage

The runner loads persistent device images after hardware selection and before cartridge execution. Missing storage files leave each device's fresh contents in place and are created when the runner shuts down. Existing files must have the exact size for the selected hardware; a short or oversized image is rejected before execution.

## File formats

Cartridge SRAM is stored as 32, 96, or 128 KiB of raw bytes. Fresh SRAM is zero-filled. EEPROM is stored as 512 bytes for the 4 Kbit part or 2048 bytes for the 16 Kbit part, with fresh bytes set to `0xff`. FlashRAM is a raw 128 KiB image for all supported Flash parts and also starts at `0xff`.

Each Controller Pak file is exactly 32 KiB and uses the Pak's raw byte order. A missing Pak image starts as zero-filled storage. Transfer Pak RAM files use the exact RAM span supplied by the configured Game Boy cartridge. This includes the 256-byte MBC2 nibble store and the mapper-specific external RAM sizes accepted by the cartridge model.

`--rtc-file` stores the cartridge RTC's 32 retained register bytes without a host structure wrapper. `--transfer-rtc-file` uses a 12-byte portable record: bytes 0-3 are `GBRT`, byte 4 is version 1, bytes 5-7 are seconds/minutes/hours, bytes 8-9 are the 9-bit day counter in big-endian order, byte 10 contains halt and carry in bits 0 and 1, and byte 11 is reserved as zero. Wrong lengths, headers, versions, reserved bits, or day ranges are rejected.

## Paths and replacement

Storage destinations must be distinct from the N64 cartridge, PIF firmware, Transfer Pak ROMs, and every other configured storage destination. Existing aliases are compared by filesystem identity when possible, and normalized path comparison catches direct path reuse. This prevents a save flush from overwriting an input image or another device's state.

Flushes write a temporary file in the destination directory, close it, and replace the destination only after the complete new image has been written. Windows uses a replace-existing move with write-through semantics; other hosts use a same-directory rename. If temporary-file creation, writing, or replacement fails, the runner reports the failure and leaves the previous destination untouched. Temporary files are removed after a failed replacement when possible.

The runner flushes configured storage on ordinary exit, test-ROM success or failure, CPU-stall exit, instruction-limit exit, and handled execution exceptions after system creation. A flush error changes the process result to failure. Paths with missing or unusable parent directories are reported instead of silently dropping data.

Tests cover create/write/destroy/recreate readback for every SRAM and EEPROM capacity, all seven Flash parts, Controller Pak contents, MBC1 and MBC2 RAM, cartridge RTC registers, and Game Boy RTC state. They also cover wrong sizes, malformed RTC records, input/storage collisions, replacement failure with preservation of the previous valid image, and a native Windows runner invocation using non-ASCII ROM, PIF, and save paths with spaces.
