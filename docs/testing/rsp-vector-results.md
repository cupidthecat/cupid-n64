# Native RSP vector results

Date: 2026-09-30. Initial parent: `ea7a397e6550e957aa5a303eb2a4c19e7432e4b4`.
Additional multiply operations were validated after `f99041c`.

Compiled blocks now emit SSE2 instructions for VMULF, VMULU, VMUDL, VMUDM,
VMUDN, VMUDH, VMACF, VMACU, VMADL, VMADM, VMADN, VMADH, VSAR, and the six
vector logical operations. Fractional products retain rounding and the positive
endpoint; accumulating operations propagate carries through all three slices.
Signed middle, unsigned middle, and unsigned low destinations retain their
distinct saturation rules. The
[execution guide](../hardware/rsp-native.md) describes register storage, helper
boundaries, accumulator behavior, and the portable fallback.

## Observation correction

The earlier context test encoded SQV output addresses from `0x400` through
`0x630` with a zero base and a signed seven-bit displacement. Those stores
wrapped into another DMEM region, while the test read unchanged output bytes.
The corrected test uses an explicit `0x400` base, keeps its flag-transfer
register separate, and requires a nonzero output sentinel.

A private fault experiment zeroed an otherwise untouched vector register after
each helper operation. The original test passed. The corrected context test and
both new scalar-oracle cases failed. The fault is absent from production code.
This establishes that the corrected fixture observes register corruption that
the original fixture missed.

The oracle checks all 19 emitted operations, all 16 element selections,
four register-alias arrangements, and five input patterns. It compares every
vector register, each accumulator slice, and all control flags. Repeated
products cross saturation and 48-bit wrapping boundaries. Seeds include
nontrivial upper slices and the 48-bit sign boundary. A 16-instruction mixed
block checks accumulator dependencies and temporary-register reuse across a
VMULQ helper call.

## Complete validation

All four configurations passed the same 528-file source snapshot, with hash
`e0a3b8737d28ac44e502c97a716c85a82f23732423742eea57791ff9e93f2cae`.
This results page was updated after that snapshot; tested implementation and
regression files are unchanged. Source and input integrity
checks passed in every report.

| Check | Clang Release | Clang portable | Clang ASan/UBSan | MSVC desktop Release |
| --- | ---: | ---: | ---: | ---: |
| Hardware and host regressions | 1,543/1,543 | 1,543/1,543 | 1,543/1,543 | 1,543/1,543 |
| Prepared default cartridge | 4,637/4,637 | 4,637/4,637 | 4,637/4,637 | 4,637/4,637 |
| Default cold and warm boots | 4,637 each | 4,637 each | 4,637 each | 4,637 each |
| Prepared extended cartridge | 6,273/6,273 | 6,273/6,273 | 6,273/6,273 | 6,273/6,273 |
| CTest groups | 9/9 | 9/9 | 9/9 | 15/15 |

Each configuration passed 67 tooling regressions and clang-format 22.1.0.
The sanitizer logs contain no diagnostics. Clang 21.1.5 used Release
optimization, ThinLTO, and strict floating-point flags. MSVC used version
19.43.34809. The portable configuration disabled both native backends.
Prepared cartridges retain the [documented fixture corrections](cartridge-fixtures.md);
original-image acceptance remains open in #5 and #39.

Reports are retained under `.work/reports/`:

- `vector-products-native/d02aea829e9f4403a7820fe8d13fec27/report.json`
- `vector-products-portable/ec09501a59c141d9bce4940c407f04c1/report.json`
- `vector-products-sanitize/7756049679424c888489b10db4e5817f/report.json`
- `vector-products-msvc/52e9ba2c9f904843a8f7cc06f178e559/report.json`

The initial 12-operation implementation also passed all four configurations,
using the 527-file source snapshot
`a7fddd3290d6bf90ccf1a5083fd1ef5311d88536a39c7cc7ad11f56190aa9e60`.
Those reports remain under `inline-vectors-*` in the same report directory.

## Super Mario 64

The final build preserves all 3,000 controller fields and all 7,200 courtyard
fields from retained captures. Complete PCM streams, EEPROM bytes, CPU cycles,
instruction totals, and native execution counters also match. Inspected frames
retain the outdoor scene and Mario swimming beneath the bridge. These are
regression comparisons; matching retained captures does not independently prove
hardware accuracy or normal audible playback.

| Replay | Fields | Host time | VI fields per host second |
| --- | ---: | ---: | ---: |
| Controller | 3,000 | 81.919 s | 36.6 |
| Courtyard movement | 7,200 | 193.233 s | 37.3 |

Both runs explicitly connected a gamepad, started with a fresh EEPROM file,
and used the same input scripts as their retained comparisons. The Windows
Intel Core i7-13700H host applied performance-core affinity and process priority.
The controller run had no concurrent builds or test suites. The courtyard
capture overlapped a brief private build and focused tests; its timing is not
an isolated performance measurement. Separate capture times and host variation
also limit performance comparisons. A preceding paired controller
experiment took 82.492 seconds with emitted vectors and 82.061 seconds without
them, so the measurements do not establish a consistent whole-game speed gain.
Full-speed gameplay and audible-playback acceptance remain open in #48 and #47.

Complete PCM SHA-256 values:

- Controller: `9ecae818c0daa6a64aab847c58536909a887dd5e06f8f47af85bd8f3625fd6ad`
- Courtyard: `188303a792349e8c7b4ee7e5e0f144af3a71df6528efe6be7fc651ea38324360`

Both EEPROM files have SHA-256
`ed22a78d4e77ded30cd61898a7aa03a355a1db16d15b5a19d24a4538aac9020a`.
