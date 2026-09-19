# Video timing

The video interface uses the region's video oscillator, not the RCP clock.
`Bus::tick` converts elapsed 62.5 MHz RCP cycles to 48.681818 MHz video clocks
for NTSC or 49.656530 MHz for PAL. Fractional clocks carry between calls, so
changing the scheduler's tick size does not change scanline timing.

The low 12 bits of H_SYNC specify a terminal count. An ordinary scanline lasts
that count plus one video clock. On the second scanline of each field, the
selected H_SYNC_LEAP value supplies the duration. H_SYNC's five-bit leap pattern
selects the low or high leap value, advancing one bit per field and repeating
after five fields. H_SYNC resets to 2047. Software-written zero timing values
are used directly; there is no fallback to a standard video mode.

CURRENT reports the vertical counter in half-lines and includes the field bit.
An odd V_SYNC value produces progressive fields. An even value alternates the
field bit, so successive fields contain different numbers of complete scanlines.
The interrupt comparison follows that field phase. Odd comparator values match
the same scanline in both fields; even values account for the half-line offset.
The zero comparator also matches the final even half-line of an interlaced field.

Writing CURRENT acknowledges the VI interrupt without changing the counter.
A zero video type holds CURRENT at zero and prevents VI interrupts. The
horizontal counter continues to drive [RI refresh](rdram-interface.md), and
re-enabling video resumes vertical counting at the next horizontal boundary.
Reset clears the counters, leap phase, and fractional clock state, initializes
the interrupt comparator to 256, and restores the 2048-clock horizontal period.

`tests/rcp/test_vi.cpp` checks region-specific boundaries, fractional clocks,
blanking, leap selection, field-dependent interrupts, reset, and zero periods.

VI framebuffer reads do not yet contend with CPU or other RCP memory requests.
The extended cartridge suite still reports the RDRAM timing limits described in
[CPU timing](cpu-timing.md). Correct scanline timing alone does not establish
accurate RDRAM arbitration.
