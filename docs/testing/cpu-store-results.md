# CPU cache stores and conversion validation (2026-09-29)

Native CPU blocks now stage aligned SB, SH, SW, and SD cache-hit writes and
commit them only after every address check passes. Interrupt replay restores
overlapping bytes and dirty flags before retiring the interrupted prefix.
Loads following a staged store retain ordinary execution. The
[CPU guide](../hardware/cpu-native.md) describes the cache and clock boundaries.

Integer-to-floating-point conversion computes IEEE bits with integer arithmetic.
Cached slices admit supported conversions only after checking live operands,
rounding, exception enables, and latency. Unsupported inputs retain the ordinary
exception path. The [floating-point guide](../hardware/floating-point.md)
describes these checks. Windows builds also compile the VSAR selector without
a constant-condition warning and allow the large instrumented RSP object.

## Local validation

| Check | Clang 21.1.5 Release | Clang 21.1.5 portable | Clang 21.1.5 ASan/UBSan | MSVC 19.43.34809 desktop |
| --- | ---: | ---: | ---: | ---: |
| Hardware and host regressions | 1,534/1,534 | 1,534/1,534 | 1,534/1,534 | 1,534/1,534 |
| Prepared default cartridge | 4,637/4,637 | 4,637/4,637 | 4,637/4,637 | 4,637/4,637 |
| Prepared extended cartridge | 6,273/6,273 | 6,273/6,273 | 6,273/6,273 | 6,273/6,273 |
| CTest groups | 9/9 | 9/9 | 9/9 | 15/15 |

Each configuration ran the complete local validation entry point, including
67 tooling tests, clang-format 22.1.0, stepped/batched cartridge comparisons,
and default cold/warm boots. The portable build disabled both native backends.
All reports verified unchanged source and inputs. Sanitizer logs contain no
diagnostics. The 512-file source fingerprint is
`a8d27d791d3813d24abd53b5621d80f0d346301d4a2b7cc3b25b24ed1ca5b78f`.

Reports are retained at:

- `.work/reports/stores-native/dbb5792fd8a348149eb463b68a5aeeb5/report.json`
- `.work/reports/stores-portable/b69a86d0d546483daa5399826914e9e9/report.json`
- `.work/reports/stores-sanitize/29e56550bce94286a8acab2a0798dab9/report.json`
- `.work/reports/stores-msvc-desktop/1b55669175394404b51454644b7f4dd1/report.json`

The prepared ROMs retain the existing
[fixture corrections](cartridge-fixtures.md) and hashes recorded in the
[preceding validation](cpu-rendering-results.md). Original-image acceptance
remains open in #5 and #39.

## Super Mario 64

The same supplied USA ROM and NTSC configuration used in the preceding captures
match at every field after these changes. Complete PCM streams and the
3,000-field replay's EEPROM bytes match the retained comparisons.

| Capture | Fields | CPU instructions | Stereo sample pairs | Wall time | Fields per host second |
| --- | ---: | ---: | ---: | ---: | ---: |
| Title | 1,200 | 1,007,904,148 | 657,643 | 42.666 s | 28.1 |
| Controller replay | 3,000 | 3,189,257,346 | 1,620,629 | 96.305 s | 31.2 |

These unprofiled measurements used the Windows 11 Intel Core i7-13700H host,
performance-core affinity, and disabled process power throttling. They do not
establish a speed improvement or full-speed playback. Sustained gameplay and
normal audible playback remain open in #48 and #47.

This results record and its index link were added after validation.
Implementation and regression-test bytes were unchanged.
