# RDP color pipeline

In one-cycle and two-cycle modes, rectangle and triangle commands pass through the color
combiner and blender for all [color-image sizes and format codes](rdp-framebuffers.md). Primitive, environment,
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

Each draw prepares its selectors and constant values once. RGB and alpha are
classified independently. When the multiplier is a constant signed nine-bit
zero, or A and B resolve to the same input or expanded constant, the equation
reduces to expanded D. The pixel path then skips the unused terms. Color keying
still reads A for its RGB bypass and retains the signed 17-bit key result.
Draw preparation runs again after state changes, so changing a constant does
not require another `SetCombine` command. Texture and noise input selection
retains the programmed selectors even when an equation simplifies.

When color keying is enabled, the RGB combiner still evaluates the programmed
`(A - CENTER) * SCALE` expression at its normal fixed-point precision. Alpha
fixup interprets each RGB result as signed 17-bit data, subtracts its absolute
magnitude from the channel's 12-bit key width expanded by four bits, takes the
minimum of the three channels, and clamps that key alpha to 0-255. Final keyed
RGB bypasses the equation result and uses the combiner's A input. This preserves
the fractional key-window boundary rather than reducing the equation to an
eight-bit color first.

In two-cycle mode, alpha comparison uses the first cycle's key alpha while the
second cycle produces the final key alpha and keyed RGB. Coverage-times-alpha
continues to use the ordinary combiner alpha before key alpha substitution.
Alpha-coverage-select can replace the final alpha with coverage as usual. These
ordering rules keep alpha rejection ahead of framebuffer/depth writes and make
key alpha available to the blender when alpha-coverage-select is disabled.

An alpha value of 255 expands to 256 for coverage multiplication. Coverage can
replace alpha, alpha can scale coverage, or both operations can be enabled.
Alpha comparison accepts equality with the blend-color alpha threshold.

## Blending and memory

The blender selects pixel, framebuffer, blend, or fog RGB and combines them
using five-bit alpha factors. Two-cycle mode feeds the first blend's RGB into
the second blend. Force-blend uses a fixed shift; ordinary blending uses the
hardware divider's truncated remainder stages. The divider differs from
ordinary integer division for overflowing numerators and large denominators.
`src/rdp/blender_divider.cpp` evaluates the 32,768 possible inputs at compile
time. Pixel blending indexes this read-only table, retaining the same input
masking and quotient bits without repeating the eight divider stages.

Full-alpha rejection and disabled blending select the first color directly.
Color-on-coverage selects the second color until accumulated coverage wraps.
Framebuffer RGB remains available when image reads are disabled; that mode
replaces the memory coverage with seven and memory alpha with 224. A deferred
framebuffer read still runs when either first-cycle RGB selector or the final
pixel-color selector consumes memory. The first blend cycle can also consume
the fixed memory alpha when no RGB read is needed. RGBA16 reads retain five-bit channel
precision without bit replication. The [depth stage](rdp-depth.md) controls
blend enable and the pixel/memory alpha shifts from coverage and depth deltas.

Rectangle coverage uses eight staggered samples in four subpixel rows. Right
and bottom edges are exclusive, including fractional scissor boundaries.
Without antialiasing, the first sample determines whether a pixel is written.
Field selection skips the other field's rows.

## Synchronous row execution

Color rectangles and color triangles can split their raster rows across the
shared range workers when the draw has independent memory cells. The parallel
path is limited to 16- or 32-bit color images with an identity-mapped RDRAM
layout, a horizontal output range inside the programmed color stride, and a
color address interval that does not wrap beyond installed memory. When depth
access is enabled, its interval must also stay in RDRAM and must not overlap the
color interval. Small draws and machines without spare worker capacity stay on
the serial path.

Each row task owns complete color pixels and their hidden-bit cells. RDRAM row
tracking is recorded in a task-local `BankAccessSummary` while the pixels are
drawn. Workers claim small contiguous ranges so uneven triangle widths do not
leave all remaining work on one worker. After every task has completed, the
summaries are merged in row-range
order so the final RI open row, dirty state, and access timestamp match serial
drawing. The raster command remains synchronous: command execution does not
continue while a row task is outstanding.

`tests/rdp/test_parallel_rows.cpp` compares serial and parallel color bytes,
hidden bits, depth, RDP registers, and RI bank state. It also checks that color
wrapping, color/depth aliases, small framebuffer formats, stride overruns, and
non-identity RDRAM mappings remain serial, and that separate machines do not
share task-local bank state.

