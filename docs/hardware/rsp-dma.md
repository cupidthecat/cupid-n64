# Signal processor DMA registers

SP_MEM_ADDR and SP_DRAM_ADDR have separate state for programming and readback.
Writes update the pending transfer. Reads expose the current transfer's
addresses, including its progress and final postincrement values. Both
addresses are aligned to eight bytes; SP_MEM_ADDR also selects DMEM or IMEM.

Writing SP_RD_LEN or SP_WR_LEN sets the pending length, row count, skip, and
direction. If the DMA engine is idle, that transfer becomes current immediately.
Otherwise it remains pending and SP_DMA_FULL is set. Address writes while a
transfer is pending change the addresses used when it becomes current. They
do not alter the active transfer or its readback.

Completing a transfer advances only its current addresses. It preserves the
programmed addresses for a later length write, including addresses written
while the engine was busy. A repeated length write without new address writes
therefore transfers from the previously programmed locations again. Software
that wants to continue from the readback addresses must write those addresses.

Each row wraps inside the selected 4 KiB SP memory bank. The RDRAM address
advances through the row and adds the skip between rows. The completed length
reads as `0xff8`. Promoting a queued transfer replaces the readback with that
transfer's starting state and clears SP_DMA_FULL while SP_DMA_BUSY remains set.

`tests/rsp/test_dma.cpp` checks repeated length writes, addresses staged while
busy, and address changes after queuing. The cases cover both transfer directions
and both SP memory banks. An encoded RSP program stages addresses with MTC0 and
reads current addresses with MFC0 before and after completion; CPU advances in
bulk and one cycle at a time produce the same data and register values. Existing
tests also cover row count, skip, alignment, bank wrap, and queued postincrements.

DMA visibility remains row-based. Shared RDRAM arbitration and finer transfer
timing are tracked separately under issues #4 and #15. See
[RCP event scheduling](rcp-scheduling.md) for instruction and device ordering.
