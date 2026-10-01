# Native CPU control-flow validation (2026-10-01)

Compiled integer and cache-hit blocks can now end with BEQ, BNE, BLEZ, BGTZ,
BLTZ, BGEZ, J, JAL, JR, or JALR. Conditions use the full 64-bit operands.
Targets and return addresses use the current PC; JALR captures its source
before writing an aliased destination. The delay slot retains the existing
execution path. Likely branches and conditional links remain outside native
blocks. The [CPU guide](../hardware/cpu-native.md) describes admission and
interrupt retirement.

Ten new regressions cover predicates, block boundaries, repeated links,
register aliases, delay-slot exceptions, cache-hit loads and stores, and
RSP interrupt retirement. All 45 focused native CPU checks pass.

## Complete validation

All four configurations passed the same 532-file source snapshot, with SHA-256
`018cb871fc395418ed06af9d7f911212fc1285b1e17b518f02b75a1e32e512e7`.
Every report verified unchanged source and inputs.

| Check | Clang Release | Clang portable | Clang ASan/UBSan | MSVC desktop Release |
| --- | ---: | ---: | ---: | ---: |
| Hardware and host regressions | 1,563/1,563 | 1,563/1,563 | 1,563/1,563 | 1,563/1,563 |
| Prepared default cartridge | 4,637/4,637 | 4,637/4,637 | 4,637/4,637 | 4,637/4,637 |
| Default cold and warm boots | 4,637 each | 4,637 each | 4,637 each | 4,637 each |
| Prepared extended cartridge | 6,273/6,273 | 6,273/6,273 | 6,273/6,273 | 6,273/6,273 |
| CTest groups | 9/9 | 9/9 | 9/9 | 15/15 |

Each configuration passed 67 tooling regressions, clang-format 22.1.0, and
stepped/batched cartridge comparisons. Clang 21.1.5 used Release optimization,
ThinLTO, and strict floating-point flags. MSVC used version 19.43.34809.
The portable build disabled both native backends. Sanitizer logs contain no
diagnostics. The desktop groups include audio lifecycle checks.

Reports are retained under `.work/reports/`:

- `cpu-control-native/91899294edf94102924952ef66c37d0a/report.json`
- `cpu-control-portable/5239095083b5431f9a2a28a328461966/report.json`
- `cpu-control-sanitize/c4830505f88448a29b38ddc7198d0791/report.json`
- `cpu-control-msvc/9e5ae43816044b8987f869ebe4bfd7bc/report.json`

Prepared cartridges retain the [documented fixture corrections](cartridge-fixtures.md).
Original-image acceptance remains open in #5 and #39. This results record and
its index link were added after local validation; implementation and test bytes
are unchanged.

## Super Mario 64

The supplied USA cartridge completed both connected-gamepad replays with fresh
EEPROM files. Every video field, complete PCM stream, EEPROM byte, CPU cycle,
and guest instruction total matches the preceding production build. The RSP
native totals also match; CPU native totals increase as expected. The retained
outdoor frame was inspected and preserves geometry, water, textures, and the HUD.

| Replay | Fields | Host time | VI fields per host second | Compiled CPU instructions |
| --- | ---: | ---: | ---: | ---: |
| Controller | 3,000 | 69.075 s | 43.4 | 139,503,792 |
| Courtyard movement | 7,200 | 199.211 s | 36.1 | 299,295,536 |

The preceding captures compiled 88,378,561 and 202,136,255 CPU instructions,
respectively. These isolated captures ran on the Windows Intel Core i7-13700H
host with performance-core affinity and process priority, without concurrent
local builds or test suites. Separate run times and host variation do not
establish a whole-game speed improvement. Matching retained output is a
regression check; it does not independently prove hardware accuracy or normal
audible playback. Full-speed gameplay and audible acceptance remain open in
#48 and #47.

Complete PCM SHA-256 values:

- Controller: `9ecae818c0daa6a64aab847c58536909a887dd5e06f8f47af85bd8f3625fd6ad`
- Courtyard: `188303a792349e8c7b4ee7e5e0f144af3a71df6528efe6be7fc651ea38324360`

Both EEPROM files have SHA-256
`ed22a78d4e77ded30cd61898a7aa03a355a1db16d15b5a19d24a4538aac9020a`.
