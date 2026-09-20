# Serial interface

The SI connects the RCP to the PIF. Its registers occupy `0x04800000` through
`0x048fffff`, with the register selection repeated every 32 bytes. The CPU can
also access the PIF image through `0x1fc00000` through `0x1fcfffff`. That window
mirrors the PIF's 2 KiB address space.

## DMA

Writing SI_PIF_ADDR_RD64B starts a 64-byte transfer from the programmed PIF
address to RDRAM. SI_PIF_ADDR_WR64B transfers in the opposite direction. The
RDRAM address is aligned to eight bytes; the PIF address register clears bit 0,
and each word transfer ignores the low two address bits. Transfers wrap within
the PIF image. Reads of locked boot ROM return zero, and writes leave boot ROM
intact. PIF RAM remains accessible after ROM lockout.

Write completion handles the PIF control byte. Control bit zero configures
retained Joybus channel descriptors and clears itself; it does not execute the
device commands. Read completion executes the retained requests, or a pending
CIC challenge, before copying the reply image to RDRAM. This prevents packet
upload from consuming input or changing accessory state before the read phase.
CPU PIF stores can also configure descriptors; ordinary CPU reads do not execute
the channel list. See [Joybus configuration and execution](joybus.md).

[PIF boot control](pif-boot.md) observes lockout, checksum, and termination bits
on separate clock boundaries. SI write completion does not execute those stages
immediately. Security failure stops Joybus handling, though the current SI model
still completes transfers and copies PIF RAM.

The model completes writes after 4,065 RCP cycles. Read timing starts at 13,600
RCP cycles and adds time for each Joybus packet: 22,000 cycles for a connected
controller, 18,000 for an absent controller, or 20,000 for the cartridge channel.
Padding, skipped channels, channel resets, and the end marker each add 1,420
cycles. Parsing stops at the end marker, the end of the 64-byte packet, or after
the fifth channel. The length fields exclude their status flags.

[Joybus packet handling](joybus.md) describes skip/reset flags, reply lengths,
error flags, controller-port isolation, and the PIF control-byte boundary.

These delays estimate serial transactions; they do not simulate each Joybus bit
or accessory-specific response time. The DMA status reports PCH/DMA states 4/1
for reads and 1/4 for writes. Completion clears those states and the busy bit,
then raises the SI interrupt in MI.

## CPU stores and the I/O latch

A CPU store into the PIF window sets both busy bits, records the word on the
serial bus, and reports PCH/DMA states 11/9. The transfer completes after 2,150
RCP cycles and raises the SI interrupt. Further stores while I/O is busy are
ignored. Byte and halfword stores use the same word-lane expansion as other
RCP registers.

A PIF read while I/O is busy returns the pending store word through the selected
byte lane. It releases the I/O latch and cancels that store's completion event.
The DMA busy bit and phase fields remain until another transfer completes.
Subsequent reads access the PIF image normally.

Writing SI_STATUS acknowledges the interrupt without cancelling a pending
transfer. Reset clears both transfer timers and the I/O latch.

`tests/rcp/test_si.cpp` checks transfer data, address wrapping, ROM lockout,
status phases, completion boundaries, interrupt acknowledgement, and latch
behavior. `tests/rcp/test_address_map.cpp` covers the physical address windows
and bus stalls on unmapped accesses.
`tests/rcp/test_pif_channels.cpp` checks the separate write/read boundaries and
retained configuration through actual SI transfers.
