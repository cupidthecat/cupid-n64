# Experimental triangle fixture audit

Replacing the legacy triangle shortcut with the shared color pipeline exposes
11 failures in the pinned `experimental_rdp` cartridge tests. These failures
remain enabled and make the extended validation command fail. No external
test source, ROM, or expected result has been changed.

The input is `thelemmy/nemu64-test` commit
`9a8b9f7d94ee2f6f57d7feed70c98c22cdc30e6c`, with the ROM and PIF hashes recorded
in the [accuracy baseline](accuracy-baseline.md). The default suite still
passes all 4,637 tests. The extended base category passes 4,638 of 4,649 tests;
its timing category retains the same 11 failures documented in that baseline.
Cycle, CP0-hazard, and partially characterized hardware-quirk groups pass.

## Command and framebuffer packing

The test source defines `ARGB8888` with alpha in bits 31:24, followed by red,
green, and blue. `RDPAssembler::set_blendcolor` writes that value unchanged to
the command. The SDK's `DPRGBColor` and `gDPSetBlendColor` macros instead encode
red in bits 31:24 and alpha in bits 7:0. See the
[SDK command header](https://ultra64.ca/files/documentation/online-manuals/man/header/gbi.htm).

For example, the degenerate-rectangle fixture supplies `0xffff0000` as red.
The RDP interprets those bytes as yellow with alpha zero. With full coverage,
the resulting RGBA32 framebuffer word is `0xffff00e0`. The fixture expects
`0xe0ff0000`, putting coverage in the high byte. Its pixel geometry matches
for this case; the disagreement is already visible in a fully covered pixel,
without fractional-edge or perspective behavior.

The affected files in the pinned checkout are:

- `src/graphics/color.rs`: ARGB storage and color constants.
- `src/rdp/rdp_assembler.rs`: unconverted blend-color command payload.
- `src/tests/rdp/filled_triangle.rs`: ARGB framebuffer reads and CPU expectations.

At pinned commit `9a8b9f7d94ee2f6f57d7feed70c98c22cdc30e6c`, their
SHA-256 hashes are:

| Source file | SHA-256 |
| --- | --- |
| `src/tests/rdp/filled_triangle.rs` | `ee76130e870a6c6b497018b7f875268a5adea23d31af6ef3f37fefef7794dc5f` |
| `src/rdp/rdp_assembler.rs` | `61d7f462b505bb8a348e6e2bee2265bf12e5ff0ae82e52fb09ffb3420ec71a4b` |
| `src/graphics/color.rs` | `c112e80b3af19eeb05ca4d04e46ee27cba824768cebd48772ccb2210172c1fc0` |

## Coverage expectations

The CPU renderer in `filled_triangle.rs` counts sixteen samples per pixel and
maps selected counts through an incomplete table; unlisted counts are returned
directly as alpha. The RDP coverage path uses eight staggered samples. The
fixture's one-cycle mode also leaves antialiasing disabled, while its CPU
renderer accepts any nonzero partial coverage. Fractional edges therefore
have additional disagreements beyond the byte order.

The failing cases are degenerate rectangle, flat-top triangle, left/top/right/
bottom scissor, fractional right/bottom scissor, negative Y, negative X, and
randomized triangles. The experimental case named `right major` passes.

## Independent raw-command fixtures

`tests/rdp/test_triangle_conformance.cpp` writes literal 64-bit command words
directly to RDRAM. It does not use the local triangle command builders or the
pinned cartridge assembler. Expected framebuffer and depth values are literal
constants derived from RGBA command byte order, the eight staggered coverage
sample positions, first-sample rejection when antialiasing is disabled, signed
28-bit edge arithmetic, and the documented compressed-depth path.

The corrected degenerate packet uses blend-color payload `0xff0000ff` for
opaque red and produces `0xff0000e0` at fully covered RGBA32 pixels. A
companion packet changes only that payload to the cartridge fixture's
`0xffff0000`; the observed fully covered word is `0xffff00e0`. This isolates
the byte-order error from triangle geometry.

The fixture set also checks a diagonal edge with four-sample partial coverage,
a fractional top edge rejected without antialiasing when coverage bit zero is
absent, a zero-height triangle with no writes, 28-bit edge overflow before
coverage, and literal interpolated Z words plus hidden delta bits. The overflow
case uses an edge position of `0x07ff0000` and command slope
`0x20040000`; after command decoding, the signed edge arithmetic crosses the
28-bit boundary within one pixel row. Its expected row is two pixels at
coverage `0x60` followed by six at `0x20`.

For provenance, packet hashes below concatenate each fixture's 64-bit command
words in command-stream byte order and hash those bytes with SHA-256:

| Raw fixture | Bytes | SHA-256 |
| --- | ---: | --- |
| Corrected RGBA degenerate | 88 | `7aef909718a984e58f7c675b8f92549f2d8ecc0cc91e98d4b6682396126a07ab` |
| Original ARGB payload companion | 88 | `e3a180f58603a25de2917d67f316b0877c1fe1b8a28e6af743a89c4144dfb0d2` |
| Eight-sample diagonal | 80 | `242ddfc2af30668d087b5829313e3984fd271549d8cc26286b8a3976dee8dc92` |
| Fractional top, no AA | 80 | `88c275fcfe803dd20511578470cb7026459e512fce37f94a5c1fa8d82b9c73d5` |
| Zero-height rejection | 80 | `8daf873733eea126c3f14196ab02804837c74741f6dc288571cdc6f31dd6dcf3` |
| Signed edge overflow | 80 | `1542a372c5c4268878531d8712db094c946a2a9a478b6e90f8eac68f883e1dd0` |
| Interpolated depth | 104 | `ceef17fd8baef6c0822c16a64736512df5d8f90180c8894c0416a8e2bda96916` |

The original 11 cartridge assertions remain enabled and unchanged. The raw
fixtures confirm the current renderer on these corrected packets and do not
provide evidence for changing edge or coverage behavior to satisfy the ARGB/
sixteen-sample oracle. Corrected cartridge fixtures and independent hardware
captures remain necessary for broader randomized conformance; issue #37 tracks
that validation work.
