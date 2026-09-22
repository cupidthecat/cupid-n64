# Video field output

`Bus::scan_video()` returns a field decoded from the current VI registers and
installed RDRAM. It is a snapshot: calling it does not advance the VI, perform
DMA, change memory-bank state, or retain pixels from an earlier call. The
implementation lives in `src/vi/scanout.cpp`, `filter.cpp`, and `gamma.cpp`.

Call `Bus::set_video_output(callback)` to receive fields as emulation advances.
Pass an empty callback to stop delivery. At each active line boundary, the
vertical counter is compared with `V_START >> 1`.
A match decodes the current registers and framebuffer and passes an owned
`VideoField` to the callback. The callback can move that field into storage;
later emulation cannot overwrite its pixels. The delivery boundary is the
start of the programmed vertical window, not the interrupt comparator or
the end of the field.

The scheduler stops at VI line boundaries while a callback is registered and
video is active, including when RI refresh is disabled. Callbacks observe the
updated CURRENT register, interrupt state, and DP clock at that boundary.
Delivery waits for the enclosing device boundary to finish, including SP work
and buffered CPU stores during CPU-driven advances. PI and SP completions at
that clock are visible to the callback. The field retains the pixels captured
at the VI stage. Transfers started during delivery begin at the callback's clock.
The next horizontal period is already latched. A callback may write registers,
replace/remove itself, or reset the system; reset discards undelivered output.
Callbacks must not recursively advance the system.
Reset preserves the registered callback and restarts video timing. Registration
does not deliver an immediate field or replay a missed boundary.

The start comparison ignores the low bit of V_START in both field phases.
Changing V_START affects subsequent boundary comparisons, so software can
cause multiple deliveries in one field or skip a field. A start of zero matches
counter rollover; a start beyond the vertical counter's programmed range never
matches. Video type zero suppresses delivery. The reserved nonzero type keeps
timing active but decodes to black, as it does in explicit snapshots.

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

For X steps up to one source pixel per output pixel, scanout filters each needed
source row into a temporary row buffer and reuses it across output pixels and
repeated output rows. The row cache is keyed by source Y and the alternate
`repeat_lower` neighborhood used by repeated-row filtering. It holds only the
current scan's samples; each render range owns its own cache, so a later snapshot
observes framebuffer and register changes without cross-scan invalidation. Larger
X steps use the scalar sample path directly.

With dither restoration enabled, rows of at least 32 samples reuse decoded
source pixels in blocks of up to 128 outputs. Three source rows and a three-pixel
horizontal margin cover reconstruction and divot filtering. Repeated-lower-row
filtering needs only the first two source rows. The fixed temporary storage
belongs to that row call; short rows and coordinates without room for the margin
use scalar sampling. Both paths share the same reconstruction arithmetic.

`VideoScanMode::Parallel` can split a sufficiently large snapshot into disjoint
output-row ranges. The tasks read the same scan register and framebuffer
state and write separate rows of the returned `VideoField`; the sequential mode
uses the same render loop on one range. This host-side scanout work does not add
VI fetch traffic or RDRAM arbitration to the hardware model.

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
horizontal neighbors. The first visible output row does not use this path,
including when the programmed vertical window begins above the display.
Clipping still advances the source Y coordinate; it does not carry the
repeated-row selection into that first visible line.

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
latching during a field. Timed delivery takes a whole-field snapshot at the
vertical-start boundary. It does not retain rows as they are scanned, and a
later framebuffer write cannot change the field already delivered. A write
between snapshots changes the next snapshot; this does not establish when
hardware would latch that write. Odd-halfline pixel placement and filtering
across mid-field register changes still need validation. Issues #23 and #24
track this remaining work. Hardware captures are also needed to check noise
correlations and the repeated-row behavior independently.

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
Clipping cases cover NTSC and PAL with both framebuffer sizes. They compare
normal lower-neighbor filtering on the first visible line with alternate
neighbor filtering on an interior line at the same source coordinate.

`test_filter_rows.cpp` compares buffered row filtering with scalar neighborhood
sampling across formats, controls, wrapped addresses, and empty memory.
`test_filter_cached_rows.cpp` checks both pixel formats across the block and
row-length boundaries, all filtering controls, repeated lower rows, negative
coordinates, odd-sized memory, partial hidden-bit storage, and coordinate limits.
`test_parallel_scanout.cpp` compares sequential and parallel fields across
formats, filtering controls, scaling, and clipping, and checks that snapshot
execution leaves framebuffer and hardware state unchanged and keeps concurrent
machines independent.

`test_output.cpp` checks delivery deadlines, ten-field NTSC/PAL traces, leap
patterns, interlaced parity, odd/even vertical starts, start-register writes,
counter wrap, blanking, framebuffer changes, owned pixels, callback replacement,
and reset. Scheduling tests compare bulk and single-cycle CPU advances while
SP and SI transfers, RDP drawing, audio, and refresh are active. These tests
check that successive fields contain the SP-written, RDP-filled, and SI-written
pixels in that order. They exercise the existing transfer and drawing models;
VI fetch contention and asynchronous RDP execution remain unimplemented.
