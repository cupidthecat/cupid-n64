# CPU code reuse validation (2026-09-30)

Instruction-cache replacement previously discarded generated CPU programs with
their local line plans. Super Mario 64 repeatedly returns to the same sequences,
so that ownership caused repeated compilation. A bounded CPU-owned lookup now
reuses programs keyed by all instruction words and their length. The
[CPU guide](../hardware/cpu-native.md) describes ownership, reset, hardware cache
checks, and execution boundaries.

## Local validation

| Check | Clang 21.1.5 Release | Clang 21.1.5 portable | Clang 21.1.5 ASan/UBSan | MSVC 19.43.34809 desktop |
| --- | ---: | ---: | ---: | ---: |
| Hardware and host regressions | 1,537/1,537 | 1,537/1,537 | 1,537/1,537 | 1,537/1,537 |
| Prepared default cartridge | 4,637/4,637 | 4,637/4,637 | 4,637/4,637 | 4,637/4,637 |
| Prepared extended cartridge | 6,273/6,273 | 6,273/6,273 | 6,273/6,273 | 6,273/6,273 |
| CTest groups | 9/9 | 9/9 | 9/9 | 15/15 |

Every configuration passed the complete validation entry point, including 67
tooling tests, clang-format 22.1.0, stepped/batched cartridge comparisons, and
default cold/warm boots. Portable execution disabled both native backends.
Sanitizer logs contain no diagnostics. All reports verified unchanged source
and inputs. The 517-file source fingerprint is
`67ce458ea453d152da6fc605cba46252722c70699804e717d7e3029df13a898b`.

Reports are retained at:

- `.work/reports/cpu-code-cache-native/5af0a1d4994a44cb864c758e2828b66c/report.json`
- `.work/reports/cpu-code-cache-portable/c672c70e5ddd42bc9826d0e2b3e96360/report.json`
- `.work/reports/cpu-code-cache-sanitize/3bcb27517d1f45429b6107d3411efc1c/report.json`
- `.work/reports/cpu-code-cache-msvc-desktop/de857d71d032473c801e34d2e1cf99d4/report.json`

The prepared ROMs retain the existing [fixture corrections](cartridge-fixtures.md)
and recorded hashes. Original-image acceptance remains open in #5 and #39.
The existing regression cases continue to check changed instruction-cache bytes,
stale backing memory, tag replacement, partial budgets, interrupts, device clocks,
and reset. Three new cases check lookup keys, failed compilation reuse, bounded
generations, retained owners after eviction, and copy/move ownership.

## Super Mario 64

An instrumented 1,200-field title experiment requested compilation 471,489 times.
Reusing identical sequences satisfied 469,346 requests with existing code, leaving
2,143 actual compilations. Measured compiler time fell from 2.61 to 0.11 seconds.
Total replay time fell from 36.19 to 33.03 seconds in that experiment. Every field
and the complete PCM stream matched.

The production implementation was then measured between two unmodified baseline
captures on the Windows 11 Intel Core i7-13700H host. Each replay used performance
core affinity and disabled process power throttling, with no concurrent build or
validation job. Every one of the 1,200 field records and every audio byte matched
both baselines.

| Title capture, in execution order | Wall time | Fields per host second |
| --- | ---: | ---: |
| Baseline A | 55.652 s | 21.6 |
| Production code reuse | 41.876 s | 28.7 |
| Baseline B | 42.383 s | 28.3 |

The baseline variation prevents a consistent overall speed claim from this batch.
Each capture executed 1,007,904,148 instructions and 1,912,382,539 CPU cycles,
producing 657,643 stereo sample pairs. The complete PCM SHA-256 is
`f12cc9d812900da0147b254b6d3d7fbc40c7d67e7708ba060a73ad991b580c3c`.
These results remain below full-speed playback; #48 and #47 remain open.

Two production 3,000-field controller replays also matched the unmodified
baseline and the retained preceding capture at every field. Complete PCM and
EEPROM bytes matched. The title head and the introductory outdoor scene were
inspected.

| Controller capture, in execution order | Wall time | Fields per host second |
| --- | ---: | ---: |
| Production A | 63.438 s | 47.3 |
| Baseline | 75.430 s | 39.8 |
| Production B | 64.562 s | 46.5 |

