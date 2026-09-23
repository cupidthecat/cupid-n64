# RDP triangle interpolation

All eight triangle opcodes use the color pipeline in one-cycle and two-cycle
modes for all [color framebuffer formats](rdp-framebuffers.md). Optional shade, texture, and depth
blocks are decoded at their command-defined offsets. An incomplete packet
waits for its remaining words. Edge-only commands use the programmed combiner
and blender; blend color is not an implicit triangle color.

## Edges and coverage

Fill, copy, and color triangles share a fixed-point edge walker. It preserves
signed 14-bit Y coordinates, 28-bit X wrap, slope truncation, fractional edge
rounding, the middle-edge transition, and scissor and field selection.
Color coverage tests eight staggered samples in four subpixel rows. Right and
bottom edges are exclusive. Antialiasing accepts any covered sample; without
it, the first sample must be covered.

Attribute interpolation retains the major edge's unclipped origin. The edge
direction and slope sign select whether attributes latch on the first or last
subpixel row. Edge and vertical derivatives apply the fractional-row correction;
the major edge's fractional X applies the horizontal correction. Arithmetic
wraps and truncates at each fixed-point stage rather than using floating point.

Shade and depth are evaluated at the first covered sample. Shade uses the
nine-bit overflow clamp. Depth keeps finer derivative precision and uses the
18-bit depth range with its overflow regions. Its delta comes from the
ones-complement magnitudes of the integer X/Y gradients, rounded to the next
power of two and capped at 32,768. Primitive depth overrides both the value
and gradients before interpolation.

The depth value is evaluated only when depth comparison or depth writing uses
it. Depth delta remains available with both disabled because it still controls
the blender's memory-alpha shift.

## Textures and pixel output

S/T/W interpolation feeds reciprocal-ROM perspective division. The divider
retains 17-bit coordinates for LOD before tile addressing clamps to 16 bits.
Nonpositive W and divider overflow force a distant LOD result. Horizontal and
vertical derivatives, the packet's tile and maximum level, and detail/sharpen
state select the sampling tiles and LOD fraction.

The divider normalizes W and prepares its reciprocal once for each S/T pair.
Triangle drawing computes horizontal and vertical neighbors when LOD consumes
them. One-cycle texel-1 also requires the horizontal neighbor and, where
applicable, next-row lookahead. These choices depend on the draw's active texture
inputs and retain the same overflow accumulation for LOD.

One-cycle texel-1 reads ahead in the major-edge direction. At the end of a
long span with a valid next row, it uses that row's initial texture attributes.
Field filtering prevents this row transition. Two-cycle sampling uses the
current coordinate and the selected second tile.

Interpolated shade, texels, and depth feed the shared combiner, alpha/coverage,
depth, blender, and framebuffer stages. Shade alpha also supplies the blender's
shade-alpha selector. Color writes precede depth writes, with the visible and
hidden storage rules described in [depth and coverage](rdp-depth.md).

## Validation and limits

`tests/rdp/test_color_triangle.cpp` checks encoded commands, every triangle
opcode in both cycle modes, shade and texture modulation, perspective/LOD,
lookahead and direction, shared edges, fractional centroids, clipped origins,
RGBA packing, hidden coverage, depth overlap, primitive depth, and partial
packets. `tests/rdp/test_triangle_interpolation.cpp` checks fixed-point rounding,
overflow regions, gradient normalization, and divider saturation.
`test_perspective_point.cpp` checks literal reciprocal results, overflow from
either axis, and paired-versus-separate division for every positive W value.
`test_triangle_texture_needs.cpp` checks which neighbors reach LOD and texel-1,
the next-row boundary, and RI bank state and clocks through real commands.

The unmodified experimental triangle fixtures have conflicting color packing
and coverage expectations. The documented preparation corrects those fixtures
while retaining their assertions; see [the fixture audit](../testing/rdp-triangle-fixtures.md)
and [recorded validation results](../testing/validation-results.md) for the
original and prepared suite outcomes. These checks do not establish complete
rendering or gameplay correctness.

[Noise and random dithering](rdp-color.md) now use the shared pixel stage.
The exact hardware noise sequence, unusual texture combinations, arbitrary
color/depth overlaps, and asynchronous
DP timing still need work. Drawing remains synchronous and does not model
per-pixel RDRAM contention. Issues #19, #21, #22, and #37 track these limits and
additional conformance testing.
