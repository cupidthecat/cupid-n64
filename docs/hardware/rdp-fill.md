# RDP fill rectangles and triangles

`src/rdp/fill.cpp` handles FillRectangle and textured rectangle commands in fill cycle. The command
processor retains color-image state, the fill word, and the scissor rectangle.
`src/rdp/triangle_spans.cpp` generates fill-cycle spans for all eight triangle
opcodes. Both paths use the same fill-word packing and hidden-bit writes.
Copy-cycle triangles use the same edge walker.
SetColorImage has a ten-bit width-minus-one field and a 24-bit address field;
reserved bits do not contribute to either value.

## Span clipping

Rectangle and scissor coordinates use quarter-pixel units. The rasterizer clips
before converting coordinates to integer pixels. A fractional left or top
scissor boundary can therefore retain part of its containing pixel.

Fill cycle sets the bottom coordinate's low two bits. Subpixel rows at or beyond
that extended bottom edge are excluded. A pixel row is drawn if it contains at
least one surviving subpixel row. In particular, a rectangle starting at the
last quarter-pixel of its own bottom row has no surviving rows.

Horizontal fill spans include both integer endpoints after clipping. This can
write the pixel at an integer right scissor boundary. A span starting at that
boundary is rejected, while a span ending at the left scissor boundary can
produce one pixel. Reversed horizontal edges are rejected before rounding.
Zero scissor coordinates participate in clipping; they are not an instruction
to disable it.

SetScissor bit 25 enables field selection. Bit 24 selects odd rows when set and
even rows when clear. Clearing bit 25 restores both fields regardless of bit 24.
The same field selection applies to the implemented unshaded triangle path.
Reset disables field selection.

## Triangle edges

Triangle Y coordinates are signed 12.2 values. The top coordinate includes its
quarter-row and the bottom excludes it, without the rectangle's bottom extension.
The major and upper minor edges start at the whole row containing the top.
The lower minor edge starts at the middle Y coordinate. A middle coordinate
above the first visited whole row never switches to the lower edge.

X positions wrap as signed 12.16 values. The low position bit is discarded;
quarter-row slopes discard their three lowest input bits. Each sample retains
an eighth-pixel coordinate with a sticky low bit. Edge ordering is checked at
quarter-pixel precision before scissor clipping. The selected major-edge side
determines which edge is left.

Each pixel row combines its surviving quarter-rows into one inclusive integer
span. Scissor clipping applies to signed edge coordinates before conversion to
pixels, and field selection can suppress a row. The framebuffer width is a
stride, not an additional clipping boundary. Positions beyond that width can
therefore write into later framebuffer rows.

Fill ignores triangle shade, texture, and depth attributes, but command decoding
still waits for every attribute word before drawing. FullSync after a complete
draw retains its normal interrupt behavior.

## Fill packing and hidden bits

Fill writes raw portions of the 32-bit fill word. The color-image format field
does not reject a fill or convert its contents.

| Pixel size | Value written | Hidden coverage pair |
| --- | --- | --- |
| 8 bits | Byte selected by destination address modulo four, most significant byte first | Even byte preserves the pair; odd byte replaces both bits with the byte's low bit |
| 16 bits | High halfword at address modulo four equal to zero, low halfword at two | Both bits copy the halfword's low bit |
| 32 bits | Complete fill word | Each halfword's pair copies that halfword's low bit |

Address selection includes the framebuffer base and row stride. An odd width
does not restart the fill pattern at the beginning of a row. Scissor field
selection skips rows without changing the programmed stride.

## Tests and limits

`tests/rdp/test_fill.cpp` submits encoded command streams through DP registers.
It checks fractional and integer clipping, rejected spans, bottom-edge behavior,
both fields, reset, width and address masks, all format encodings, odd strides,
byte selection, hidden bits, and untouched neighboring pixels. The triangle test
checks that field selection also suppresses its framebuffer writes.

`tests/rdp/test_fill_triangle.cpp` checks both major-edge directions, all eight
opcodes, fractional top and middle coordinates, negative edges, clipping,
field selection, accumulator wrap, slope quantization, edge-order precision,
stride overflow, packing, hidden bits, and incomplete commands.

Four-bit fill destinations halt command processing. Other invalid fill states
and [copy-cycle rectangles](rdp-copy.md) have separate tests. Solid rectangles
in one-cycle and two-cycle modes use the [color pipeline](rdp-color.md).
[Textured rectangles](rdp-texture-sampling.md) share that color pipeline.
One-cycle and two-cycle shaded/textured triangles and the complete color/depth
pipeline remain unfinished.
These fill tests do not establish their correctness. Drawing is synchronous
and does not model RDRAM contention or per-pixel DP timing.
