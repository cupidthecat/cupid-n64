# Nested RDRAM summary results

Date: 2026-09-30. Parent revision: `dad9cde7915b1ba460e482104c150e649d22466d`.

An inner raster task could publish its bank summary while an outer scope was
still collecting accesses. The outer summary then lost the intervening row
changes. Returning to the original row could retain dirty bits that serial
execution had cleared. Accesses also missed an enclosing scope when a scope
for another machine lay above it on the same host thread.

Accesses and summary merges now find the nearest scope for their own memory.
Inner summaries append their row effects to that scope. Canonical bank state
remains deferred until the enclosing summary is merged outside the scope.
First and final rows, row transitions, dirty bits, and RCP timestamps preserve
serial access order.

## Regression proof

All three new cases fail against the saved parent libraries:

- The inner merge publishes bank status `0xccff` while the enclosing scope
  should leave it at `0x0000`.
- The final summary leaves status `0xffff` instead of serial status `0xddff`.
- An intervening scope for another machine publishes status `0x0001` before
  the matching enclosing scope is merged.

The changed core passes all three. The focused RDRAM, RI, CPU row/refresh,
parallel RDP, parallel VI, and task set passes 92/92. The new cases compare
memory bytes, bank status, row validity, clocks, error flags, and bank access
timestamps. The separate-machine case also verifies that its bank state stays
independent.

## Complete validation

All four configurations passed the same 523-file source snapshot, with hash
`f92879d9a6e0799ee22a16539aac1819dc63d5a12392820c5746e35f52628cbd`.
Source and input integrity checks passed in every report.

| Check | Clang Release | Clang portable | Clang ASan/UBSan | MSVC desktop Release |
| --- | ---: | ---: | ---: | ---: |
| Hardware and host regressions | 1,541/1,541 | 1,541/1,541 | 1,541/1,541 | 1,541/1,541 |
| Prepared default cartridge | 4,637/4,637 | 4,637/4,637 | 4,637/4,637 | 4,637/4,637 |
| Default cold and warm boots | 4,637 each | 4,637 each | 4,637 each | 4,637 each |
| Prepared extended cartridge | 6,273/6,273 | 6,273/6,273 | 6,273/6,273 | 6,273/6,273 |
| CTest groups | 9/9 | 9/9 | 9/9 | 15/15 |

Every configuration passed 67 tooling regressions and clang-format 22.1.0.
The sanitizer run reported no diagnostics. Clang 21.1.5 used Release
optimization, ThinLTO, and strict floating-point flags. MSVC used version
19.43.34809. The portable build disabled both native backends.

The prepared cartridges retain the
[documented fixture corrections](cartridge-fixtures.md). Original-image
acceptance remains open in #5 and #39.

Reports are retained under `.work/reports/`:

- `nested-rdram-native/fb78f735712f4d76bc465236a6a7c75f/report.json`
- `nested-rdram-portable/5cf3740fe0ea45c7a14040f31cb95702/report.json`
- `nested-rdram-sanitize/41e1a5f4bfe9432691b80096dc23d680/report.json`
- `nested-rdram-msvc-desktop/885eaed69f9546d19bb09ea891c9f8ca/report.json`

## Super Mario 64

The production build matched all 3,000 controller fields and all 7,200 courtyard
fields against retained captures. Complete PCM streams, EEPROM bytes, CPU
cycles, instruction totals, and native CPU/RSP counters also matched. Inspected
frames retain the outdoor scene and Mario swimming beneath the bridge, with
textures, depth, water, and HUD output.

| Replay | Fields | Host time | VI fields per host second |
| --- | ---: | ---: | ---: |
| Controller | 3,000 | 62.104 s | 48.3 |
| Courtyard movement | 7,200 | 175.039 s | 41.1 |

The Windows host used an Intel Core i7-13700H. Both captures applied the same
process priority and P-core affinity as their retained comparisons and ran
without concurrent builds or test suites. Separate capture times and observed
host variation limit performance comparisons. These runs remain below full
emulated speed.

The complete PCM hashes are
`9ecae818c0daa6a64aab847c58536909a887dd5e06f8f47af85bd8f3625fd6ad` for
the controller replay and
`188303a792349e8c7b4ee7e5e0f144af3a71df6528efe6be7fc651ea38324360` for
the courtyard replay. EEPROM output hashes to
`ed22a78d4e77ded30cd61898a7aa03a355a1db16d15b5a19d24a4538aac9020a`.

The nested bank-state defect is tracked in #84. Full-speed gameplay and normal
audible-playback acceptance remain open in #48 and #47.
