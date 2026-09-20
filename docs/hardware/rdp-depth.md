# RDP depth and coverage

One-cycle and two-cycle rectangles compare and update depth through the pixel
pipeline. `SetDepthImage` supplies the depth-buffer address; its stride follows
the color-image width, with two bytes per pixel regardless of color size.
The address field is 24 bits; higher command bits are ignored.
`SetPrimDepth` supplies a 15-bit depth and a 16-bit delta when primitive depth
is selected. The high depth bit is ignored. Rectangles have no interpolated
depth attributes, so the other source supplies depth zero and delta one.

## Storage

The pipeline expands primitive depth by three bits before comparison. An
18-bit depth compresses to a three-bit exponent and an 11-bit mantissa. The
mantissa's step gets smaller toward the far end of the depth range:

| Exponent | First depth | Step |
| --- | --- | --- |
| 0 | `0x00000` | 64 |
| 1 | `0x20000` | 32 |
| 2 | `0x30000` | 16 |
| 3 | `0x38000` | 8 |
| 4 | `0x3c000` | 4 |
| 5 | `0x3e000` | 2 |
| 6 | `0x3f000` | 1 |
| 7 | `0x3f800` | 1 |

Compression discards low bits. The 14-bit result occupies bits 15 through 2
of the stored halfword. A four-bit delta code occupies the remaining two
visible bits and the two hidden bits belonging to that halfword. Reading it
back gives a delta of `1 << code`.

Primitive delta encoding ORs the positions of all set input bits. It is not
an ordinary logarithm for values with several set bits: `0x1800` encodes as
15, and both zero and one encode as zero. Depth writes replace both visible
and hidden delta bits. CPU and DMA writes continue to use their normal
hidden-bit behavior, which can change subsequent depth comparisons.

## Comparison and blending

Depth comparison adjusts the stored delta for the precision of the stored
depth. The comparison tolerance is eight times the highest power of two in
the OR of that adjusted delta and the incoming delta. Boundaries are inclusive
for the tolerance tests. A stored depth of `0x3ffff` is the clear value.

For exponents zero through two, the stored delta doubles and has a minimum
of 16, 8, or 4 respectively. Delta code 15 at those precisions forces both
sides of the tolerance test to pass. Strict front/behind comparisons still
apply where the selected mode requires them.

| Mode | Passing condition |
| --- | --- |
| Opaque | Clear depth, or a nearer surface when coverage wraps; otherwise the incoming depth may lie within the tolerance behind stored depth. |
| Interpenetrating | Opaque rules, with coverage scaled at an overlapping intersection in front of the stored surface. |
| Transparent | Strictly nearer depth, or clear depth. |
| Decal | Within the tolerance on both sides of stored depth, excluding clear depth. |

Coverage wraps when incoming and stored coverage sum to at least eight.
Image-read disable supplies stored coverage seven. Antialias blending also
depends on whether the incoming surface reaches the stored surface's tolerance
window; force-blend overrides that decision. Differences between incoming and
stored delta codes shift either the pixel-alpha or memory-alpha factor when
the blender uses memory alpha. Each shift is limited to four bits.

Interpenetrating comparison can reduce coverage to zero. Antialias mode then
rejects the pixel. The coverage-wrap decision still uses the coverage before
that reduction. Color-on-coverage and coverage destination modes consume this
depth-stage result.

Alpha rejection and failed depth comparison preserve both buffers. Passing
pixels write color first, then depth if depth updates are enabled. When an
RGBA16 color buffer shares its address with the depth buffer, the depth word
and its hidden bits are the final stored value.

## Tests and limits

`tests/rdp/test_depth.cpp` checks compression boundaries, all 262,144 depth
inputs, all 65,536 delta inputs, precision-dependent tolerance, all four
comparison modes, coverage changes, clear depth, and blend shifts.
`tests/rdp/test_depth_rectangle.cpp` checks encoded commands and the resulting
color, depth, and hidden bits in memory. Cases cover reserved address bits,
CPU/SP DMA overwrites, color/depth aliasing, clipping and fields, alpha
rejection, and textured draws in both directions and cycle modes.

Triangle depth interpolation and integration remain unfinished. The legacy
one-cycle unshaded triangle path does not use this pipeline. Arbitrary partial
overlaps between color and depth buffers, other framebuffer formats, and
asynchronous rasterizer memory ordering need further validation. These checks
do not establish complete rendering or gameplay correctness. Issue #22 tracks
the remaining depth and coverage work.
