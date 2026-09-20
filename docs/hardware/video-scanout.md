# Video field snapshots

`Bus::scan_video()` returns a field decoded from the current VI registers and
installed RDRAM. It is a snapshot: calling it does not advance the VI, perform
DMA, change memory-bank state, or retain pixels from an earlier call. The
implementation lives in `src/vi/scanout.cpp`, `filter.cpp`, and `gamma.cpp`.

`VideoField` contains 640 by 240 pixels for NTSC or 640 by 288 for PAL. Pixels
are packed RGBA8888 with alpha 255. Areas outside the display window are black.
`field` copies the current VI field bit; `interlaced` reports the control
register's serrate bit. Each array row represents one scanline of that field.
The API does not weave two fields or duplicate progressive rows.

The decoder handles these register inputs:

| Input | Behavior |
| --- | --- |
| Type | Blank and reserved types produce black. Types 2 and 3 fetch 16-bit and 32-bit pixels. |
| Origin | Aligns down to the pixel size. Fetches wrap at the installed RDRAM size. |
| Width | Sets the source row stride in pixels; it does not clip source X. |
| Horizontal window | Positions the active span relative to NTSC clock 108 or PAL clock 128. |
| Vertical window | Positions field rows relative to NTSC halfline 34 or PAL halfline 44. |
| X/Y scale | Uses 10 fractional bits, including the programmed starting offsets. Zero increments repeat a source coordinate. |
| Antialias mode | Modes 0 and 1 reconstruct partial coverage and resample. Mode 2 resamples without coverage reconstruction; mode 3 replicates pixels. |

RGBA16 color components expand to eight bits with their low three bits zero.
RGBA32 supplies the upper three bytes of each word. The stored coverage bits
do not become output alpha. RGBA16 coverage combines the visible low bit with
the two hidden RDRAM bits. RGBA32 uses bits 7 through 5 of its low byte. Modes
2 and 3 force full coverage at the filter input.

Horizontal guard bands discard the first eight and final seven output pixels
when the corresponding programmed endpoint lies within the visible interval.
An endpoint beyond that interval is clipped without adding its guard band.
Clipping preserves the source coordinates, so an offscreen start still advances
the X or Y sample position. Empty and reversed windows produce black.

Resampling takes the high five fractional bits of each coordinate. It rounds
the vertical interpolation of both source columns first, then rounds their
horizontal interpolation. Replication discards the fractional parts.

## Filter order

Partial-coverage pixels use six neighboring samples: the upper and lower
diagonals and the pixels two positions to either side. Only fully covered
neighbors contribute. Each channel tracks its second-lowest and second-highest
values, with the center counted twice. The correction is proportional to
`7 - coverage`, divided by eight with rounding.

For fully covered pixels, control bit 16 enables dither restoration instead.
It compares the center's five-bit channel values with the eight surrounding
pixels. Each difference contributes -1, 0, or 1. Their sum adjusts the center
after clearing its low three color bits. Partial-coverage pixels skip this
step. Writes retain control bit 16 for filtering; CPU register readback retains
its existing low-16-bit behavior.

Divot filtering follows reconstruction. When any of three adjacent samples
has partial coverage, each output channel takes the median of the three
filtered channel values. Three fully covered samples bypass this step.

When Y scaling repeats a source row, the final repetition before advancing
can use different lower samples during interpolation. Coverage reconstruction
substitutes the same-row samples two pixels away for the lower diagonals.
Dither restoration replaces the three lower neighbors with the two immediate
horizontal neighbors. The first visible output row does not use this path.

Gamma follows resampling. Its eight-bit output is twice the integer square
root of `channel * 64`. Gamma dithering adds a six-bit value before taking the
root. With gamma disabled, dithering adds a single bit per channel and
saturates at 255. RGB noise uses distinct, partly shared bit slices.

Noise is a reproducible hash of field sequence and sample position. Repeated
snapshots within a field agree; vertical-counter wrap advances the sequence,
and reset clears it. This does not reconstruct the hardware generator's exact
sequence or clock phase.

## Current limits

Snapshots read the installed RDRAM bytes directly. They do not model chip
remapping, VI fetch traffic, arbitration, line-buffer retention, or register
latching during a field. A write between calls changes the next snapshot;
this does not establish when hardware would latch that write. Timed field
delivery, odd-halfline boundary behavior, and filtering across mid-field
register changes still need validation. Issues #23 and #24 track this remaining
work. Hardware captures are also needed to check noise correlations and the
repeated-row behavior independently.

## Regression coverage

`tests/vi/test_scanout.cpp` checks both formats, NTSC/PAL placement, black
borders, guard bands, clipping, fixed-point offsets, zero scales, replication,
interpolation order and rounding, origin alignment, 4/8 MiB wrapping, source
stride, field metadata, independent snapshots, and preservation of VI and
memory-bank state. These tests exercise the core API without a frontend.

`test_filter.cpp` checks coverage weighting, neighbor eligibility, hidden bits,
negative corrections, divot medians, restoration, repeated rows, gamma-table
addresses, and noise-channel correlations. `test_filter_scanout.cpp` checks
register control, filter order, both framebuffer sizes, repeated-row selection,
field-dependent noise, reset, black borders, and memory-state preservation.
