# RDP color pipeline

In one-cycle and two-cycle modes, rectangle commands pass through the color
combiner and blender for RGBA16 and RGBA32 framebuffers. Primitive, environment,
fog, and blend colors use RGBA byte order. `SetCombine`, `SetKeyR`, `SetKeyGB`,
`SetConvert`, and the primitive LOD fraction update the color state used by
subsequent commands. Reset clears that state.

## Combining colors

Each channel evaluates `(A - B) * C / 256 + D`, with rounding before the shift.
The multiplier is signed nine-bit data. The other terms use the RDP's special
nine-bit expansion, and the final clamp preserves its overflow behavior.
Clamping an intermediate result as an ordinary eight-bit color would change
the next cycle's result.

One-cycle mode uses the second combiner mux. Two-cycle mode feeds the first
result into the second cycle without clamping, swaps its texture inputs, and
uses the first cycle's alpha for comparison. The combiner supports separate
RGB and alpha selectors, key center/scale, K4/K5 conversion constants, primitive
LOD fraction, and supplied shade, texel, LOD, and noise inputs.

An alpha value of 255 expands to 256 for coverage multiplication. Coverage can
replace alpha, alpha can scale coverage, or both operations can be enabled.
Alpha comparison accepts equality with the blend-color alpha threshold.

## Blending and memory

The blender selects pixel, framebuffer, blend, or fog RGB and combines them
using five-bit alpha factors. Two-cycle mode feeds the first blend's RGB into
the second blend. Force-blend uses a fixed shift; ordinary blending uses the
hardware divider's truncated remainder stages. The divider differs from
ordinary integer division for overflowing numerators and large denominators.

Full-alpha rejection and disabled blending select the first color directly.
Color-on-coverage selects the second color until accumulated coverage wraps.
Framebuffer RGB remains available when image reads are disabled; that mode
replaces the memory coverage with seven. RGBA16 reads retain five-bit channel
precision without bit replication. The [depth stage](rdp-depth.md) controls
blend enable and the pixel/memory alpha shifts from coverage and depth deltas.

Rectangle coverage uses eight staggered samples in four subpixel rows. Right
and bottom edges are exclusive, including fractional scissor boundaries.
Without antialiasing, the first sample determines whether a pixel is written.
Field selection skips the other field's rows.

Coverage destination modes clamp, wrap, replace with seven, or preserve the
framebuffer value. RGBA16 stores the high coverage bit in the pixel and the low
two bits in hidden memory. RGBA32 stores coverage in the top three bits of its
low byte. Neither format stores the combiner's alpha as ordinary opacity.
Magic-square and Bayer dithering apply their four-by-four thresholds before
framebuffer packing; alpha dithering supports the matrix and inverse matrix.

## Tests and limits

`tests/rdp/test_color_combiner.cpp` checks selectors, fixed-point rounding,
signed intermediates, overflow clamps, cycle feedback, alpha/coverage behavior,
blend factors, divider edge cases, and a checksum of all 32,768 divider inputs.
`tests/rdp/test_color_rectangle.cpp`
checks encoded commands, framebuffer bytes, hidden coverage, clipping, fields,
state changes, reset, alpha rejection, blending, and dither thresholds.

Rectangles now supply [sampled texels and texture LOD](rdp-texture-sampling.md)
to the combiner. Shade and noise inputs remain zero. One-cycle/two-cycle
triangles retain their earlier implementation limits; the existing unshaded
triangle shortcut is separate from this pipeline.

Rectangles support depth comparison and writes. Triangle depth integration,
color-key alpha generation, random noise
and random dither/alpha thresholds, other framebuffer formats, and asynchronous
rasterizer timing remain unfinished. Key center and scale can be selected by
the combiner, but key widths do not yet affect alpha. K4 and K5 are available
to the combiner, while K0 through K3 feed texture conversion. These tests do
not establish complete rendering or gameplay correctness. Issues #18 through
#22 track the remaining sampling and rendering work.
