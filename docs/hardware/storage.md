# Persistent host storage

The runner loads persistent device images after hardware selection and before cartridge execution. Missing storage files leave each device's fresh contents in place and are created when the runner shuts down. Existing files must have the exact size for the selected hardware; a short or oversized image is rejected before execution.

## File formats

Cartridge SRAM is stored as 32, 96, or 128 KiB of raw bytes. Fresh SRAM is zero-filled. EEPROM is stored as 512 bytes for the 4 Kbit part or 2048 bytes for the 16 Kbit part, with fresh bytes set to `0xff`. FlashRAM is a raw 128 KiB image for all supported Flash parts and also starts at `0xff`.

Controller Pak files contain the complete configured capacity in raw bank
order. Each bank is 32 KiB, so the default one-bank Pak uses a 32 KiB file and
`--pak-banks PORT:COUNT` requires exactly `COUNT * 32768` bytes. Bank zero comes
first, followed by each higher bank in numeric order. A missing Pak image starts
with every configured bank zero-filled. In the core,
`Bus::controller_paks[port]` stores the same bytes as one flat vector in bank
order. Programmatic callers choose its capacity with
`Bus::configure_controller_pak` before loading bytes. Transfer Pak RAM files use
the exact RAM span supplied by the configured Game Boy cartridge. This includes
the 256-byte MBC2 nibble store and the mapper-specific external RAM sizes
accepted by the cartridge model.

`--rtc-file` stores the cartridge RTC's 32 retained register bytes without a host structure wrapper. `--transfer-rtc-file` uses a 12-byte portable record: bytes 0-3 are `GBRT`, byte 4 is version 1, bytes 5-7 are seconds/minutes/hours, bytes 8-9 are the 9-bit day counter in big-endian order, byte 10 contains halt and carry in bits 0 and 1, and byte 11 is reserved as zero. Wrong lengths, headers, versions, reserved bits, or day ranges are rejected.

## Paths and replacement

Storage destinations must be distinct from the N64 cartridge, PIF firmware, Transfer Pak ROMs, and every other configured storage destination. Existing aliases are compared by filesystem identity when possible, and normalized path comparison catches direct path reuse. This prevents a save flush from overwriting an input image or another device's state.

Flushes reserve a temporary file exclusively in the destination directory before writing it. Windows uses `CREATE_NEW` while opening reparse points themselves rather than following them; POSIX hosts use `O_CREAT|O_EXCL`. Existing regular files, directories, and dangling symbolic links therefore make that temporary name unavailable instead of being truncated or followed. Up to 100 numbered temporary names are tried before the flush fails safely.

The complete payload is written to the reserved handle, flushed (`FlushFileBuffers` on Windows or `fsync` on POSIX), and the handle close is checked before replacement. Windows then uses a replace-existing move with write-through semantics; transient access, sharing, or lock collisions from another publisher are retried for up to 100 one-millisecond attempts. POSIX hosts use a same-directory rename. If reservation, writing, flushing, closing, or replacement fails before a successful rename, the runner reports the failure and leaves the previous destination untouched. Temporary files are removed after failed writes or replacement when possible.

Separate emulator processes are not locked against one another and their saves are not merged. Exclusive temporary reservation prevents them from sharing or truncating the same staging file; each completed rename publishes one whole image, so the last successful replacement wins rather than producing a torn mixture. The POSIX implementation flushes the temporary file contents but does not separately `fsync` the parent directory, so persistence of the renamed directory entry across sudden power loss remains dependent on the host filesystem and operating system. Windows relies on the documented behavior of `MOVEFILE_WRITE_THROUGH` for the final replacement.

The reusable file-publication primitive is `cupid::storage::replace_file(path, bytes, error)` from `cupid/storage/file.hpp`. Device-specific storage code supplies format, collision, and size policy around that helper instead of duplicating the exclusive reservation and atomic replacement sequence.

The runner flushes configured storage on ordinary exit, test-ROM success or failure, CPU-stall exit, instruction-limit exit, and handled execution exceptions after system creation. A flush error changes the process result to failure. Paths with missing or unusable parent directories are reported instead of silently dropping data.

The storage regressions include create/write/destroy/recreate readback for SRAM
and EEPROM capacities, all seven Flash parts, single- and multi-bank Controller
Pak images, MBC1 and MBC2 RAM, cartridge RTC registers, and Game Boy RTC state.
They also exercise wrong sizes, malformed RTC records, input/storage collisions,
replacement failure with preservation of the previous valid image, occupied and
dangling temporary paths, concurrent same-destination replacement, and a native
Windows runner invocation using non-ASCII ROM, PIF, and save paths with spaces.
