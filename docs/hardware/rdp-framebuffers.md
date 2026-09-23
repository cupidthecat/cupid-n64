# RDP color framebuffer storage

One-cycle and two-cycle rectangles and triangles accept every color-image
size and format code. The color-image format controls framebuffer storage
independently of the texture format. These paths share the combiner, alpha
test, depth test, blender, and coverage logic.

| Size | Format code | Stored result | Framebuffer color input |
| --- | --- | --- | --- |
| 4-bit | Any | Zero byte for each addressed pixel | Black, coverage seven |
| 8-bit | Any | Red at even byte addresses; green at odd addresses | Stored byte replicated to RGB, coverage seven |
| 16-bit | Zero | RGBA5551, with split visible/hidden coverage | RGB channels expanded with three zero low bits |
| 16-bit | Nonzero | Red in the high byte, coverage in low-byte bits 7:5 | High byte replicated to RGB |
| 32-bit | Any | RGBA byte order, with coverage in low-byte bits 7:5 | Stored RGB bytes |

The 4-bit color path advances one byte per pixel and clears that byte. It is
not a normal packed-nibble intensity framebuffer. It preserves the hidden
pair on both even and odd byte writes.

Eight-bit writes preserve the hidden pair at even addresses. Odd writes set
both hidden bits from the stored byte's low bit. Address parity includes the
framebuffer origin and row stride, so an odd width reverses channel selection
on alternate rows.

RGBA16 puts the high coverage bit in visible bit zero and the other two bits
in hidden memory. Non-RGBA 16-bit storage does not use that split: its low
five visible bits are zero after a color write, and its hidden pair becomes
zero. For 32-bit writes, the first hidden pair follows green bit zero and
the second follows the low visible byte's bit zero.

## Reads, coverage, and addressing

Image-read enable controls stored coverage for 16-bit and 32-bit buffers;
when disabled, coverage is seven. Four-bit and eight-bit buffers always
supply coverage seven. The RGB memory input still follows the table above.
Non-RGBA 16-bit reads ignore hidden memory and the low five visible alpha
bits when extracting coverage.

The origin aligns down to the storage element size. Four-bit and eight-bit
paths use byte alignment; 16-bit paths use halfword alignment; 32-bit paths
use word alignment. Width is a pixel stride, not a clipping limit. Scissor
and field selection determine which pixels are written.

Color and depth addresses wrap at the installed 4 MiB or 8 MiB RDRAM size.
Fill and copy destinations use the same address helper while retaining their
own packing and validity rules. Depth uses two bytes per pixel and the
color-image width regardless of color size. Alpha and depth rejection leave
the color buffer, depth buffer, and hidden bits untouched. Accepted pixels
write color before depth.

## Validation and limits

`tests/rdp/test_framebuffer.cpp` adds 23 regressions. The command fixtures cover
every size and format code in both cycle modes, all eight triangle forms,
untouched neighbors, odd origins and strides, hidden pairs, intensity blending,
coverage and memory-alpha reads, fields, alpha/depth rejection, and color/depth
address wrapping in both memory configurations. Separate fill/copy cases check
that wrapping preserves word lanes and hidden bits.

Shared color/depth halfwords are checked across all 16-bit format codes:
primitive depth `0x4000` and delta `0x80` leave visible word `0x2001` and hidden
pair three. The same exact-origin alias is also checked for every compressed
depth-delta code. RGBA16 keeps the depth delta's low two hidden bits; non-RGBA
16-bit formats repack the updated depth through the intensity/alpha storage
path, so the low visible bit is replicated into the hidden pair.

Exact matching color and depth origins select alias behavior for every color
size. A successful depth update does not issue a second depth-memory write for
4-bit, 8-bit, or 32-bit color images, even when that pixel's color bytes and
two-byte depth lane do not overlap. The color write therefore remains intact,
and neighboring depth-lane bytes remain untouched. For 16-bit color, the depth
state is instead represented through the color format as described above.

These regressions establish the synchronous storage rules for exact-origin
color/depth aliasing. Arbitrary offset overlaps, deferred rasterizer ordering,
and scanout from unusual framebuffer layouts still need hardware captures.
Drawing remains synchronous. Issues #22, #23, and #37 track those broader
checks.
