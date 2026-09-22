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

## Packed data transfers

Ordinary one-, two-, four-, and eight-byte transfers align the bus address down
to the transfer width before row tracking and address translation. Multi-byte
data is stored in the RDRAM byte array in big-endian order. The endian helpers
use byte-safe copies and an explicit native-endian conversion, so the packed
path does not depend on host pointer alignment or type aliasing.

This changes only how the bytes are moved. Row tracking still happens before
translation, so an unmapped request keeps the same RI error and bank-state
ordering. Device-ID remapping still selects the physical chip before accessing
the backing bytes. Reads through a non-identity mapping still pass the assembled
value through the chip-current reliability model; the direct identity mapping
is available only when the installed, enabled chips meet its mapping and current
requirements.

Hidden memory keeps its existing rules. Normal halfword and wider writes copy
the low data bit of each halfword into its hidden pair, while byte writes retain
their address-dependent single-bit behavior. EBUS reads and writes continue to
use the hidden-bit packing path for every supported width. The packed-transfer
regressions check backing-byte order, align-down behavior, remapping, low-current
reads, failed translations, and hidden/EBUS results independently.

## Open rows and request timing

A chip answers a request to its open row directly. A request to a closed or
different row makes the chip close the current row and load the requested one
before it can answer. RI knows the tracked row state in advance, so it holds
the request for that time instead of retrying. `Rdram::row_open` reports whether
an address matches the bank's open row, and `Bus::rdram_row_miss` combines that
with the [VI line-buffer fill](video-timing.md#framebuffer-fetches-and-rdram-rows),
which keeps the framebuffer row open in its bank. The tracking stamps each bank
with the RCP cycle of its latest access so the fill can be applied lazily.

Uncached CPU reads add the row-open wait described in
[CPU timing](cpu-timing.md#row-open-wait). Refresh closes every row, so the first
request to each bank after a refresh opens its row again. Cache refills and
writebacks, buffered stores, and DMA transfers update the row state but keep
their existing waits.

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

A cache refill or writeback also checks the next horizontal boundary before it
starts its nominal response interval. If enabled refresh begins inside that
interval, the request includes the clean or dirty recovery time selected from
`RI_REFRESH`. The recovery delay is converted at the CPU/RCP phase after the
nominal response interval, rather than reusing the request phase. This makes a
large CPU clock advance produce the same completed request and refresh state as
one-cycle bus advances. A single-word uncached read that is already in flight
completes first; see [CPU timing](cpu-timing.md#uncached-rdram-reads) for the
measurement behind that distinction.

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
absent memory, error clearing, reset, and the `0xff00` bank-status result after a
bank-status write. `tests/rcp/test_ri_refresh.cpp` checks refresh
boundaries in both regions, clean and dirty delays, video-off behavior, tick-size
independence, reset, and blocking CPU transactions, including speculative fetches
and explicit instruction-cache transfers. The cache-operation cases check all
32 transferred bytes, Count advancement, both video regions, and operations that
do not access RDRAM storage. `tests/cpu/test_rdram_refresh_overlap.cpp` adds
in-flight refresh, Compare, and tick-partition coverage for blocking CPU
transfers.

`tests/cpu/test_rdram_rows.cpp` covers the row-open wait, VI fetches, and
tick-size independence of the lazily applied fill.

Shared-memory arbitration remains incomplete: requesters do not wait for each
other's bus time. Buffered stores and DMA transfers do not yet wait for refresh
or for a closed row. Per-chip refresh-row registers, the longer close time of
a dirty row, detailed RAS/minimum-interval timing, multibank overlap, and the
optimize bit are not modeled. Coalescing requests under unusually short
horizontal periods is not hardware-validated. Unexpected negative
acknowledgements caused by deliberately desynchronizing RI and the chips are also
not modeled. See [CPU timing](cpu-timing.md) for the remaining optional timing
failures.
