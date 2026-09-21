# RDP texture sampling

One-cycle and two-cycle rectangle and triangle commands sample TMEM and pass their texels
through the [color pipeline](rdp-color.md). `TextureRectangle` and
`TextureRectangleFlip` interpolate their encoded S/T coordinates. `FillRectangle`
can also select a texel through the combiner, using tile zero and zero texture
attributes. Copy-cycle primitives keep their separate raw-word sampling path.

## Addressing and formats

Coordinates clamp to signed 16-bit values before tile shifts. Shifts zero
through ten divide the coordinate; shifts eleven through fifteen shift left
with signed 16-bit wrap. The tile's quarter-texel origin is subtracted before
integer addressing. Clamp uses the programmed tile bounds and clears the
fraction when an edge is reached. A zero mask enables clamping even when the
explicit clamp bit is clear.

Masks and mirrors apply independently to each sampled neighbor. Mask widths
above ten use ten bits. T addressing keeps eight bits for the base row while
preserving the carry into the next filtered row. TMEM reads wrap within their
bank and exchange word halves on odd rows.

| Format | Decoding |
| --- | --- |
| RGBA16 | Replicated five-bit RGB, one-bit alpha |
| RGBA32 | RG in the lower bank, BA in the upper bank |
| IA4 | Three-bit intensity, one-bit alpha |
| IA8 | Separate four-bit intensity and alpha |
| IA16 | Separate eight-bit intensity and alpha |
| I4 and I8 | Intensity replicated into all four channels |
| CI4 and CI8 | Palette lookup when TLUT is enabled; replicated index otherwise |
| YUV16 | Signed U/V from the lower bank, luma from the upper bank |

The sampler also retains the channel-routing aliases: RGBA4/8 behave like
intensity formats, while CI16/32, I16/32, and IA32 repeat the high and low bytes
of a TMEM halfword as RGBA. CI4 includes the tile's palette number even without
TLUT lookup.

When TLUT is enabled, RGBA, CI, IA, and I tiles all use palette lookup.
Indices come from the lower 2 KiB and select replicated RGBA16 or IA16
entries in the upper bank. Sixteen- and 32-bit index modes use the source
halfword's high byte. Each filter tap reads its assigned palette bank; this
matters when the banks contain different values. With TLUT enabled and quad
sampling disabled, the footprint uses one texture index and multiple palette
banks.

The palette integration test uses a 16-bit texture-image source and a load tile
with size zero, as encoded by the [SDK palette-load macros](https://ultra64.ca/files/documentation/online-manuals/man/header/gbi.htm).
These are separate state fields; setting the load tile to 16 bits changes the
spacing between loaded entries.

## Filtering and conversion

Point sampling reads one texel. Quad sampling chooses a triangular footprint
from five-bit S/T fractions. Filtering combines three taps with fixed-point
rounding. The mid-texel mode averages all four taps at the exact center.
YUV chroma uses half the horizontal sampling rate of luma, so the two channel
pairs can select different triangles.

The per-cycle filter-enable bits also select texture conversion. Clearing a
filter bit applies the programmed conversion factors, including for non-YUV
formats. `convert_one` can convert the preceding texture sample directly, or
use its signed channels as interpolation coefficients when filtering is enabled.
The combiner receives the intermediate signed values before its final clamp.

## Rectangles and LOD

Texture accumulators wrap at 32 bits before extracting signed coordinates.
Fractional left edges adjust the interpolation origin; clipping and field
selection preserve the original coordinate steps. The vertical interpolation
origin is the whole row containing the rectangle's top. Flipped rectangles
exchange the horizontal and vertical derivative axes.

One-cycle `texel1` reads ahead to the next pixel. At the end of a sufficiently
long span it can read the next valid row's initial coordinate. Two-cycle mode
samples adjacent tiles, then follows the combiner's texture-input exchange.

LOD uses the largest S/T derivative, including its ones-complement handling of
negative differences. Detail and sharpen modes preserve their tile offsets,
minimum LOD, and signed fractions. Rectangles have a maximum LOD level of zero;
detail mode can still select the following tile. Rectangle commands supply
W=0, so enabling perspective produces positive coordinate saturation and a
distant LOD result.

## Tests and limits

`tests/rdp/test_texture_sampling.cpp` checks format decoding, palette banks,
coordinate boundaries, shifts, masking, mirroring, filtering, conversion, and
TMEM wrap. `tests/rdp/test_texture_lod.cpp` checks magnification, minification,
signed fractions, detail/sharpen selection, overflow, and tile-index wrap.
`tests/rdp/test_texture_rectangle.cpp` checks encoded loads and draws, both
rectangle directions, interpolation, clipping, fields, alpha comparison,
two-cycle sampling, lookahead, and incomplete commands. Together they add
59 regressions.

[Triangles](rdp-triangles.md) interpolate S/T/W, retain 17-bit perspective
coordinates for LOD, and supply the packet's maximum mip level. Their command
tests cover perspective, LOD, and lookahead separately from rectangles.
Reserved texture formats and YUV with TLUT
currently supply zero; non-16-bit YUV uses the 16-bit addressing path. Those
are implementation limits, not verified hardware dispositions. Unusual texture
load combinations retain the limits described in [texture loads](rdp-texture-loads.md).

Textured primitives use the [depth comparison and update stage](rdp-depth.md).
The shared pixel stage supplies [noise and random dithering](rdp-color.md).
All [color framebuffer formats](rdp-framebuffers.md) use this sampling stage.
The exact hardware noise sequence, framebuffer/depth overlap ordering, and
asynchronous DP timing remain unfinished. [Key-generated alpha](rdp-color.md)
uses the combiner's fixed-point key equation and per-cycle alpha fixup.
Issues #18 through #22 remain open.
