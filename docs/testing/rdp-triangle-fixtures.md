# Experimental triangle fixtures

The pinned `experimental_rdp` cartridge uses ARGB command payloads and a CPU
oracle with different coverage and edge rules from the RDP. The maintained
[fixture corrections](cartridge-fixtures.md) repair those definitions while
retaining all twelve triangle cases and their geometry inputs.

The input is `thelemmy/nemu64-test` commit
`9a8b9f7d94ee2f6f57d7feed70c98c22cdc30e6c`. The [validation results](validation-results.md)
record ROM and PIF hashes, platform results, and remaining timing failures.
Earlier unmodified images fail eleven of the twelve triangle cases. Corrected
images have their own source manifest and hashes.

## Command and framebuffer packing

The test source defines `ARGB8888` with alpha in bits 31:24, followed by red,
green, and blue. The original `RDPAssembler::set_blendcolor` writes that value
unchanged to the command. The SDK's `DPRGBColor` and `gDPSetBlendColor` macros encode
red in bits 31:24 and alpha in bits 7:0. See the
[SDK command header](https://ultra64.ca/files/documentation/online-manuals/man/header/gbi.htm).

For example, the degenerate-rectangle fixture supplies `0xffff0000` as red.
The RDP interprets those bytes as yellow with alpha zero. With full coverage,
the resulting RGBA32 framebuffer word is `0xffff00e0`. The fixture expects
`0xe0ff0000`, putting coverage in the high byte. Its pixel geometry matches
for this case; the disagreement is already visible in a fully covered pixel,
without fractional-edge or perspective behavior.

The correction converts the blend-color payload to RGBA and converts RGBA32
framebuffer reads back to ARGB before comparison. The fill-color helper is
unchanged; these fixtures clear with raw black zero.

The affected files in the pinned checkout are:

- `src/graphics/color.rs`: ARGB storage and color constants.
- `src/rdp/rdp_assembler.rs`: unconverted blend-color command payload.
- `src/tests/rdp/filled_triangle.rs`: ARGB framebuffer reads and CPU expectations.

At pinned commit `9a8b9f7d94ee2f6f57d7feed70c98c22cdc30e6c`, their
SHA-256 hashes with LF line endings, matching the Git blobs, are:

| Source file | SHA-256 |
| --- | --- |
| `src/tests/rdp/filled_triangle.rs` | `c40643bb8a05a005c9b51ebc8ff06f56cf36a025335c1888ff17dd13f7a52544` |
| `src/rdp/rdp_assembler.rs` | `48644e60608b76201c275320fda50ca3bdec95e14e8ba87e172278dd5918108f` |
| `src/graphics/color.rs` | `8eae1bef4007aa5c368741e44915b0699797a3ba02bad068a9ed0a6dbc3ce1b4` |

## Coverage expectations

The original CPU renderer in `filled_triangle.rs` counts sixteen samples per pixel and
maps selected counts through an incomplete table; unlisted counts are returned
directly as alpha. The RDP coverage path uses eight staggered samples. The
fixture's one-cycle mode also leaves antialiasing disabled, while its CPU
renderer accepts any nonzero partial coverage. Fractional edges therefore
have additional disagreements beyond the byte order.

The corrected oracle evaluates eight staggered samples independently for each
pixel. It uses the whole-row top origin, the lower edge's own middle-Y origin,
the command's quantized slopes, and signed 28-bit edge arithmetic. With
antialiasing disabled, a pixel is accepted only when coverage sample zero is
present. All twelve cartridge cases select `CoverageMode::Zap`, which stores
coverage `0xe0` in each accepted pixel. They do not exercise the other coverage
storage modes; the raw-command regressions below provide separate coverage
checks.

The original failing cases are degenerate rectangle, flat-top triangle, left/top/right/
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

The edge walker starts at the whole row containing `YH`, before top clipping.
It changes the minor edge only when it reaches `YM`. A middle coordinate before
that initial row is never reached, so the upper edge remains active. A middle
coordinate inside the initial row can be reached before the first visible
sample; comparing `YM` with the unclamped fractional `YH` would also be wrong.

The packet discussed in issue #59 has `YH=1`, `YM=-1`, `YL=4`, a major edge at
X=2, an upper edge at X=0, and a lower edge at X=4. It retains the upper edge,
leaving a reversed span and no framebuffer writes. The earlier change selected
the lower edge immediately, and its cartridge oracle made the same mistake.
Both now retain the initial-row condition. Literal packets also check a middle
coordinate equal to that origin, a switch before a fractional top, a later
fractional switch, and clipping after the switch. The fill-cycle tests check
the corresponding inclusive spans. Physical captures for malformed coordinate
orders remain part of issue #37.

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
| Middle Y before the initial row | 88 | `57183d2da120056bfa2fa0900546eab0e91c1d8a30103a3cf8b2d7e32dba4aff` |

The raw packets isolate rendering rules from the cartridge's command helpers.
The maintained cartridge retains every triangle case and compares complete
framebuffers against the corrected CPU oracle. Broader rendering modes and
independent hardware captures remain part of issue #37's conformance work.
