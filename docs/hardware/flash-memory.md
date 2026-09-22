# FlashRAM

`SaveType::FlashRam` provides a 128 KiB flash array. Select the chip before boot
with `Bus::set_flash_chip(FlashChip::...)`; the enum is declared in
`include/cupid/cartridge/flash.hpp`. The default is `Mx29L1100`.

| Chip | Enum value | Manufacturer/device ID | Array address unit |
| --- | --- | --- | --- |
| MX29L0000 | `Mx29L0000` | `00c2 0000` | Halfword |
| MX29L0001 | `Mx29L0001` | `00c2 0001` | Halfword |
| MX29L1100 | `Mx29L1100` | `00c2 001e` | Halfword |
| MX29L1101 A | `Mx29L1101A` | `00c2 001d` | Byte |
| MX29L1101 B | `Mx29L1101B` | `00c2 0084` | Byte |
| MX29L1101 C | `Mx29L1101C` | `00c2 008e` | Byte |
| MN63F81MPN | `Mn63F81Mpn` | `0032 00f1` | Byte |

Selecting a chip resets volatile flash state and cancels its operation timer,
but preserves the saved array. System reset preserves the chip selection.
The core does not infer a chip from the cartridge image.

## Bus access

For halfword-indexed chips, selecting cartridge address `0x08000040` starts at
byte `0x80` in the flash array. Each halfword advances the index by one, with the
low 14 bits wrapping. Byte-addressed chips start at byte `0x40` for that same
selection, advance by two, and wrap the low 15 bits. Both paths wrap within a
32 KiB burst window while preserving the upper address bits. PI page boundaries
start new address phases, so the programmed page size affects long reads.

The page buffer uses byte addresses and wraps within 128 bytes. Reads in page-load
mode return the buffer. Macronix loads replace the addressed buffer bytes;
MN63F81MPN loads combine them with bitwise AND. Programming combines each buffered
byte with the saved byte using bitwise AND, then resets the buffer to `0xff`.
Erase restores bits to one.

Commands arrive as two halfwords at `0x08010000` and `0x08010002`. CPU stores and
PI DMA use the same command path. Selecting another address discards an incomplete
command. Writes elsewhere in that 64 KiB range do not issue commands.

## Commands and status

| Command | Behavior |
| --- | --- |
| `0x3c` | Prepare a chip erase. |
| `0x4b` | Prepare a 16 KiB sector erase; the low 10 bits select a page within the sector. |
| `0x78` | Start the prepared erase. Without preparation, leave the chip unchanged. |
| `0xa5` | Program the 128-byte page selected by the low 10 bits. |
| `0xb4` | Select the page buffer. |
| `0xd2` | Select status; Macronix requires two consecutive status commands, MN63F81MPN requires one. |
| `0xe1` | Select silicon identification. |
| `0xf0` | Select the flash array. |

Silicon identification begins with `1111 8001`, then the manufacturer and device
IDs above. Macronix repeats all four halfwords. MN63F81MPN repeats only its device
ID after the first four. A new PI address phase restarts that sequence.

Status repeats as a halfword. Bit 7 reports ready, bit 1 reports erase busy, and
bit 0 reports program busy. Macronix keeps bits 2 and 3 set: idle status is
`0x008c`, programming reports `0x000d`, and erasing reports `0x000e`. Entry into
status mode returns the previous flash read for one halfword before current
status appears. MN63F81MPN status reads have no stale halfword.

MN63F81MPN powers up with status `0x0080`. Program completion sets bit 2 and erase
completion sets bit 3. These success bits accumulate until a data write in
status mode clears both; the write leaves ready/busy bits intact. Reset clears
both success bits. Commands at the command address retain their normal decoding.

Operation deadlines use the 62.5 MHz RCP clock:

| Chip family | Program page | Erase sector | Erase chip |
| --- | --- | --- | --- |
| Macronix | 218,750 cycles (3.5 ms) | 5,312,500 cycles (85 ms) | 5,312,500 cycles (85 ms) |
| MN63F81MPN | 18,750 cycles (0.3 ms) | 17,500,000 cycles (280 ms) | 18,750,000 cycles (300 ms) |

Commands received while busy are ignored. Completion restores ready and clears
the busy bits without raising a PI interrupt. Reset cancels the timer and erase
preparation, clears the page buffer and volatile status, and preserves saved
bytes. A cancelled operation cannot set completion flags later.

A status read with cartridge offset bit 17 set stops driving the bus. That state
persists across address phases until the next command. Reads then follow the
[PI latch rules](cartridge-bus.md).
MN63F81MPN also stops driving silicon-ID reads when offset bit 17 is set;
Macronix identification reads remain driven at those addresses.

## Validation and limits

The regressions in `tests/rcp/test_flash.cpp` cover identity, address units, burst
wrap, page-buffer contents, programming, both erase modes, status transitions,
command delivery through DMA, busy deadlines, CPU/RCP clock conversion, and
reset. The save-memory regression in `tests/test_bus.cpp` reads back a complete
programmed page through PI DMA.

`tests/rcp/test_flash_chips.cpp` checks all seven chips through CPU and PI bus
accesses. Its eleven regressions cover IDs and burst tails, PI page reselection,
address units and wrapping, page-buffer writes, programmed readback, status entry,
all operation deadlines, success bits, open bus, rejected busy commands, reset,
and changing the configured chip without replacing storage.

The model applies array changes when an operation starts and delays its ready
status. Reset after an operation starts retains that array result; reset before
erase execution discards the preparation without changing the array. This does
not model partial programming or erasing after interrupted power. Independent
power-cut captures and a cartridge compatibility corpus are still needed; issue
#32 remains open for those checks.
