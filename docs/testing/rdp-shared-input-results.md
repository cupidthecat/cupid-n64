# RDP shared-input validation (2026-10-01)

The rendering changes preserve the complete retained SM64 observations and
reduce elapsed time in both long replay pairs. The gain is modest; these runs
remain below full NTSC speed.

## Pixel behavior

Prepared combiner plans retain references to shared RGBA inputs. Matching RGB
and alpha products use all four packed lanes; distinct alpha selectors and
color keying keep their separate equations. Shared direct terms retain the
same nine-bit expansion. The blender defers unpacking unused colors.

Image-read triangles can reject depth before sampling textures when alpha
comparison and alpha-scaled coverage are disabled. Framebuffer color and
coverage are read before depth and retained for passing pixels. All eight
coverage bits use a fixed population-count table.

RGBA16 decode and positive-W perspective setup use read-only tables. TMEM
addressing, palette banks, ROM reciprocal rounding, signed coordinates,
overflow, and nonpositive W retain their existing behavior.

## Regression checks

Four new checks cover:

- All 65,536 RGBA16 values through both ordinary texture reads and TLUT lookup.
- All 32,767 positive W values, with 1,310,680 comparisons against a separate
  ROM-base/slope oracle using integer floor division and signed bounds.
- 262,144 scalar/prepared combiner comparisons, including shared products,
  direct terms, different alpha inputs, signed nine-bit boundaries, cycle
  feedback, keying, and alpha/coverage controls.
- 512 depth scenes comparing early image-read testing with a zero-threshold
  alpha comparison that forces depth through the ordinary pixel path. Visible
  memory, hidden bits, and all six fields of every RDRAM bank summary match.

The private full build passes all 1,571 hardware and host regressions. A separate
8,192-scene comparison against the preceding implementation also matches all
framebuffer/hidden hashes and ordered bank summaries. It covers both color
sizes, 64 mode combinations, all eight stored coverage values, four stored
depth values, and shared color/depth addresses.

All 45 focused combiner checks also pass with `CUPID_RDP_FORCE_SCALAR`, including
the existing signed-product oracle and the new shared-RGBA cases.

## Compiler and cartridge validation

The complete local matrix passes all 1,571 regressions, 4,637 prepared default
cartridge cases, 6,273 prepared extended cases, stepped/batched comparisons,
default cold/warm boots, capture/storage checks, 67 tooling regressions, and
clang-format 22.1.0. MSVC also passes the desktop groups. Source and input
integrity checks pass in every configuration; sanitizer logs contain no
diagnostics. Original-image acceptance remains open in #5 and #39.

| Configuration | CTest groups | Elapsed time | Retained report directory |
| --- | ---: | ---: | --- |
| Clang 21.1.5 Release | 9 | 711.85 s | `.work/reports/rdp-shared-input-native-r2/ba0e4e201e304e4392c2b017d53b5244` |
| Clang with both native backends disabled | 9 | 488.16 s | `.work/reports/rdp-shared-input-portable-r2/557ea415fb394edeb2f9a765e5992ce3` |
| MSVC 19.43 desktop Release | 15 | 606.31 s | `.work/reports/rdp-shared-input-msvc-r2/c8ae2f7b6809427fa5d5a213ff4e9641` |
| Clang ASan/UBSan | 9 | 1169.05 s | `.work/reports/rdp-shared-input-sanitize-r2/c3f1a764d218480eb4fe4ce6581a6502` |

The 542-file validated source snapshot has parent revision `acdb769` and SHA-256
`48fd7e0f612fd9bda085677525d5ea875d0d282ff915962b15f2cf8be62e62b7`.
The table and validation summary were added after the matrix; implementation,
regression tests, and hardware guides are unchanged.

The first incremental matrix retained some objects compiled against the old
public headers because the copied header timestamps predated those objects.
Those runs failed RSP, host, and capture checks. Rebuilding every dependent
object resolves the failures without changing emulation code or assertions.
The retained successful reports above come from that rebuilt matrix.

## SM64 comparisons

The ordinary and candidate builds use the same Windows Clang 21.1.5 Release
settings, strict floating-point behavior, core/host interprocedural optimization,
and replay harness. Each measurement runs alone on the Intel Core i7-13700H
host with performance-core affinity and disabled process power throttling.
The 7,200-field courtyard sequence uses the supplied USA cartridge and PIF,
gamepad input, and a fresh EEPROM-4Kbit save.

| Pair | Order | Preceding core | Rendering changes | Rendering fields per host second |
| --- | --- | ---: | ---: | ---: |
| 1 | Rendering, preceding | 191.042 s | 183.038 s | 39.3 |
| 2 | Preceding, rendering | 176.516 s | 171.986 s | 41.9 |

The reductions are 4.2% and 2.6%. Host conditions vary between pairs, so the
within-pair comparisons carry more weight than their absolute rates. The
shorter 3,000-field run takes 70.743 seconds and does not establish a gain over
the earlier preceding-core capture.

Every field, complete PCM stream, EEPROM byte, emulated clock, guest instruction
count, and native CPU/RSP instruction count matches in all four long runs.
The long PCM SHA-256 remains
`188303a792349e8c7b4ee7e5e0f144af3a71df6528efe6be7fc651ea38324360`.
Retained comparisons are `.work/render-hot-reuse-{render,control}-courtyard-comparison.json`
and `.work/render-hot-reuse-{render-r2,control-r2}-courtyard-comparison.json`.

Sustained full-speed gameplay and normal audible playback remain open in #48
and #47.
