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

## Automatic refresh

RI refresh-enable bit 17 allows a refresh request at each VI horizontal boundary.
The request closes the tracked rows without changing their stored data. Recovery
uses the dirty delay in bits 15-8 if an open row was dirty, or the clean delay in
bits 7-0 otherwise. Both delays count 62.5 MHz RCP cycles. A request arriving
during recovery is coalesced into the current refresh rather than restarting it.

Horizontal timing continues when video output is off, even though the vertical
counter stays at zero. H_SYNC resets to 2047, producing a 2048-video-clock
period until software programs it. The shared scheduler stops at horizontal
boundaries and refresh completion, so a large clock advance gives the same
recovery state as single-cycle advances.

Blocking CPU memory requests issued during recovery wait for its remaining
cycles before their nominal transfer delay. This includes uncached reads, cache
fills, dirty data-cache writebacks, and explicit instruction-cache fill and hit
writeback operations. The wait uses the physical transfer address after older
buffered stores have drained. Cache operations that only change tags or valid
bits, a hit writeback that misses, and chip-register transfers do not wait for
RDRAM recovery. Cache hits and device-register reads do not wait either.
Speculative instruction-fetch waits remain deferred until after the older
instruction samples its operands and device registers.

A blocking CPU transaction also checks the next horizontal boundary before it
starts its nominal response interval. If enabled refresh begins inside that
interval, the request includes the clean or dirty recovery time selected from
`RI_REFRESH`. This makes a large CPU clock advance produce the same completed
request and refresh state as one-cycle bus advances.

Disabling refresh prevents later requests but does not cancel a recovery already
in progress. Reset cancels recovery. The [RI hardware notes](https://n64brew.dev/wiki/RDRAM_Interface)
describe refresh control and delay fields; the [VI register notes](https://n64brew.dev/wiki/Video_Interface)
describe the horizontal period and its power-on value.

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
reversed the valid and dirty fields. `tests/rcp/test_ri_refresh.cpp` checks refresh
boundaries in both regions, clean and dirty delays, video-off behavior, tick-size
independence, reset, and blocking CPU transactions, including speculative fetches
and explicit instruction-cache transfers. The cache-operation cases check all
32 transferred bytes, Count advancement, both video regions, and operations that
do not access RDRAM storage. `tests/cpu/test_rdram_refresh_overlap.cpp` adds
in-flight refresh, Compare, and tick-partition coverage for blocking CPU
transfers.

Row-change delays and shared-memory arbitration remain incomplete. Buffered
stores and DMA transfers do not yet wait for refresh. Per-chip refresh-row
registers, detailed RAS/minimum-interval timing, multibank overlap, and the
optimize bit are not modeled. Coalescing requests under unusually short
horizontal periods is not hardware-validated. Unexpected negative
acknowledgements caused by deliberately desynchronizing RI and the chips are also
not modeled. See [CPU timing](cpu-timing.md) for the remaining optional timing
failures.
