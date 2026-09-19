# FlashRAM

`SaveType::FlashRam` models the 128 KiB Macronix MX29L1100. Its silicon-ID
sequence is `1111 8001 00c2 001e`. Other FlashRAM chips can use different address
units, command sequences, and completion times; they are not selected by this
save type.

## Bus access

Array reads use a halfword index. Selecting cartridge address `0x08000040`
starts at byte `0x80` in the flash array. Each subsequent halfword advances the
internal index by one. The low 14 index bits wrap within a 32 KiB burst window.
PI page boundaries still start new address phases, so the programmed page size
affects long reads.

The page buffer uses byte addresses and wraps within 128 bytes. Reads in page-load
mode return the buffer. Programming combines each buffered byte with the saved
byte using bitwise AND, then resets the buffer to `0xff`. Erase restores bits to
one.

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
| `0xd2` | Select status after two consecutive status commands. |
| `0xe1` | Select the repeating four-halfword silicon-ID sequence. |
| `0xf0` | Select the flash array. |

Status repeats as a halfword. Bit 7 reports ready, bit 1 reports erase busy, and
bit 0 reports program busy. Bits 2 and 3 remain set for this chip. Idle status is
`0x008c`; programming reports `0x000d`, and erasing reports `0x000e`. Entry into
status mode returns the previous flash read for one halfword before current
status appears.

Programming takes 218,750 RCP cycles (3.5 ms), and erasing takes 5,312,500 RCP
cycles (85 ms). Commands received while busy are ignored. Completion restores
ready and clears the busy bits without raising a PI interrupt. Reset cancels
the operation's timer and clears volatile state while preserving saved bytes.

A status read with cartridge offset bit 17 set stops driving the bus. That state
persists across address phases until the next command. Reads then follow the
[PI latch rules](cartridge-bus.md).

## Validation and limits

The regressions in `tests/rcp/test_flash.cpp` cover identity, address units, burst
wrap, page-buffer contents, programming, both erase modes, status transitions,
command delivery through DMA, busy deadlines, CPU/RCP clock conversion, and
reset. The save-memory regression in `tests/test_bus.cpp` reads back a complete
programmed page through PI DMA.

The model applies array changes when an operation starts and delays its ready
status. It does not model partial programming or erasing after interrupted power.
