# RDP copy primitives and draw validation

`src/rdp/copy.cpp` draws TextureRectangle and TextureRectangleFlip commands in
copy cycle. It reads shared TMEM through the selected tile and writes framebuffer
bytes or halfwords without the combiner, blender, or depth pipeline. Texture
format fields do not convert the copied values.

`src/rdp/copy_triangle.cpp` handles all eight triangle opcodes in copy cycle.
Triangles use the [fill/copy edge walker](rdp-fill.md#triangle-edges) and share
TMEM reads, alpha rejection, and framebuffer writes with copy rectangles.

## Rectangle coordinates

Copy uses the same quarter-pixel clipping and inclusive integer horizontal spans
as [fill rectangles](rdp-fill.md). The bottom coordinate extends to the last
quarter-pixel of its row. Scissor field selection can suppress even or odd rows.

Texture coordinates use signed 10.5 values; derivatives use signed 5.10 values.
The interpolation accumulators wrap at 32 bits. Normal rectangles advance S
between horizontal groups and T between rows. Flipped rectangles exchange those
directions, but the TMEM reads within each group still advance S.

An 8-bit framebuffer uses groups of eight output pixels. A 16-bit framebuffer
uses groups of four. The horizontal derivative applies once per group, not once
per pixel. Clipping the left edge restarts the group at the clipped integer
position without advancing the initial S/T coordinates. Vertical clipping does
advance the row coordinate. Fractional left edges do not adjust copy coordinates.

Rectangle commands supply W=0. Enabling perspective therefore saturates both
coordinates to `0x7fff` before tile addressing.

## Triangle texture interpolation

Textured triangles decode S, T, W, and their X, edge, and Y derivatives from
the command's separate integer and fractional halfwords. Shaded variants place
the texture block after their color attributes. Color and depth attributes do
not affect copy pixels. Triangles without texture attributes use zero values
and derivatives, while retaining the tile selected in the triangle header.

Each row starts from the whole row containing the top Y coordinate and advances
attributes by the edge derivative. The row accumulator discards its low nine
bits. When the major-edge side matches the sign bit of its X slope, a correction
from the edge and Y derivatives selects the last quarter-row latch. That
correction uses derivatives with their low nine bits cleared. The resulting
row base discards its low ten bits. Copy does not adjust attributes for the
major edge's fractional X coordinate.

Horizontal interpolation advances once per four 16-bit pixels or eight 8-bit
pixels. The X derivative discards its low five bits. A left major edge advances
groups from left to right; a right major edge starts at the right and subtracts
the derivative between groups. Lanes within each fetched group advance S.
Scissor clipping restarts groups at the clipped span endpoint, while vertical
clipping and skipped fields retain the row's absolute attribute position.

All attribute arithmetic wraps at 32 bits. Without perspective, the high
halfwords supply signed S/T coordinates. With perspective, the high W halfword
is a signed 1.15 divisor. The divider normalizes W, interpolates a reciprocal
from its 64-entry ROM, and multiplies the signed coordinate. The reciprocal
values retain the ROM's irregular rounding. Nonpositive W forces both results
to `0x7fff`; other results saturate to signed 16 bits before tile shifts and
masks. `src/rdp/texture_coordinates.cpp` implements this copy addressing path.

## Tile and TMEM addressing

Shifts from zero through ten shift coordinates right. Larger shifts move them
left by `16 - shift`, wrapping at 16 bits. The tile's quarter-texel low bound is
subtracted after shifting. Copy ignores the clamp flags and high bounds. Mask
and mirror operations apply independently to each fetched coordinate, with
nonzero mask widths limited to ten bits.

Each group reads four halfwords. Addressing includes the tile's TMEM base and
line stride. Odd T rows exchange the two 32-bit halves of each word. Addresses
wrap within TMEM; 32-bit tiles and palette-enabled reads use only the lower
2 KiB for source indices.

With palette lookup disabled, the first two halfwords replicate pairs of 4-bit
or 8-bit texels into bytes. Four-bit values repeat their nibble in both halves
of a byte. For 32-bit tiles, those first halfwords combine the high bytes of
successive lower-bank halfwords. The last two halfwords remain raw TMEM reads.
Sixteen-bit tiles use raw halfword reads for all four lanes.

Palette lookup selects a nibble plus the tile's palette bank for 4-bit tiles,
or a byte for other sizes. Each output halfword selects its corresponding
palette bank in upper TMEM. Copy preserves the palette word without interpreting
the palette-type bit. Eight-bit destinations split the four fetched halfwords
into high and low bytes.

## Framebuffer writes

RGBA16 destinations receive raw halfwords. Alpha compare rejects a halfword
whose low bit is clear, preserving both framebuffer data and hidden coverage
bits. Alpha compare does not reject 8-bit destination writes. Even-byte writes
preserve the hidden pair; odd-byte writes replace both bits with the byte's low
bit. Sixteen-bit non-RGBA destinations do not write in the implemented copy path.

Four-bit framebuffer mode writes zero bytes at successive pixel addresses,
rather than packing two copied nibbles into a byte. Its even writes preserve
the hidden pair and odd writes clear it. A 32-bit copy destination halts the DP.

FillRectangle in copy cycle uses tile zero and zero texture attributes. Textured
rectangle commands in fill cycle use the fill word and ignore their texture
attributes.

## Invalid draw states

The command processor validates every complete triangle and rectangle command
before clipping or drawing. It halts on these combinations:

- Fill cycle with a four-bit framebuffer.
- Fill cycle with depth comparison or framebuffer reads enabled.
- Fill cycle with depth updates enabled and primitive depth disabled.
- Copy cycle with a 32-bit framebuffer.

Setting the state alone does not halt execution. An incomplete primitive also
waits for its remaining words before validation. A halt restores the pipe and
command-buffer busy bits even after FullSync, prevents later commands from
running, and requires reset to recover. Clearing freeze or busy counters does
not resume it.

## Tests and limits

`tests/rdp/test_copy.cpp` submits loads and draws through DP command streams. Its
30 tests cover group boundaries, flipped rectangles, clipping, fields, signed
coordinates, fractional derivatives, shifts, masks, mirrors, palettes, raw
formats, hidden bits, partial commands, and copy/fill command combinations.
`tests/rdp/test_draw_state.cpp` checks invalid states across all eleven primitive
opcodes, complete-command validation, permitted primitive-depth updates, and
reset recovery.

`tests/rdp/test_copy_triangle.cpp` checks all triangle variants, major-edge
directions, clipping, row and group interpolation, attribute precision, signed
wrap, perspective, alpha rejection, field selection, and partial commands.
`tests/rdp/test_texture_coordinates.cpp` checks divider saturation, nonpositive
W, power-of-two divisors, fractional reciprocal precision, and zero numerators.

[One-cycle and two-cycle textured rectangles](rdp-texture-sampling.md) now use
the sampling and color pipelines, as do [one-cycle and two-cycle triangles](rdp-triangles.md).
Drawing is synchronous;
these tests do not establish DP timing,
RDRAM contention, or synchronization with an asynchronous rasterizer. Issues
#16, #18, #19, and #20 track the remaining work.
