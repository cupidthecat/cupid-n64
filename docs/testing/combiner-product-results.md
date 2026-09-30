# RGB combiner arithmetic validation (2026-09-30)

The full RGB combiner equation now uses SSE2 on supported hosts. Expanded
differences fit signed 10-bit values, and multipliers fit signed 9-bit values.
Packing these operands with interleaved zeros produces four exact 32-bit
products. The existing rounding, arithmetic shift, and expanded D input remain
unchanged. Direct-D equations, color keying, alpha, coverage, and scalar hosts
retain their existing paths. The [color guide](../hardware/rdp-color.md)
describes the equations and ordering.

## Local validation

| Check | Clang 21.1.5 Release | Clang 21.1.5 portable | Clang 21.1.5 ASan/UBSan | MSVC 19.43.34809 desktop |
| --- | ---: | ---: | ---: | ---: |
| Hardware and host regressions | 1,538/1,538 | 1,538/1,538 | 1,538/1,538 | 1,538/1,538 |
| Prepared default cartridge | 4,637/4,637 | 4,637/4,637 | 4,637/4,637 | 4,637/4,637 |
| Prepared extended cartridge | 6,273/6,273 | 6,273/6,273 | 6,273/6,273 | 6,273/6,273 |
| CTest groups | 9/9 | 9/9 | 9/9 | 15/15 |

Every configuration passed the complete validation entry point, including 67
tooling tests, clang-format 22.1.0, stepped/batched cartridge comparisons, and
default cold/warm boots. Portable execution disabled both native backends;
it still uses the host's SSE2 arithmetic. A separate focused build undefining
`__SSE2__` passed all 44 color-combiner cases through the scalar fallback.
Sanitizer logs contain no diagnostics. Reports verified unchanged source and
inputs. The 520-file source fingerprint is
`694ea7d00925a2decce841ea91b76eb6995e9d72349b5a32121350c677d7f178`.

Reports are retained at:

- `.work/reports/combiner-native/63641316d21f411ab7d800a987ae0d1a/report.json`
- `.work/reports/combiner-portable/bb04b89e14ae4af097db9c69be156b64/report.json`
- `.work/reports/combiner-sanitize/5dd6dd852dd44e9f8f370e437dfba378/report.json`
- `.work/reports/combiner-msvc-desktop/7cef7521b8754a9a86c740febc5d0e72/report.json`
- `.work/combiner-scalar-tests.log`

The new regression compares the prepared combiner with the ordinary equation
for all 1,023 differences and all 512 encoded multipliers, using six expanded
D values, unequal RGB lanes, alpha-coverage selection, coverage, and dither.
It checks 3,142,656 combinations. Existing cases retain direct-D, two-cycle,
keying, and alpha coverage. The prepared cartridges retain the documented
[fixture corrections](cartridge-fixtures.md); original-image acceptance remains
open in #5 and #39.

## Super Mario 64

The arithmetic was first measured in a separate implementation between
unmodified captures on the Windows 11 Intel Core i7-13700H host. Each replay
used performance-core affinity and disabled process power throttling, with no
concurrent build or validation job.

| Capture | Baseline A | SIMD arithmetic | Baseline B |
| --- | ---: | ---: | ---: |
| 3,000-field controller replay | 69.025 s | 67.259 s | 72.683 s |
| 7,200-field courtyard replay | 191.688 s | 178.514 s | 182.511 s |

Every field record and the complete PCM stream matched both baselines. EEPROM
bytes also matched. The courtyard replay includes movement, jumping, camera
input, and swimming. The measured improvement in this experiment is modest.

The production implementation also preserves all 3,000 controller fields and
all 7,200 courtyard fields, their complete PCM streams, execution observations,
and EEPROM bytes. The title head and the courtyard swimming scene were
inspected. These captures used the same host settings without concurrent
build or validation work.

| Production capture | Wall time | Fields per host second |
| --- | ---: | ---: |
| 3,000-field controller replay | 62.599 s | 47.9 |
| 7,200-field courtyard replay | 191.848 s | 37.5 |

The production courtyard timing does not establish an overall improvement over
the preceding 191.688-second capture. Host timing varies across these batches;
the complete output comparisons establish behavioral parity, while sustained
60 Hz VI output remains unmet.

The 3,000-field replay executed 3,189,257,346 instructions and 4,733,062,618 CPU
cycles, producing 1,620,629 stereo sample pairs. Complete PCM SHA-256 is
`9ecae818c0daa6a64aab847c58536909a887dd5e06f8f47af85bd8f3625fd6ad`.
The 7,200-field replay executed 7,368,396,134 instructions and 11,314,637,598 CPU
cycles, producing 3,867,593 stereo sample pairs. Complete PCM SHA-256 is
`188303a792349e8c7b4ee7e5e0f144af3a71df6528efe6be7fc651ea38324360`.
Both EEPROM captures have SHA-256
`ed22a78d4e77ded30cd61898a7aa03a355a1db16d15b5a19d24a4538aac9020a`.
Normal audible playback remains open in #47; full-speed gameplay remains open
in #48.

This record and its index link were added after validation. Implementation and
regression-test bytes were unchanged.
