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

Local regressions now use explicit RGBA command bytes, eight-sample coverage,
and separate expectations for antialiasing enabled and disabled. Two older
local assertions that assumed ARGB triangle output were corrected. Those
checks do not replace the external tests or establish hardware conformance
for every randomized edge case. Corrected cartridge fixtures and independent
hardware captures remain necessary; issue #37 tracks that validation work.
