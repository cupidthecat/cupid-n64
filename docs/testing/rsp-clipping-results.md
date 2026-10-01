# Compiled RSP clipping results

Date: 2026-10-01. Preceding revision: `26987c371be1ad588edca7ce5f760f9e93c6b736`.

The RSP compiler emits SSE2 instructions for VCH, VCL, and VCR, bringing its
emitted vector set to 35 operations. Clipping receives the current machine's
carry, comparison, and extension storage. It retains unsigned VCL comparisons,
wrapped VCH negation, VCR one's complement, and each operation's flag lifetime.
Cached accumulator slices flush before clipping uses their SIMD registers;
later operations reload the current slices. Upper accumulator slices remain
unchanged. See [native RSP blocks](../hardware/rsp-native.md).

## Regression checks

The independent scalar oracle checks the three operations with all 16 element
selections, input and destination aliases, every vector register, and every
accumulator slice. Repeated execution checks flag consumption after the first
operation. Additional cases exercise all 32 combinations of the five flags in
each lane and mixed multiply, accumulate, clipping, comparison, and VSAR chains.
The shared-code context check also exercises clipping on independent machines.

All 20 focused native-vector checks pass. The complete four-configuration
matrix validates the same 547-file source snapshot with SHA-256
`7663e0b3562589d18d6bb8ba7ce3a409940d4c9d07c1fbbac68d1afe599c51aa`.

| Check | Clang Release | Clang portable | MSVC desktop Release | Clang ASan/UBSan |
| --- | ---: | ---: | ---: | ---: |
| Hardware and host regressions | 1,576/1,576 | 1,576/1,576 | 1,576/1,576 | 1,576/1,576 |
| Prepared default cartridge | 4,637/4,637 | 4,637/4,637 | 4,637/4,637 | 4,637/4,637 |
| Default cold and warm boots | 4,637 each | 4,637 each | 4,637 each | 4,637 each |
| Prepared extended cartridge | 6,273/6,273 | 6,273/6,273 | 6,273/6,273 | 6,273/6,273 |
| CTest groups | 9/9 | 9/9 | 15/15 | 9/9 |

Clang Release and portable builds use Clang 21.1.5; portable execution disables
both native backends. MSVC uses version 19.43.34809. The sanitizer build uses
Clang 22.1.8 RelWithDebInfo and reports no diagnostics. Every configuration
passes all 67 tooling checks and clang-format 22.1.0, with verified source,
prepared test-source, and input integrity. The cartridges retain their
[documented fixture corrections](cartridge-fixtures.md); original-image
acceptance remains open in #5 and #39.

Retained local report IDs:

- Native: `d8c6d3774579485ca434a10f78e5ffa8`.
- Portable: `b8cbddb2e9dd41da9665c3214323ef62`.
- MSVC desktop: `4f6dacbfe26846ac96ac7ec30b043fd2`.
- Sanitizers: `c85f8a7f672745cf9ad06a9cc6e9d29b`.

## Isolated execution measurements

Each of eight samples executes one million compiled blocks on a Windows
Intel Core i7-13700H host. Both builds use Clang 21.1.5 Release with matching
optimization, P-core affinity, and power-throttling settings. The four groups
alternate order between samples. The runs occur in control, changed, changed,
control order without concurrent local builds or validation suites. Every
observed register, accumulator, and flag hash matches across the four runs.

Median nanoseconds per compiled block:

| Block | Control A | Changed A | Changed B | Control B |
| --- | ---: | ---: | ---: | ---: |
| VCL | 12.94 | 8.99 | 8.97 | 12.86 |
| VCH | 6.01 | 7.05 | 6.96 | 6.09 |
| VCR | 5.41 | 5.76 | 5.60 | 5.39 |
| Sixteen mixed vector operations | 46.65 | 33.33 | 33.20 | 46.47 |

The mixed chain includes accumulating arithmetic, all three clipping operations,
comparison, merge, and accumulator reads. Its elapsed time falls by about 29%.
Standalone VCH and VCR do not improve in these measurements. Repeated synthetic
blocks do not establish a gameplay rate.

## Super Mario 64

The complete 7,200-field courtyard replay matches the preceding core's video
and PCM streams, final image, guest instructions, emulated clocks, and compiled
CPU/RSP instruction totals. It produces 3,834,508 stereo sample pairs,
7,454,095,587 guest instructions, and 11,314,563,915 emulated CPU cycles.

| Replay | Host time | VI fields per host second |
| --- | ---: | ---: |
| Preceding core | 227.949 s | 31.59 |
| Compiled clipping | 214.199 s | 33.61 |

Both runs use the same compiler settings, controller script, private cartridge
and firmware, 8,192-instruction quantum, and 10,000-cycle budget. They run at
separate times without concurrent local builds or test suites. Other host work
and scheduling variation limit this single pair; it does not establish sustained
full-speed gameplay. The replay records generated PCM without verifying live
audio-device playback. Acceptance remains open in #48 and #47.

Complete output SHA-256 values:

- Field and execution CSV: `869effa519c5bf096e88eda53ffc219709a70ff08f75cd3058f39757a4a47ed8`.
- PCM: `e1157308b74948895166f880d264d93bf57f2a97fc8d04c051de8e3505b6d2d8`.
- Final PPM: `0d0d036d9030ede55904fe15b0076fd35e58533f4506141dd8bae98a3fd0bf6d`.
