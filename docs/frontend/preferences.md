# Desktop preferences and automatic storage

The desktop frontend stores persistent settings through `cupid::host::Preferences`. The file format is versioned and length-delimited so an incomplete or newer file is rejected without partially changing the caller's settings.

Preferences v2 begins with `CUPID-PREFERENCES 2 <payload-bytes>` followed by a newline and the payload. The reader also accepts v1 files and gives their Controller Paks the original one-bank capacity. Saving writes v2, including each port's explicit bank selection and capacity from one through 62 banks. GameCube controller selections are retained alongside N64 gamepads and mice.

The complete file, including its header, is limited to 512 KiB. Individual strings are limited to 16 KiB. Paths are serialized as UTF-8 with quoted escaping; malformed UTF-8 and embedded NUL bytes are rejected on every platform. Volume must be finite and between 0 and 1. Input bindings remain an opaque string for the input mapper to validate.

`load_preferences` treats a missing file as an empty operation and leaves the supplied settings unchanged. Any malformed header, payload length, field, enum, path, or bound fails without mutating the supplied `Preferences`. `save_preferences` validates the complete object before writing and uses the shared atomic replacement helper, so a failed write leaves an existing valid preferences file intact.

The preferences destination must be separate from all selected input and device-storage files. Saving is rejected when the destination resolves to the cartridge ROM, PIF ROM, cartridge save or RTC, Controller Pak image, Transfer Pak ROM, Transfer Pak RAM, or Transfer Pak RTC file. Existing aliases are checked with filesystem equivalence; unresolved paths are compared after normalization. Windows comparisons are case-insensitive.

`prepare_storage_paths` derives automatic storage names only after a `System` has loaded the media. The top-level directory is `n64-<sha256>`, where the digest covers the normalized N64 ROM bytes already held by the system. Cartridge save, RTC, and Controller Pak files use stable names within that directory.

Transfer Pak RAM and RTC names include the SHA-256 digest of the Game Boy ROM bytes already loaded into the inserted `GameBoyCartridge`. The source path is no longer needed after insertion. Changing, deleting, replacing, or omitting that path cannot change the loaded cartridge's identity. Two loaded cartridges with identical bytes produce the same automatic storage identity even when they came from different paths; different loaded bytes produce different names.

Callers may provide explicit storage paths. Automatic preparation fills only paths that are empty and correspond to hardware actually present in the configured `System`, then runs the same storage collision checks used by the host storage layer before committing the prepared options.
