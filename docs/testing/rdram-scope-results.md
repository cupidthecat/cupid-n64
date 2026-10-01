# RDRAM scope initialization results

Date: 2026-09-30. Parent revision: `152a01d311e4eaaf63816af547c3f4c04ce86c4d`.

The per-thread RDRAM scope pointer uses constant initialization. Parallel RDP
transfers retain complete bank tracking while removing the host-thread
initialization check from each scoped memory access. An optimized Clang
assembly inspection confirms that the bank-tracking path no longer performs
that check or saves registers for its initialization call.

The access summaries retain their first and last rows, row-change state, dirty
bits, and original RCP timestamps. The caller still merges summaries in raster
order. Scope restoration and independent machines retain their existing rules.

## Focused checks

All 89 RDRAM, RI, CPU row/refresh, parallel RDP, parallel VI, and task regressions
passed against the changed tracking path. These include remapping, hidden bits,
refresh recovery, summary ordering, thread lifecycle, nested jobs, and concurrent
machines.

## Complete validation

All four configurations passed the same 521-file source snapshot, with hash
`fd4cb21c07793d760b298ae21c00ea8be231fdc0e4fe845b6f97bf6de9a43914`.
Source and input verification passed in every report.

| Check | Clang Release | Clang portable | Clang ASan/UBSan | MSVC desktop Release |
| --- | ---: | ---: | ---: | ---: |
| Hardware and host regressions | 1,538/1,538 | 1,538/1,538 | 1,538/1,538 | 1,538/1,538 |
| Prepared default cartridge | 4,637/4,637 | 4,637/4,637 | 4,637/4,637 | 4,637/4,637 |
| Default cold and warm boots | 4,637 each | 4,637 each | 4,637 each | 4,637 each |
| Prepared extended cartridge | 6,273/6,273 | 6,273/6,273 | 6,273/6,273 | 6,273/6,273 |
| CTest groups | 9/9 | 9/9 | 9/9 | 15/15 |

Every configuration passed 67 tooling regressions and clang-format 22.1.0.
No sanitizer diagnostics were reported. Clang 21.1.5 used Release optimization,
ThinLTO, and strict floating-point flags. MSVC used version 19.43.34809.
The portable build disabled both native backends. The prepared cartridges
retain the [documented fixture corrections](cartridge-fixtures.md); original-ROM
acceptance remains open in #5 and #39.

Reports are retained under `.work/reports/`:

- `rdram-scope-native/cee0ab1935bb48e58aa84fc056d87d81/report.json`
- `rdram-scope-portable/b4e959ceef0d45e19b3d83e39a528a58/report.json`
- `rdram-scope-sanitize/6aad44c4f603430f99950270bb702f31/report.json`
- `rdram-scope-msvc-desktop/1c3e7c17b23146c4b10076c14934cbdf/report.json`

## Super Mario 64

The isolated tracking change matched all 3,000 controller fields and all 7,200
courtyard fields against retained captures. Complete PCM streams, EEPROM bytes,
CPU cycles, instruction totals, and native CPU/RSP execution counters also matched.

| Replay | Retained baseline | Scope change | VI fields per host second |
| --- | ---: | ---: | ---: |
| Controller, 3,000 fields | 62.377 s | 61.865 s | 48.5 |
| Courtyard, 7,200 fields | 187.749 s | 175.960 s | 40.9 |

The captures ran at separate times, each without concurrent builds or test
suites. The host was a Windows machine with an Intel Core i7-13700H; captures
applied the same process priority and P-core affinity. The shorter difference
is small, and earlier courtyard baselines ranged from roughly 178 to 192 seconds.
These measurements do not establish sustained full-speed gameplay.

The complete PCM hashes are
`9ecae818c0daa6a64aab847c58536909a887dd5e06f8f47af85bd8f3625fd6ad` for
the controller replay and
`188303a792349e8c7b4ee7e5e0f144af3a71df6528efe6be7fc651ea38324360` for
the courtyard replay. EEPROM output hashes to
`ed22a78d4e77ded30cd61898a7aa03a355a1db16d15b5a19d24a4538aac9020a`.

The final production build also matched every field, complete PCM stream,
EEPROM byte, CPU cycle, instruction total, and native execution counter in both
replays. The inspected frames retain the courtyard, Mario, textures, depth,
and water. The controller replay took 63.588 seconds (47.2 VI fields per second);
the courtyard replay took 197.053 seconds (36.5). These timings do not demonstrate
a consistent whole-game speed improvement. The generated tracking code removes
the initialization overhead, while sustained gameplay still falls short of
full emulated speed.

Full-speed gameplay and normal audible-playback acceptance remain open in #48
and #47.
