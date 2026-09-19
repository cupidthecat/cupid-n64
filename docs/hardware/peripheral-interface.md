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

The calculation uses the starting address and length before the transfer updates
the PI registers. It returns RCP cycles, so CPU-driven advancement reaches the
same deadline through the system's 2:3 clock conversion. Completion clears DMA
busy and raises the PI and MI interrupt flags. Reset cancels the pending
completion.

Data copies occur when DMA starts; only busy status and the completion interrupt
are delayed. The model does not expose individual cartridge bus beats or account
for shared RDRAM contention. CPU cartridge I/O uses a separate timing path.

`tests/rcp/test_pi.cpp` checks field sensitivity, domain selection in both DMA
directions, page and buffer boundaries, halfword rounding, clock conversion,
reset cancellation, register masks, and payload preservation.
