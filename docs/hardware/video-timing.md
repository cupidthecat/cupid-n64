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

The duration and leap selection are sampled when a line begins. Writes to
H_SYNC or H_SYNC_LEAP become readable immediately but do not shorten, extend,
or replace the active line. The following line uses the new register values.
RI refresh uses the same latched deadline. Reset discards the pending period;
register writes before the first elapsed cycle configure that first line.

CURRENT reports the vertical counter in half-lines and includes the field bit.
An odd V_SYNC value produces progressive fields. An even value alternates the
field bit, so successive fields contain different numbers of complete scanlines.
The interrupt comparison follows that field phase. Odd comparator values match
the same scanline in both fields; even values account for the half-line offset.
The zero comparator also matches the final even half-line of an interlaced field.

The vertical line counter is nine bits, separate from the field bit. Its natural
512-line wrap preserves field parity and does not advance the five-field leap
pattern. A programmed V_SYNC restart performs those updates separately. At
the largest V_SYNC values, the counter can wrap before reaching the programmed
restart comparison. Changing V_SYNC or the interrupt target affects the next
line boundary; it does not restart the counter or raise a retroactive interrupt
at the write itself.

Writing CURRENT acknowledges the VI interrupt without changing the counter.
A zero video type resets the vertical line counter at the next horizontal
boundary and prevents new VI interrupts. It preserves the field bit and any
already pending interrupt, so CURRENT can remain 1 while blanked. A blank and
unblank sequence entirely within one line does not reset the counter. The
horizontal counter continues to drive [RI refresh](rdram-interface.md), and
re-enabling video resumes vertical counting at the next horizontal boundary.
Reset clears the counters, leap phase, and fractional clock state, initializes
the interrupt comparator to 256, and restores the 2048-clock horizontal period.

`tests/rcp/test_vi.cpp` checks region-specific boundaries, fractional clocks,
blanking, leap selection, field-dependent interrupts, reset, and zero periods.
`tests/vi/test_timing_registers.cpp` checks writes during normal and leap lines,
blanking on odd fields, short blank pulses, counter wrap, reset cancellation,
and refresh deadlines. Ten-field NTSC/PAL progressive and interlaced traces
compare line counts, interrupt edges, and leap phase under bulk and single-cycle
advances. These traces use short programmed horizontal periods to exercise the
regional clock conversion and field boundaries without assuming a broadcast
display mode.
Another trace changes VI registers at audio sample boundaries while SP DMA and
RI refresh are active, comparing counters, interrupts, sample clocks, audio
data, and bank state across CPU tick sizes.

Registered [video output callbacks](video-scanout.md) receive a field when the
vertical counter reaches `V_START >> 1`. Their event deadline participates in
RCP scheduling even without RI refresh. `tests/vi/test_output.cpp` checks exact
delivery clocks, progressive and interlaced ten-field traces in both regions,
start-register changes, blanking, and counter wrap. Bulk and single-cycle CPU
advances also produce matching fields while SP/SI DMA, RDP drawing, audio,
and refresh modify the machine state.

## Framebuffer fetches and RDRAM rows

While a 16- or 32-bit framebuffer type is selected, VI fills its line buffers
from RDRAM throughout every line. The model spreads one eight-byte word over
each `line period * 8 / (WIDTH * bytes per pixel)` RCP cycles. On lines before
V_START the fill reads the first source line; after the visible window it stays
on the last one. Within the window the source line follows Y_OFFSET and Y_SCALE
as in [field output](video-scanout.md), and the word address advances with the
fraction of the line that has elapsed.

These fetches are not scheduled as separate bus events. `src/vi/fetch.cpp`
computes the current fetch address and interval; `Bus::rdram_row_miss` applies
the fetches that fell between a bank's previous access and the current request
before deciding whether the requested row is open. The result is the same for
bulk and single-cycle clock advances. The fill leaves the framebuffer row open
in its 1 MiB bank, so an uncached CPU read of a different row in that bank
pays the row-open wait described in [CPU timing](cpu-timing.md#uncached-rdram-reads).
A read in another bank, or of the row VI is reading, does not.

The fetch address reproduces the cartridge suite's same-bank and other-bank
uncached-load medians. The exact burst length, the fill's line-buffer
occupancy, and the extra lines fetched in the anti-aliasing modes are not
measured; the model does not charge VI transfers against CPU or DMA bus time.
Updated measurements after line-duration latching are recorded in the
[VI timing results](../testing/vi-timing-results.md). `tests/cpu/test_rdram_rows.cpp`
covers the same-bank, other-bank, same-row, interval, disabled-type, and
tick-size cases.
