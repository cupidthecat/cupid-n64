# Native RSP vector results

Date: 2026-10-01. Initial parent: `ea7a397e6550e957aa5a303eb2a4c19e7432e4b4`.
Additional multiply operations were validated after `f99041c`. Accumulator
register caching and destination analysis were validated after `1bb83a4`.
Four carry-sensitive operations were added after `c2f8976`.
Nine comparison, merge, move, and divider-high operations were added after `6088066`.

Compiled blocks now emit SSE2 instructions for VMULF, VMULU, VMUDL, VMUDM,
VMUDN, VMUDH, VMACF, VMACU, VMADL, VMADM, VMADN, VMADH, VSAR, and the six
vector logical operations, plus VADD, VSUB, VADDC, VSUBC, VABS, VLT, VEQ, VNE,
VGE, VMRG, VMOV, VRCPH, and VRSQH. Fractional
products retain rounding and the positive
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

The oracle checks all 32 emitted operations, all 16 element selections,
four register-alias arrangements, and five input patterns. It compares every
vector register, each accumulator slice, and all control flags. Repeated
products cross saturation and 48-bit wrapping boundaries. Seeds include
nontrivial upper slices and the 48-bit sign boundary. A 16-instruction mixed
block checks accumulator dependencies and temporary-register reuse across a
VMULQ or VMACQ helper call. Additional chains check overwritten results,
late consumers, memory and transfer helpers, and accumulator flushing before
both fast and wrapped scalar loads. Removing the pre-branch flush in a private
fault experiment makes the load-boundary regression fail; production retains
that flush.

Add/subtract cases retain wrapped low accumulator results separately from signed
saturation, preserve upper slices, and check both carry groups. An additional
case consumes every low carry mask for all 16 element selections. Mixed chains
check that carry results stay visible between emitted operations while the
accumulator remains cached. Equality cases check every carry combination, and
merge consumes every VCC mask without changing either comparison group. Partial
writes retain live lanes from a preceding full destination. Divider-high cases
observe shared latches across ordinary low-half helpers, including aliased input
and destination lanes. All 36 focused native RSP checks pass.

## Complete validation

All four configurations passed the same 530-file source snapshot, with hash
`7301aa8f015c51436c9b93b5f694780d6ae5417134c8ca3767add00bef25bf74`.
Source and input integrity checks passed in every report. This results record
was updated after validation; the tested implementation and fixtures are unchanged.

| Check | Clang Release | Clang portable | Clang ASan/UBSan | MSVC desktop Release |
| --- | ---: | ---: | ---: | ---: |
| Hardware and host regressions | 1,553/1,553 | 1,553/1,553 | 1,553/1,553 | 1,553/1,553 |
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

- `vector-divider-native/6cd9c4fe5ef542f599d1349a6edc475a/report.json`
- `vector-divider-portable/ebec463c9ea94b40aeffdd5ba3c86c40/report.json`
- `vector-divider-sanitize/210eed9dbab94748b52d28ce7e6f05ce/report.json`
- `vector-divider-msvc/51d9568fffb14fbcbbf106d2c4251747/report.json`

The preceding 23-operation implementation passed all four configurations with
1,548 hardware and host regressions. Its 530-file snapshot has hash
`09ca93d7398a79e3b7863c2445e50409f3b212cc0699007237d0763fafc15af5`;
those reports remain under `vector-carry-*`.

The preceding accumulator-cache implementation passed all four configurations
with 1,546 hardware and host regressions. Its 530-file snapshot has hash
`bf9ffe0953b42e55a17c324743c0e91e3c0266c0a966875b4ed9fac646d70c82`;
those reports remain under `vector-cache-*`.

The preceding 19-operation implementation passed all four configurations with
1,543 hardware and host regressions. Its 528-file source snapshot has hash
`e0a3b8737d28ac44e502c97a716c85a82f23732423742eea57791ff9e93f2cae`;
those reports remain under `vector-products-*`.

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
| Controller | 3,000 | 60.494 s | 49.6 |
| Courtyard movement | 7,200 | 180.141 s | 40.0 |

Both runs explicitly connected a gamepad, started with a fresh EEPROM file,
and used the same input scripts as their retained comparisons. The Windows
Intel Core i7-13700H host applied performance-core affinity and process priority.
Neither capture had concurrent local builds or test suites. Separate capture
times and host variation limit comparisons. The preceding carry build took
69.316 seconds for the controller capture and 200.943 seconds for the courtyard
capture; its controller run overlapped a private CPU cartridge suite. The cache build
took 60.547 seconds for the controller capture and 170.101 seconds for the
courtyard capture. These separate captures do not establish a consistent
whole-game speed gain from the additions. A paired controller run
with image hashing and PCM file writes disabled took 59.248 seconds before
accumulator caching and 59.101 seconds with it. That small difference does not
establish a consistent whole-game speed gain.

An isolated synthetic benchmark repeatedly executed chains of four, eight,
and sixteen accumulating operations with an overwritten destination. Across
three paired rounds, the combined destination analysis and accumulator caching
ran 1.17 to 1.46 times as fast as the preceding implementation. Its zero inputs
and repeated single-destination chains are not representative gameplay.
Full-speed gameplay and audible-playback acceptance remain open in #48 and #47.

Complete PCM SHA-256 values:

- Controller: `9ecae818c0daa6a64aab847c58536909a887dd5e06f8f47af85bd8f3625fd6ad`
- Courtyard: `188303a792349e8c7b4ee7e5e0f144af3a71df6528efe6be7fc651ea38324360`

Both EEPROM files have SHA-256
`ed22a78d4e77ded30cd61898a7aa03a355a1db16d15b5a19d24a4538aac9020a`.