These captures used the same host settings with no concurrent build or validation
job. A separate instruction prototype ran between Production A and the baseline;
it preserved output but did not improve the production replay's time. It remains
outside the implementation.

Each controller replay executed 3,189,257,346 instructions and 4,733,062,618 CPU
cycles, producing 1,620,629 stereo sample pairs. The complete PCM SHA-256 is
`9ecae818c0daa6a64aab847c58536909a887dd5e06f8f47af85bd8f3625fd6ad`;
EEPROM SHA-256 is
`ed22a78d4e77ded30cd61898a7aa03a355a1db16d15b5a19d24a4538aac9020a`.
The improved controller timings still fall short of 60 Hz VI output. Sustained
3D movement and normal audible playback remain separate acceptance work.

This record, its index link, and the ownership guide were added after validation.
Implementation and regression-test bytes were unchanged. The preceding
[profile results](sm64-profile-results.md) describe a different core revision;
their source-bound profile package cannot be reused for this implementation.

## First eligible reuse (2026-10-01)

A replacement line plan now checks for an existing program on its first eligible
use. It reuses code only when every instruction word and the length match.
The lookup neither allocates storage nor compiles or evicts programs on a miss;
new sequences retain the five-use compilation threshold. Hardware instruction
bytes, tags, access guards, pipeline state, and guest budgets still control entry.

Two new regressions check a nonallocating miss in a full lookup generation and
immediate execution after tag or unrelated last-word replacement. The first-use
check fails on the preceding compiled core: it retires zero compiled instructions
where seven were expected. It passes with the change. All 47 focused native CPU
checks pass.

The same 536-file snapshot passes Clang 21.1.5 Release, Clang with both native
backends disabled, Clang ASan/UBSan, and MSVC 19.43.34809 desktop Release. Each
configuration passes all 1,567 hardware and host regressions, all 4,637 prepared
default and 6,273 prepared extended cartridge cases, stepped/batched comparisons,
and default cold/warm boots. Clang configurations pass all nine CTest groups;
MSVC passes all 15, including desktop audio lifecycle checks. All 67 tooling
checks and clang-format 22.1.0 pass. Source and input integrity checks pass,
and sanitizer logs contain no diagnostics. The source fingerprint is
`7bed151aceb9712e02cfd612c1eb2bb6b365602d43bb0ae7918ebc580a1f9787`.

Retained reports:

- `.work/reports/cpu-hot-reuse-native/d4a2d6488db74f4391e85c8382f9b768/report.json`
- `.work/reports/cpu-hot-reuse-portable/78cdb4d4ca014826b38fe17aa7dd6f2b/report.json`
- `.work/reports/cpu-hot-reuse-sanitize/8be45a6122a342d2bcc274f1c84ee1fe/report.json`
- `.work/reports/cpu-hot-reuse-msvc/6705b797afb24c119cb2b4ffcfb5c3fa/report.json`

### Gameplay comparison

Four 3,000-field controller captures ran in the following order, with matching
compiler settings, performance-core affinity, and no concurrent local build or
validation. Every field record, complete PCM stream, EEPROM byte, guest clock,
and guest instruction count matches. RSP compiled instruction totals also match.
The CPU compiled total rises from 139,503,792 to 165,950,608.

| Capture | Wall time | Fields per host second |
| --- | ---: | ---: |
| Preceding core A | 71.136 s | 42.2 |
| First eligible reuse A | 65.731 s | 45.6 |
| First eligible reuse B | 65.962 s | 45.5 |
| Preceding core B | 77.560 s | 38.7 |

Both candidate captures are faster than both baselines in this batch. A separate
7,200-field courtyard comparison is effectively unchanged: 197.625 seconds with
first-use reuse and 197.876 seconds on the preceding core, about 36.4 fields per
host second each. Both preserve every field, complete PCM, EEPROM, guest clock,
and instruction count. The CPU compiled total rises from 299,295,536 to
377,939,254; the RSP total remains 2,055,901,330. The final inspected frame retains
Mario, the bridge, water, depth, textures, and HUD.

The short-sequence gain does not establish a sustained 3D speed improvement.
Full-speed gameplay and normal audible playback remain open in #48 and #47.
Initial private captures used different compiler settings and are excluded from
these timing comparisons. This result section and its index entry were finalized
after the complete matrix; implementation, regression tests, and ownership-guide
bytes were unchanged.
