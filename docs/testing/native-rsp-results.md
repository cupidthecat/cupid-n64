# Native RSP and audio playback validation (2026-09-27)

The RSP can compile frequently reused local instruction blocks on x86-64 hosts.
The existing issue plan still controls pairing, stalls, DMA boundaries, and
the maximum execution interval. Generated code receives the current machine's
register and memory pointers on each call. The
[native block guide](../hardware/rsp-native.md) describes cache ownership,
invalidation, memory access, and the portable fallback.

Desktop playback now observes requests for unavailable samples through SDL's
stream callback. Emptying the input queue after a successful device read no
longer pauses playback or starts another prefill interval. The
[audio guide](../frontend/audio.md) describes the counter's meaning and
the unchanged initial prefill and explicit pause/reset behavior.

## Local checks

The combined implementation passed these Windows configurations:

| Check | MSVC 19.43 Release desktop | Clang 21.1.5 ASan/UBSan desktop |
| --- | ---: | ---: |
| Hardware and host regressions | 1,482/1,482 | 1,482/1,482 |
| Prepared default cartridge | 4,637/4,637 | 4,637/4,637 |
| Cold and warm boots | 4,637 each | 4,637 each |
| Prepared extended cartridge | 6,273/6,273 | 6,273/6,273 |
| CTest groups | 15/15 | 15/15 |

Both runs passed 67 tooling tests and clang-format 22.1.0. Source and input
integrity checks passed. No sanitizer diagnostics were reported. Stepped and
batched cartridge runs agreed on their execution observations. The tested
497-file snapshot has content fingerprint
`8cb0f4b290eaaea1e5de597401b0fd570b970b20e2ea32de5b87d9e3d0fe0b15`.

The local reports are retained at:

- `.work/build-native-msvc/validation/8de1565569cf4f1b9e4d2d5186388805/report.json`
- `.work/build-native-sanitize/validation/d443db26aabd45b4ac8cf964724cb0ed/report.json`

Before the desktop audio correction, the same core and RSP implementation also
passed ordinary Clang Release validation, a build with native RSP execution
disabled, and a Clang profile-collection desktop build. The native-disabled
build ran the local regressions; the other two configurations also ran both
prepared cartridge suites. CI includes a separate portable-build job.

Sixteen added RSP regressions cover encoded scalar operations, vector function
and element combinations, aliases, accumulator and flag state, wrapped DMEM
accesses, DMA, instruction replacement, speculative stores, reset, backend
selection, and code-cache ownership. Five deterministic desktop audio tests
control device consumption independently of frontend polling; four fail with
the preceding playback code. The real SDL dummy-device tests also pass.

The cartridges retain the existing [fixture corrections](cartridge-fixtures.md).
The default image SHA-256 is
`2a3171a342edeeef9c86acf92ee4dcaf689211d884a624b0147aaebbc43588de`;
the extended image is
`083cd13005b4706e1128946d49059f3222f93df23f7084ce038dd271e48ba824`.
Original-image acceptance remains tracked in issues #5 and #39.

## Super Mario 64

A 7,200-field controller replay reaches the title and file menu, starts a game,
jumps in the courtyard, and moves Mario to the castle fence. The native RSP
build matches all 29 retained video, audio, execution, and EEPROM outputs from
the preceding core. Both complete 7,156,154,368 CPU steps and 11,314,557,830 CPU
cycles, producing 3,834,472 stereo sample pairs without a CPU freeze or PIF
failure. The title, courtyard, and fence captures were inspected.

The native-execution counter records 2,329,564,912 RSP instruction executions
during that replay. This includes speculative work and replay after rollback;
it is not a count of retired guest instructions.

An isolated ordinary Clang Release run took 161.057 seconds on a Windows 11
Core i7-13700H, or about 44.7 VI fields per host second. The process used the
performance cores with process power throttling disabled. Local compilation
and validation had finished before the measurement. Repeating the same input
with serial RDP rendering took 178.427 seconds and produced identical output.
These are individual bounded measurements, not a sustained performance claim.
The normal parallel renderer remains enabled.

Inputs, executable hashes, captures, and comparisons are retained under
`.work/rdp-audit/`. Full-speed gameplay, normal audible playback, level/star
progression, and save/reload acceptance remain in issues #48 and #47. Later
profile-build and desktop measurements are recorded in PR #76.

This results record and its index links were added after validation.
Implementation and regression-test bytes were unchanged.
