# Cached CPU and rendering validation (2026-09-29)

The core compiles bounded integer and cache-hit load sequences without changing
VR4300 instruction clocks. Cached slices preserve branch-likely annulment,
integer-load interlocks, and SP DMA row boundaries. Local RSP blocks can end at
a branch issue group while leaving the delay slot and branch bubble to the
ordinary pipeline. RDP combiner plans retain live texture and keying inputs, and
VI scanout preserves the programmed row count when its top starts above the
display. The [CPU guide](../hardware/cpu-native.md),
[RSP guide](../hardware/rsp-native.md), and
[scanout guide](../hardware/video-scanout.md) describe the execution boundaries.

## Local validation

All four configurations passed the complete local validation entry point:

| Check | Windows Clang 21.1.5 Release | Windows clang-cl 21.1.5 desktop | Windows Clang 21.1.5 ASan/UBSan | Linux Clang 18.1.3 portable |
| --- | ---: | ---: | ---: | ---: |
| Hardware and host regressions | 1,517/1,517 | 1,517/1,517 | 1,517/1,517 | 1,517/1,517 |
| Prepared default cartridge | 4,637/4,637 | 4,637/4,637 | 4,637/4,637 | 4,637/4,637 |
| Prepared extended cartridge | 6,273/6,273 | 6,273/6,273 | 6,273/6,273 | 6,273/6,273 |
| CTest groups | 9/9 | 15/15 | 9/9 | 9/9 |

Each run passed 67 tooling tests and clang-format 22.1.0. Both cartridge suites
ran through ordinary stepping and batched execution; the default cartridge also
passed cold and warm boots. The portable configuration disabled both CPU and RSP
native code generation. No AddressSanitizer or UndefinedBehaviorSanitizer
diagnostics were found. Each report verified unchanged source and inputs.

The Windows runs checked a 510-file source snapshot with content fingerprint
`0086560b24229cd5eddec0f224567118571601b3593ec7b03960b82602265a43`.
The Linux report records
`4266a2919a2d64d26ce2deb872dc39e10575a15ca9b34002ba79841d18e3b284`;
the source identity includes platform executable-file permissions.

Reports are retained at:

- `.work/reports/accuracy-native-release/07310081cad64464b0b52dd9d199ea71/report.json`
- `.work/reports/accuracy-desktop-release/0227fba135894921babecd382584ee4a/report.json`
- `.work/reports/accuracy-sanitize/d9441736af4d4d07ab680d73430684bd/report.json`
- `.work/reports/accuracy-portable-linux/b9c9996893774ee2af52c160b3399beb/report.json`

The prepared cartridges retain the existing
[fixture corrections](cartridge-fixtures.md). The default ROM SHA-256 is
`84b90983b256f2017ee6df73e4c0d9c62bfe2b607fc8ba98cb3228c48424c289`;
the extended ROM is
`149b6b0db7b5c4c08d19597fb2b0ea4ce9e71a11147def2d6830cf01e7ef3409`.
Original-image acceptance remains tracked in #5 and #39.

## Super Mario 64

The supplied Super Mario 64 (USA) cartridge has SHA-256
`17ce077343c6133f8c9f2d6d6d9a4ab62c8cd2aa57c40aea1f490b4c8bb21d91`.
The captures use NTSC timing, 8 MiB RDRAM, EEPROM 4 Kbit, and a gamepad on port 1.

A 1,200-field title capture and a 3,000-field controller replay match the retained
comparison captures at every field: framebuffer hash, audio hash, sample
timestamps, PC, instructions, and CPU cycles. Complete PCM streams and EEPROM
bytes also match. The title head and castle scene were inspected.

| Capture | CPU instructions | CPU cycles | Stereo sample pairs | Wall time |
| --- | ---: | ---: | ---: | ---: |
| Title, 1,200 fields | 1,007,904,148 | 1,912,382,539 | 657,643 | 61.497 s |
| Controller replay, 3,000 fields | 3,189,257,346 | 4,733,062,618 | 1,620,629 | 112.154 s |

These unprofiled Windows 11 runs used an Intel Core i7-13700H with performance
core affinity and process power throttling disabled. They averaged about 19.5
and 26.7 VI fields per host second. They do not establish a performance
improvement or full-speed playback. Sustained gameplay, normal audible playback,
level progression, and save/reload acceptance remain open in #48 and #47.

This results record and its index link were added after validation.
Implementation and regression-test bytes were unchanged.