The [render-worker guide](../frontend/render-workers.md) describes job ownership,
exception handling, and thread shutdown.

Coverage destination modes clamp, wrap, replace with seven, or preserve the
framebuffer value. RGBA16 stores the high coverage bit in the pixel and the low
two bits in hidden memory. RGBA32 stores coverage in the top three bits of its
low byte. Neither format stores the combiner's alpha as ordinary opacity.
Magic-square and Bayer dithering apply their four-by-four thresholds before
framebuffer packing. Random RGB dithering uses separate three-bit thresholds
for red, green, and blue; rounding saturates at 255. Alpha dithering supports
the matrix, inverse matrix, random three-bit values, and disabled mode.
Random alpha comparison uses an eight-bit threshold instead of blend alpha.
Equality passes. Rejected pixels leave color, coverage, and depth untouched.

## Noise

Combiner noise has three random bits in positions 8:6, bit 5 set, and bits
4:0 clear. It therefore supplies one of eight values from 32 through 480;
the combiner's existing nine-bit arithmetic determines their color result.
When both cycles select noise, they receive separate samples. Alpha comparison
still uses the first cycle's alpha in two-cycle mode.

The noise source is a reproducible hash of the primitive sequence and pixel
coordinates. Repeated draws resample it; reset restores the initial sequence.
Scissor and field filtering retain the same samples at surviving coordinates.
DP buffer boundaries, partial packets, and sync commands do not restart the
sequence. Each accepted draw command advances it, including clipped draws and
fill/copy commands. Matrix dithering uses field-relative Y, while spatial
noise uses framebuffer Y.

This source models the required value ranges and pixel-pipeline connections,
not the hardware noise generator's exact sequence or clock phase. The two
cycle samples and their correlation with alpha comparison are part of the
current deterministic model. Hardware captures and asynchronous DP timing
are needed to validate those correlations. Tests do not treat the hash output
as a measured hardware sequence.

## Tests and limits

`tests/rdp/test_color_combiner.cpp` checks selectors, fixed-point rounding,
signed intermediates, overflow clamps, cycle feedback, key-window boundaries,
key RGB bypass, two-cycle key alpha comparison, alpha/coverage behavior,
blend factors, divider edge cases, and a checksum of all 32,768 divider inputs.
`tests/rdp/test_combiner_plan.cpp` compares prepared execution with the scalar
equation for every raw selector and randomized states. It also checks equal
and unequal constants, signed D expansion, key bypass, two-cycle feedback,
texture swapping, and constants changed between draws.
`tests/rdp/test_color_rectangle.cpp`
checks encoded commands, framebuffer bytes, hidden coverage, clipping, fields,
state changes, reset, keyed alpha rejection/depth ordering, keyed blending,
two-cycle key comparison, and dither thresholds.
`tests/rdp/test_framebuffer_read_dependencies.cpp` checks framebuffer RGB
selection with blending disabled, both first-cycle RGB selectors, final-cycle
memory selection, and fixed memory alpha with image reads disabled.
`tests/rdp/test_noise.cpp` and `tests/rdp/test_pixel_noise.cpp` check
noise quantization, selector combinations, separate cycle inputs, per-channel
dithering, saturation, alpha rejection, unchanged depth/hidden bits, clipping,
fields, reset, repeated draws, and partial command packets.

Rectangles and triangles supply [sampled texels and texture LOD](rdp-texture-sampling.md)
to the combiner. [Triangles](rdp-triangles.md) also supply interpolated shade
and depth. Rectangle shade inputs remain zero.

Both primitives support depth comparison and writes. The key equation and
alpha-fixup ordering follow the SGI RDP command summary and the Nintendo 64 RDP
programming manual's chroma-key equations. `SetKeyR` and `SetKeyGB` widths remain
12-bit command values; center and scale remain eight-bit values. The exact
hardware noise sequence, framebuffer/depth overlap ordering, and asynchronous
rasterizer timing remain unfinished. K4 and K5 are available to the combiner,
while K0 through K3 feed texture conversion. These tests do not establish
complete rendering or gameplay correctness. Issues #18 through #22 track the
remaining sampling and rendering work.

References:

- <https://ultra64.ca/files/documentation/silicon-graphics/SGI_RDP_Command_Summary.pdf>
- <https://ultra64.ca/files/documentation/online-manuals/man/pro-man/pro12/12-06.html>
