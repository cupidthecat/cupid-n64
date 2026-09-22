# Peripheral interface timing

PI DMA completion uses the bus timing registers selected by the starting cartridge
address. Addresses in `0x05000000..0x05ffffff` and `0x08000000..0x0fffffff` select
domain 2; other addresses select domain 1. Both domains have 8-bit latency and
pulse-width fields, a 4-bit page-size field, and a 2-bit release-duration field.

The timing calculation rounds the programmed length to a whole number of
halfwords. A page contains `2^(PGS + 2)` bytes. Each page touched contributes
`15 + LAT` RCP cycles, and each halfword contributes `PWD + RLS + 2` cycles.
Buffer overhead adds 28 cycles for each counted full buffer and one cycle per
partial byte:

- A transfer within one page counts one buffer if it is exactly 128 bytes;
  otherwise it counts all bytes as partial.
- A transfer spanning pages counts a buffer for each fully covered boundary
  page. Partial boundary pages contribute their transferred byte counts.
  Interior pages contribute one buffer per 128 bytes, rounded down in total.

The calculation uses the starting address and length before transfer progress
updates the PI registers. It returns RCP cycles, so CPU-driven advancement reaches
the same deadlines through the system's 2:3 clock conversion. Completion clears
DMA busy and raises the PI and MI interrupt flags. Reset cancels the remaining
transfer and completion event while preserving data that already crossed a
progress boundary.

Payload visibility follows the same cartridge timing instead of appearing at DMA
start. Cartridge-to-RDRAM DMA fills the PI's 128-byte internal buffer over the
programmed cartridge pages and exposes a block to RDRAM only when that block's
transfer deadline is reached. RDRAM-to-cartridge DMA samples RDRAM at each
scheduled cartridge segment, so writes made after an earlier segment cannot
retroactively change it. Page boundaries reselect the cartridge device at the
time that boundary is reached. PI address registers therefore expose only the
progress already made by the sampled RCP clock.

Only `PI_STATUS` accepts PI register writes while DMA or cartridge I/O is busy.
Other PI register writes set the error bit and leave the active transfer
unchanged. CPU cartridge reads and writes continue through the ordinary PI bus
path while DMA is busy: they can replace the bus latch, selected device, and
visible cartridge-address register, and CPU writes enter the usual I/O-busy
interval. The DMA keeps its own transfer cursor and reselects that cursor when it
next reaches a progress boundary, so an intervening CPU transaction does not
retarget the remaining payload. The status reset bit aborts pending progress and
clears the error bit; interrupt acknowledgement is independent.

Intermediate deadlines distribute the calculated buffer-overhead budget by the
number of transferred bytes. This preserves the total programmed duration and
ordered payload visibility; it is not a measurement of each internal bus edge.
The model schedules page and internal-buffer progress rather than every individual
halfword edge, and it does not yet arbitrate shared RDRAM bandwidth.

[Cartridge bus transactions](cartridge-bus.md) describes address selection,
sequential halfwords, the bus latch, and SRAM windows. Payload transfers reselect
the cartridge device at the same domain page boundaries used by the timer.

`tests/rcp/test_pi.cpp` checks field sensitivity, domain selection in both DMA
directions, page and 128-byte buffer boundaries, halfword rounding, clock
conversion, progressive payload sampling, reset/abort behavior, busy writes,
CPU cartridge I/O during DMA, register masks, status, and interrupt delivery.
