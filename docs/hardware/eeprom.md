# Cartridge EEPROM

`SaveType::Eeprom4K` selects 512 bytes of storage; `SaveType::Eeprom16K` selects
2,048 bytes. Both respond on Joybus channel 4. Other save types and unsupported
array sizes leave EEPROM commands unanswered.

## Addressing and commands

Commands `0x00` and `0xff` return `00 80` for a 4 Kbit chip or `00 c0` for a
16 Kbit chip, followed by a busy byte. Bit 7 of that byte is set while a write
is pending. A short response returns the requested prefix; longer responses
pad the three-byte status with zeros. The `0xff` command reports status without
cancelling a write.

Read command `0x04` takes a one-byte block address and returns the requested
number of bytes. Write command `0x05` takes a block address followed by data and
returns one status byte: `0x00` when accepted or `0x80` while busy. The block
address selects an eight-byte boundary. Extra write-response bytes are zero;
bytes outside the requested response remain untouched. A 4 Kbit chip decodes
six block bits, so blocks `0x00`, `0x40`, `0x80`, and `0xc0` alias. A 16 Kbit chip decodes all
eight bits. Transfers wrap at the installed chip's byte capacity, including
requests longer than eight bytes.

Reads need a command and block address. Writes also need space for at least one
response byte. Short writes replace only the supplied data bytes. A write with
no data still starts the busy interval. Unsupported commands and requests
missing these fields receive the PIF no-response flag and do not change storage.

## Busy interval and reset

An accepted write changes the array immediately and remains busy for 375,000
RCP cycles, or 6 ms at 62.5 MHz. Reads return `0xff` during that interval.
Rejected writes preserve both the original data and its deadline. Completion
does not raise an interrupt.

The busy interval starts when the PIF executes the command during the read
phase. Uploading a packet only configures its channel. If SI read completion
starts a write, that write cannot consume time from the transfer that delivered
it. Conversely, a busy interval ending at the same clock as an SI command
expires before the command checks status. Bulk and single-cycle advances use
the same order, including advances driven by the CPU clock.

System reset cancels the timer and retains the array. This is the core's reset
model; it does not establish which cells survive interrupted programming after
physical power loss. Save files and readback across separate machine instances
remain outside this implementation.

## Validation

`tests/rcp/test_eeprom.cpp` covers both capacities, all 256 block addresses,
boundary-crossing transfers, response lengths, busy rejection, exact deadlines,
SI completion ordering, reset, and absent or malformed requests. Protocol tests
execute the Joybus module directly; SI tests configure through a PIF write and
execute through a read, preserving the full busy interval after that read. The existing EEPROM
regression in `tests/test_bus.cpp` also checks busy reads and later readback.

Issue #31 remains open for game-level persistence checks and broader save-memory
validation. SI transaction durations are still estimates; see
[serial-interface timing](serial-interface.md).
