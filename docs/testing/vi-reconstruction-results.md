# VI reconstruction validation (2026-10-01)

Coverage reconstruction and dither restoration now process the three color
channels together on SSE2 hosts. The scalar implementation remains available
on other architectures. Neighbor selection, signed rounding, repeated-row
handling, divot order, and output clamping retain their preceding behavior.
The [scanout guide](../hardware/video-scanout.md) describes these rules.

Two new regressions compare 65,536 coverage cases with an independently sorted
sample oracle and 11,520 dither cases with a neighbor-vote oracle. The private
comparison probe also checks the preceding scalar filter across both pixel
formats, all antialias modes, wrapped addresses, repeated rows, divot settings,
and row-cache boundaries. All 85 probe checks pass with SIMD enabled and with
the scalar fallback forced.

## Complete validation

All four configurations passed the same 535-file source snapshot, SHA-256
`24b9720c7dcdbab95c5e5f4ff3fa9105f32b74f09a6e611d5e5ff45a8b8b7dec`.
Every report verified unchanged source and inputs.

| Check | Clang Release | Clang portable | Clang ASan/UBSan | MSVC desktop Release |
| --- | ---: | ---: | ---: | ---: |
| Hardware and host regressions | 1,565/1,565 | 1,565/1,565 | 1,565/1,565 | 1,565/1,565 |
| Prepared default cartridge | 4,637/4,637 | 4,637/4,637 | 4,637/4,637 | 4,637/4,637 |
| Default cold and warm boots | 4,637 each | 4,637 each | 4,637 each | 4,637 each |
| Prepared extended cartridge | 6,273/6,273 | 6,273/6,273 | 6,273/6,273 | 6,273/6,273 |
| CTest groups | 9/9 | 9/9 | 9/9 | 15/15 |

Each configuration passed 67 tooling regressions, clang-format 22.1.0, and
stepped/batched cartridge comparisons. Clang 21.1.5 used Release optimization,
ThinLTO, and strict floating-point flags. MSVC used version 19.43.34809.
The portable configuration disabled both native execution backends.
Sanitizer logs contain no diagnostics; desktop checks include audio lifecycle
and device-failure handling. The prepared cartridges retain the documented
[fixture corrections](cartridge-fixtures.md).

Reports are retained under `.work/reports/`:

| Configuration | Report directory |
| --- | --- |
| Clang Release | `vi-simd-native/ab82bcc0fe0f4bb5a829657baf712ac6` |
| Clang portable | `vi-simd-portable/3ef27a90f7fc40c6bb279182c987e15a` |
| Clang ASan/UBSan | `vi-simd-sanitize/cd0cc7d0991142339d1b1016ebf79f05` |
| MSVC desktop Release | `vi-simd-msvc/33f0de29e26c4ffcac76c072a6c2465d` |

This result page, its index entry, and the conditional constructor's formatting
were finalized after those runs. The constructor retains the same executable
operations; all 106 focused VI checks pass after formatting. The regressions
and hardware guide are unchanged.

## Super Mario 64 replay

The supplied USA cartridge, NTSC timing, PIF boot, 8 MiB RDRAM, gamepad on port
1, and EEPROM 4 Kbit produced these complete captures on the Windows 11
Intel Core i7-13700H host:

| Replay | Fields | Wall time | Fields per host second |
| --- | ---: | ---: | ---: |
| Title, file selection, and movement | 3,000 | 62.231 seconds | 48.2 |
| Courtyard, water, and sustained movement | 7,200 | 173.094 seconds | 41.6 |

Neither capture had concurrent local builds or tests. Every field observation,
complete PCM stream, EEPROM byte, guest clock, instruction count, and native
execution count matches the preceding CPU control-flow captures. Both CPU and
RSP native totals remain unchanged.

The short PCM SHA-256 is
`9ecae818c0daa6a64aab847c58536909a887dd5e06f8f47af85bd8f3625fd6ad`;
the long PCM SHA-256 is
`188303a792349e8c7b4ee7e5e0f144af3a71df6528efe6be7fc651ea38324360`.
Both EEPROM files have SHA-256
`ed22a78d4e77ded30cd61898a7aa03a355a1db16d15b5a19d24a4538aac9020a`.
Captures and complete comparisons remain under `.work/sm64/` and `.work/`,
using the `vi-rgb-simd-production` prefix. Private probes and cartridge files
are excluded from the repository.

An isolated row benchmark alternated execution order across three pairs per
pixel format and repeated-row setting. Its checksums agree in every pair, and
the SIMD implementation runs 1.7–2.2 times faster. A separate four-run controller
comparison took 65.799 and 86.367 seconds before the change, and 71.147 and
63.878 afterward. Those timings varied enough that they do not establish a
consistent whole-game gain. The production captures above also remain below
full NTSC speed. Sustained gameplay and normal audible playback remain open in
#48 and #47.
