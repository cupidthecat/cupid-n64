# Video field snapshots

`Bus::scan_video()` returns a field decoded from the current VI registers and
installed RDRAM. It is a snapshot: calling it does not advance the VI, perform
DMA, change memory-bank state, or retain pixels from an earlier call. The
implementation lives in `src/vi/scanout.cpp`.

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
| Antialias mode | Mode 3 replicates pixels. Modes 0 through 2 currently apply the same bilinear resampling step. |

RGBA16 color components expand to eight bits with their low three bits zero.
RGBA32 supplies the upper three bytes of each word. The stored coverage bits
do not become output alpha.

Horizontal guard bands discard the first eight and final seven output pixels
when the corresponding programmed endpoint lies within the visible interval.
An endpoint beyond that interval is clipped without adding its guard band.
Clipping preserves the source coordinates, so an offscreen start still advances
the X or Y sample position. Empty and reversed windows produce black.

Resampling takes the high five fractional bits of each coordinate. It rounds
the vertical interpolation of both source columns first, then rounds their
horizontal interpolation. Replication discards the fractional parts.

## Current limits

This is the framebuffer fetch and resampling portion of scanout. Coverage
antialiasing, divot filtering, dither restoration, gamma, and gamma dithering
are not implemented. In particular, modes 0 and 1 still need their coverage
filter before resampling. The control-register path also needs to retain the
dither-restoration control bit when that filter is added.

Snapshots read the installed RDRAM bytes directly. They do not model chip
remapping, VI fetch traffic, arbitration, line-buffer retention, or register
latching during a field. A write between calls changes the next snapshot;
this does not establish when hardware would latch that write. Timed field
delivery, odd-halfline boundary behavior, and the repeated-source-row fetch
quirk still need validation. Issues #23 and #24 track this remaining work.

## Regression coverage

`tests/vi/test_scanout.cpp` checks both formats, NTSC/PAL placement, black
borders, guard bands, clipping, fixed-point offsets, zero scales, replication,
interpolation order and rounding, origin alignment, 4/8 MiB wrapping, source
stride, field metadata, independent snapshots, and preservation of VI and
memory-bank state. These tests exercise the core API without a frontend.
