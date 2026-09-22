# RDP texture loads

`src/rdp/texture_load.cpp` implements SetTile, SetTileSize, LoadTile, LoadBlock,
and LoadTLUT. The RDP holds eight tile descriptors and 4 KiB of texture memory
(TMEM). All tiles address the same memory; loading a tile can overwrite data
used by another tile.

## State and addressing

SetTextureImage supplies a 24-bit source address, a ten-bit width-minus-one
field, and the source pixel size. Reserved address and width bits are ignored.
Source reads use physical RDRAM even when DP commands come from SP DMEM.

SetTile stores format, size, palette, S/T shift and mask fields, mirror and clamp
flags, a nine-bit TMEM word address, and a nine-bit line stride. Address and
stride use eight-byte units. Updating a descriptor preserves its bounds.
SetTileSize stores all four 12-bit bounds without changing the descriptor.
The stored mask fields retain their command values; coordinate interpretation
belongs to the sampling path.

LoadTile and LoadTLUT use the integer parts of their quarter-texel bounds.
LoadBlock uses integer S/T coordinates; its final field is DXT rather than a
bottom bound. Loads update the selected tile's bounds after rejecting an empty
range. Horizontal counts wrap to 12 bits. Reversed vertical ranges, zero counts,
and block counts greater than 2,048 perform no transfer and preserve the bounds.

## Transfers

Texture transfers read eight bytes per iteration, including the extra bytes at
the end of a partial word. Source rows use the image width. Destination rows use
the tile stride, so a short or zero stride can overwrite earlier rows. TMEM
addresses wrap within 4 KiB. Transfer order determines the final contents when
destinations overlap.

For 8- and 16-bit tiles, odd rows relative to the load origin exchange the two
32-bit halves of each loaded word. RGBA32 loads put the high halfword in the lower 2 KiB bank and the low
halfword in the upper bank. YUV16 loads put interleaved U/V bytes in the lower
bank and Y bytes in the upper bank. These split-bank transfers exchange adjacent
word pairs on odd rows and wrap each bank independently.

LoadBlock reads a linear source stream. Each 64-bit source transfer advances the
DXT accumulator; its integer T value adds the tile stride and selects the odd-row
swap. Fractional carries can skip destination words or make successive transfers
overwrite the same word pair. Source data continues advancing in either case.

Mismatched source and tile sizes can duplicate destinations or leave gaps. The
implemented paths cover 8-bit sources into 4-bit, 8-bit, 16-bit, or 32-bit RGBA tiles;
16-bit sources into 8-bit, 16-bit, or 32-bit RGBA tiles; and 32-bit sources into
16-bit or 32-bit RGBA tiles. Blocks that generate a nonzero T value require the
tile size to be at least the source size. YUV transfers require both sizes to be
16 bits.

## Palette loads

LoadTLUT distributes a source halfword to four TMEM banks. With a 16-bit source,
each transfer consumes one palette entry. An 8-bit or 32-bit source advances by
a full source word and uses its first halfword. The tile size controls destination
spacing; some combinations overwrite a destination more than once. An odd source
address makes each bank select a successive halfword instead of replicating the
same halfword.

Palette loads support 8-, 16-, and 32-bit source images with 4-, 8-, or 16-bit tile
descriptors. Addresses wrap, and later palette or texture loads can overwrite
each other. Non-YUV loads with four-bit source images and palette loads spanning
multiple integer rows halt DP command execution. The pipe and command-buffer
busy bits remain set, later FullSync commands do not raise an interrupt, and
clearing freeze or busy counters does not recover the processor. Reset clears
the halted state. A halt restores both busy bits even if FullSync cleared them
earlier in the same command batch. A previously raised DP interrupt stays latched.

## Ordering and tests

Loads finish synchronously before the next command. Freeze delays them, incomplete
commands remain buffered, and FullSync observes preceding loads. This establishes
command order but does not model transfer latency, RDRAM arbitration, or overlap
with rasterization. SyncLoad, SyncTile, and SyncPipe have no separate pending work
to drain in this model.

`Rdp::texture_memory()` and `Rdp::tile()` expose read-only host views of the stored
state. They are not CPU-visible registers. Reset clears these stores to a
deterministic initial state.

`tests/rdp/test_texture_load.cpp` compares all 4,096 TMEM bytes after command
streams, including untouched bytes. Cases cover descriptor preservation, source
alignment, reserved bits, partial words, both banks, YUV lanes, odd rows, zero
stride, overlap, DXT carries, palette spacing, malformed loads, XBUS commands,
freeze, reset, and FullSync ordering.

## Remaining work

[Copy-cycle primitives](rdp-copy.md) and
[one-cycle/two-cycle rectangles](rdp-texture-sampling.md) read loaded TMEM through tile descriptors.
The load tests alone do not establish rendered-pixel correctness or synchronization
with an asynchronous rasterizer. Some size combinations remain unsupported: 16-bit
sources into 4-bit tiles, 32-bit sources into 4- or 8-bit tiles outside LoadTLUT,
32-bit palette destinations, and non-RGBA 32-bit tiles. Those combinations leave
TMEM unchanged; this is an implementation limit, not a claim that hardware ignores
them. Unusual mismatched-size blocks need more validation. These gaps remain
tracked in issues #16, #17, and #18.
