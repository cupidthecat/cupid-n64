# RDRAM interface state

RI tracks eight 1 MiB banks in the bottom 8 MiB of the memory map. Each bank has
one open 2 KiB row, a valid bit, and a dirty bit. Memory reads open a clean row
when the row changes. Writes mark the open row dirty; reads of that same row
leave it dirty. The tracking uses the bus address, even when a chip's device ID
maps its storage elsewhere.

`RI_BANK_STATUS` exposes valid bits in 7-0 and dirty bits in 15-8. Writing any
value clears all valid bits and sets all dirty bits, giving `0xff00`. Figure 37H
of the [hardware register description](https://patentimages.storage.googleapis.com/cc/33/96/6e54e1628ec0f9/US6593929.pdf)
defines this layout and write action.

CPU cache fills, writebacks, uncached accesses, and DMA transfers share this
tracking through the RDRAM access path. A CPU cache hit does not reach RI.
Modifying a cached line therefore does not dirty the corresponding RDRAM row
until the line is written back.

## Errors

An access without a responding chip latches the missing-acknowledgement bit in
`RI_ERROR`, including accesses to absent expansion memory after normal RAM
initialization. Accesses from `0x00800000` through `0x03efffff` also latch the
over-range bit. These accesses still reach a chip if its device ID maps it there,
but they do not change the eight tracked banks. Register-space accesses do not
set the over-range bit.

Errors remain latched across successful accesses. Any write to `RI_ERROR` clears
them; the missing-acknowledgement bit also appears in the unintended readback of
`RI_CURRENT_LOAD`. The [RI hardware notes](https://n64brew.dev/wiki/RDRAM_Interface)
describe the address limits and error behavior.

## Coverage and limits

`tests/rcp/test_ri.cpp` covers all eight banks, row boundaries, dirty-state
transitions, cache hits and writebacks, DMA memory paths, relocated chips,
absent memory, error clearing, and reset. The older memory-bus regression now
expects `0xff00` after a bank-status write; its previous `0x00ff` expectation
reversed the valid and dirty fields.

This bookkeeping does not yet model refresh, row-change delays, or memory
arbitration. Unexpected negative acknowledgements caused by deliberately
desynchronizing RI and the chips are also not modeled. The extended cartridge
suite's memory-timing failures remain open; see [CPU timing](cpu-timing.md).
