# Cartridge bus transactions

The PI selects a cartridge device during an address phase. That phase loads the
low 16 address bits into both halves of the bus latch. Each responding read
replaces the latch with the returned halfword. If no device responds, the latch
keeps its previous value.

A CPU read selects an address once and receives two consecutive halfwords.
Subword loads select their byte or halfword from the assembled word. For example,
an unmapped word read at `0x05001234` returns `0x12341234`. A read that starts at
the last ROM halfword repeats that halfword when the next beat exceeds the image.

DMA selects the starting cartridge address and selects again at each programmed
PI page boundary. Crossing an internal 128-byte DMA buffer does not select a new
address. Both directions use the page-size register for the starting address's
domain. A DMA write updates the latch even if no device accepts the data. CPU
stores retain their full 32-bit latch while I/O is busy.

## SRAM addressing

A 32 KiB SRAM mirrors its address when the PI selects it. Sequential beats stop
at the end of that selected memory window. Reads retain the last driven value;
writes beyond the window leave memory unchanged. A later PI address phase can
select a mirrored address at the start of SRAM.

Larger SRAM images use 32 KiB banks spaced 256 KiB apart in the cartridge address
window. Each bank responds only in its first 32 KiB. The gaps and absent banks
leave the PI latch unchanged.

[FlashRAM](flash-memory.md) has its own burst counter, command decoder, page
buffer, and timed status transitions on the same halfword bus.

## Implementation and validation

`src/cartridge/pi_bus.cpp` handles device selection, sequential halfword access,
and CPU cartridge transactions. `tests/rcp/test_pi_bus.cpp` exercises open-bus
reads, both DMA directions, page and buffer boundaries, ROM exhaustion, SRAM
mirrors, bank selection, and unmapped bank gaps through the public bus interface.

DMA payload copies still occur at transfer start. The separate PI completion
timer controls busy status and interrupt delivery. Cartridge response timing
and shared RDRAM arbitration remain incomplete.
