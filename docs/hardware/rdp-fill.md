# RDP fill rectangles

`src/rdp/fill.cpp` handles FillRectangle commands in fill cycle. The command
processor retains color-image state, the fill word, and the scissor rectangle.
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

Four-bit destinations and FillRectangle in one-cycle, two-cycle, or copy mode
remain unsupported. Textured rectangles, texture sampling, and the complete
color/depth pipeline are separate unfinished work. These fill tests do not
establish their correctness. Drawing is synchronous and does not model RDRAM
contention or per-pixel DP timing.
