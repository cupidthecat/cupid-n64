# Native RSP vector results

Date: 2026-09-30. Initial parent: `ea7a397e6550e957aa5a303eb2a4c19e7432e4b4`.
Additional multiply operations were validated after `f99041c`. Accumulator
register caching and destination analysis were added after `1bb83a4`.

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
VMULQ or VMACQ helper call. Additional chains check overwritten results,
late consumers, memory and transfer helpers, and accumulator flushing before
both fast and wrapped scalar loads. Removing the pre-branch flush in a private
fault experiment makes the load-boundary regression fail; production retains
that flush.

## Complete validation

All four configurations passed the same 530-file source snapshot, with hash
`bf9ffe0953b42e55a17c324743c0e91e3c0266c0a966875b4ed9fac646d70c82`.
This results page was updated after that snapshot; tested implementation and
regression files are unchanged. Source and input integrity
checks passed in every report.

| Check | Clang Release | Clang portable | Clang ASan/UBSan | MSVC desktop Release |
| --- | ---: | ---: | ---: | ---: |
| Hardware and host regressions | 1,546/1,546 | 1,546/1,546 | 1,546/1,546 | 1,546/1,546 |
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

- `vector-cache-native/dd05d84f764e4b16bcdaa2f907d2efb5/report.json`
- `vector-cache-portable/57af1e25d27e4c859bb42ba61868aa05/report.json`
- `vector-cache-sanitize/5d4539b3982e4d8f8b10daa67d1257f4/report.json`
- `vector-cache-msvc/3de4b235c4d64da78c2747eaa6b4fec0/report.json`

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
| Controller | 3,000 | 60.547 s | 49.5 |
| Courtyard movement | 7,200 | 170.101 s | 42.3 |

Both runs explicitly connected a gamepad, started with a fresh EEPROM file,
and used the same input scripts as their retained comparisons. The Windows
Intel Core i7-13700H host applied performance-core affinity and process priority.
The controller capture overlapped the end of a private focused test; the
courtyard capture had no concurrent local builds or test suites. Separate
capture times and host variation limit comparisons. A paired controller run
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
