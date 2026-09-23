# Recorded validation results

## RDP halfword transfers and RSP vector dispatch (2026-09-23)

The RDP transfers each 16-bit color or depth value together with its hidden
pair, preserving row tracking and ordinary accesses under remapping,
calibration, and missing memory. The [RDRAM guide](../hardware/rdram-interface.md)
describes the boundaries covered by four new regressions. RSP vector dispatch
keeps scalar fallback setup outside the supported packed path; see
[vector execution](../hardware/rsp-pipeline.md#decoded-packets-and-vector-execution).

The same 437-file snapshot passed these Windows configurations:

| Check | Clang Release | Clang ASan/UBSan | MSVC desktop Release |
| --- | ---: | ---: | ---: |
| Hardware and host regressions | 1,417/1,417 | 1,417/1,417 | 1,417/1,417 |
| Prepared default cartridge | 4,637/4,637 | 4,637/4,637 | 4,637/4,637 |
| Cold and warm boots | 4,637 each | 4,637 each | 4,637 each |
| Prepared extended cartridge | 6,273/6,273 | 6,273/6,273 | 6,273/6,273 |
| CTest groups | 9/9 | 9/9 | 14/14 |

All runs passed the 41 validation-tool tests, clang-format 22.1.0, and source
and input integrity checks. The logs contain no sanitizer diagnostic. Default
and extended stepped/batched execution matched all 3,673 and 3,876 observations,
respectively, with identical cartridge instruction and cycle totals across
compilers. A separate Clang desktop build passed its five desktop test groups.
These cartridges retain the [documented fixture corrections](cartridge-fixtures.md);
original-image acceptance remains tracked in #5 and #39.

Two Super Mario 64 replay pairs used ordinary Clang Release builds on the
i7-13700H with high process QoS and performance-core affinity. The second pair
reversed the executable order:

| Replay | Before | After |
| --- | ---: | ---: |
| Title, 1,200 fields, first pair | 29.354 s | 28.938 s |
| Title, 1,200 fields, reverse pair | 28.863 s | 29.210 s |
| Outdoor, 6,000 fields, first pair | 124.904 s | 122.782 s |
| Outdoor, 6,000 fields, reverse pair | 123.451 s | 123.041 s |

Outdoor runtime fell by 0.3% to 1.7%, averaging about 1.0% across the pairs.
The changed build ran at 81.8% to 82.0% of real time. Title timing varied in
both directions and does not establish an improvement. Each title replay
matched all 1,200 field observations and 12 retained video, audio, and save
files. Each outdoor replay matched all 6,000 observations and 52 retained
files. The executables and replay inputs retained their hashes. All replays
recorded zero audio-timeline discontinuities, CPU freezes, and PIF failures.
The retained outdoor capture was also inspected.

The current Clang desktop completed a separate 60.048-second title-screen run
with 2,051 VI fields and 2,040 presentations, averaging 34.2 VI fields per
second. It saved its capture and EEPROM and exited successfully. The capture
was inspected, and executable, dependency, firmware, and cartridge hashes
remained unchanged. Playback recorded 773 underruns, zero host audio drops,
and 364 dropped audio frames at the device queue. Full-speed gameplay and
normal audible playback remain open in #48 and #47.

Reports and replay comparisons are retained under
`.work/validation/rdram-pairs-20260923/`. This result record was added after
validation; executable source and tests were unchanged.

## Local RSP blocks and integer conversions (2026-09-23)

Local RSP execution reuses decoded instructions and pipeline timing for blocks
of up to 16 instructions. Checkpoints preserve latched instructions and memory
stores while CPU accesses and callbacks bring observable RSP state back to the
shared clock. Reuse requires matching instruction-memory revision, dependency
history, and single-issue state. The [RSP pipeline guide](../hardware/rsp-pipeline.md)
describes the execution and rollback bounds.

Supported floating-point conversions to word or long now use the encoded
significand and discarded bits. Guest rounding, flags, register aliases, and
five-cycle latency are preserved. Inputs that can raise a guest exception keep
the scoped conversion path. The [floating-point guide](../hardware/floating-point.md)
describes the ranges and host-environment checks.

The same 435-file snapshot passed these Windows configurations:

| Check | Clang Release | Clang ASan/UBSan | MSVC desktop Release |
| --- | ---: | ---: | ---: |
| Hardware and host regressions | 1,413/1,413 | 1,413/1,413 | 1,413/1,413 |
| Prepared default cartridge | 4,637/4,637 | 4,637/4,637 | 4,637/4,637 |
| Cold and warm boots | 4,637 each | 4,637 each | 4,637 each |
| Prepared extended cartridge | 6,273/6,273 | 6,273/6,273 | 6,273/6,273 |
| CTest groups | 9/9 | 9/9 | 14/14 |

Each run passed all 41 validation-tool tests, clang-format 22.1.0, and source
and input integrity checks. Sanitizer logs contain no diagnostic. The default
stepped/batched comparison matched all 3,673 observations; the extended
comparison matched all 3,876. Instruction and cycle totals agree across the
three configurations. A separate Clang desktop build passed its five desktop
test groups. The cartridges retain the [documented fixture corrections](cartridge-fixtures.md);
original-image failures remain tracked in #5 and #39.

Two fresh Super Mario 64 replays used ordinary Clang Release builds on the
i7-13700H, high process QoS, and performance-core affinity:

| Replay | First run | Second run |
| --- | ---: | ---: |
| Title, 1,200 fields | 29.282 s | 28.836 s |
| Outdoor gameplay, 6,000 fields | 125.788 s | 124.799 s |

Each title replay matched all 1,200 field observations and 12 retained video,
audio, and save files. Each outdoor replay matched all 6,000 observations and
52 retained files. Executables and inputs retained their hashes. Outdoor
gameplay covered 100.631 seconds of emulated time, or 80.0% and 80.6% of real
time in these runs. Both recorded zero audio-timeline discontinuities, without
a CPU freeze or PIF failure.

The current Clang desktop completed a separate 60.050-second title-screen run
with 2,020 VI fields and 1,986 presentations. It saved its capture and EEPROM
and exited successfully. The run averaged 33.6 VI fields per second and recorded
766 audio underruns, with zero host or device audio drops. Full-speed gameplay
and normal audible playback remain open in #48 and #47.

Reports and replay comparisons are retained under
`.work/validation/cached-conversions-20260923/`. This result record was added
after validation; executable source and tests were unchanged.

## Shared RSP scheduling and instruction storage (2026-09-23)

Cached CPU slices now keep shared RSP operations within their existing device
and timer bounds. Each interruptible CPU instruction retires before its full
RSP cycle quota runs. RDP and RDRAM clocks advance before each RSP tick, DMA
payloads become visible before issue, and MI is sampled after the complete
instruction. Masked RCP work catches up through the ordinary scheduler before
the CPU can observe shared state or re-enable interrupts.

SP memory tracks IMEM writes so decoded packets can be reused across calls.
Public storage aliases permanently select exact word checks, including const
aliases and aliases retained across reset. DMA invalidates future fetches once
per IMEM row while preserving an already latched packet. The
[CPU timing guide](../hardware/cpu-timing.md) and
[instruction-storage guide](../hardware/rsp-instruction-storage.md) describe the
execution bounds and invalidation rules.

Strict Windows Clang and MSVC desktop validation and Linux Clang ASan/UBSan
passed the same implementation and tests:

| Check | Windows Clang | Windows MSVC | Linux ASan/UBSan |
| --- | ---: | ---: | ---: |
| Core regressions | 1,397/1,397 | 1,397/1,397 | 1,397/1,397 |
| Default cartridge | 4,637/4,637 | 4,637/4,637 | 4,637/4,637 |
| Cold and warm boots | 4,637 each | 4,637 each | 4,637 each |
| Extended cartridge | 6,273/6,273 | 6,273/6,273 | 6,273/6,273 |
| CTest groups | 14/14 | 14/14 | 9/9 |

All three runs passed 41 validation-tool tests, formatting with clang-format
22.1.0, and source/input integrity checks. Their manifests contain 429 existing
files and two recorded deletions. The sanitizer logs contain no diagnostic.
Each default stepped/batched comparison matched all 3,673 observations; each
extended comparison matched all 3,876, including register state and timestamps.
The cartridge inputs retain the documented fixture corrections.

The retained original extended image, SHA-256
`441bc0b4409034c0c4cffdb658005cae9033c53c0fef69763ffbd89dcf30b089`, still
reports eleven triangle-fixture disagreements and two timing failures. Its guest
output and totals match the previous published build: 358,173,663 instructions
and 793,786,086 CPU cycles. The comparison excludes the host elapsed-time field
in the runner's final summary. The VI-disabled cache average remains 43.03
against 42.5 +/- 0.5, and the original CPU/RDP sampler remains 133,300 against
133,333 +/- 20. This original layout still fails; #5 and #39 remain open.

The 31 added core regressions cover shared SP/DP reads, semaphore side effects,
transient and persistent interrupts, branch-delay exception state, DMA row
timestamps, code replacement during stalls, storage aliases, and callback/NMI
boundaries. Two tool regressions cover formatter path resolution and failure
propagation. Source-relative filenames remove the repeated checkout prefix from
the formatter command, fixing the Windows command-length failure in #63.

One paired run on the i7-13700H used ordinary Clang Release builds, strict
floating-point settings, high process QoS, and performance-core affinity:

| Replay | Previous build | Combined build | Wall-time reduction |
| --- | ---: | ---: | ---: |
| Stationary title, 2,000 fields | 72.860 s | 68.344 s | 6.20% |
| Gameplay, 6,000 fields | 136.563 s | 130.055 s | 4.77% |

All 8,000 field observations and 71 retained video, audio, and EEPROM files
matched. Executables and inputs retained their hashes. The combined gameplay
run covers about 100.63 seconds of emulated time, or 77.4% of real time at the
measured wall time. Each build was measured once per workload, so these pairs
do not establish sustained performance. Full-speed gameplay and normal audible
playback remain open in #48 and #47.

A separate desktop title-screen run completed 60.067 seconds with 1,739 VI
fields, 1,724 presentations, 645 audio underruns, and 104 device audio drops.
Host audio drops were zero. The application saved its capture and EEPROM and
exited successfully. Its approximately 29.0 VI fields per second and audio
underruns leave the desktop speed and playback requirements unmet.

Reports and replay comparisons are retained under
`.work/validation/rsp-cpu-integration-20260923/`. This result record and the
formatter-path explanation in the testing guide were updated after validation;
implementation and test bytes were unchanged.

## Cached arithmetic and RSP settlement (2026-09-23)

Cached CPU execution accepts binary floating-point arithmetic whose live
operands and control bits prove an exact, nontrapping latency. It charges the
same integer and floating-point issue waits as ordinary stepping, advances
instruction counters by retirements, and advances clocks by elapsed cycles.
Instructions that would reach a device or Count/Compare edge use ordinary
stepping. A multicycle instruction with a running RSP first proves that the
entire RSP span accesses only local state.

RSP settlement can also run local instructions together within a scheduled
interval. It preserves the final cycle's device-before-issue ordering. Shared
register operations, DMA visibility, branch waits, and already fetched packets
retain their synchronized behavior. The [CPU timing guide](../hardware/cpu-timing.md)
and [RCP scheduling guide](../hardware/rcp-scheduling.md) describe these bounds.

Strict Windows Clang 21.1.5 and MSVC 19.43 desktop builds and Linux Clang 18.1.3
ASan/UBSan passed the same 422-file source snapshot:

| Check | Windows Clang | Windows MSVC | Linux ASan/UBSan |
| --- | ---: | ---: | ---: |
| Core regressions | 1,366/1,366 | 1,366/1,366 | 1,366/1,366 |
| Default cartridge | 4,637/4,637 | 4,637/4,637 | 4,637/4,637 |
| Cold and warm boots | 4,637 each | 4,637 each | 4,637 each |
| Extended cartridge | 6,273/6,273 | 6,273/6,273 | 6,273/6,273 |
| CTest groups | 14/14 | 14/14 | 9/9 |

All three runs passed both stepped-versus-batched cartridge comparisons, all
39 validation-tool tests, formatting with clang-format 22.1.0, and source/input
integrity checks. The sanitizer logs contain no diagnostic. Extended results
were Base 4,649, Timing 1,604, Cycle 13, CP0 hazards five, and quirks two, with
zero failures. The cartridge inputs retain the documented fixture corrections.

The 23 added regressions cover exact arithmetic costs, dependency waits,
budgets ending within instructions, rounding and register aliases, host
floating-point state, timer and output callbacks, DMA boundaries, and IMEM
edits while an instruction packet is latched. The existing 23 framebuffer
regressions also passed, covering every color-image size and format code in
both cycle modes. This completes the storage behavior tracked in #49; the
broader rendering and hardware-capture limits remain separate.

On the i7-13700H, one final paired run took 71.537 seconds before these changes
and 70.800 afterward for 2,000 stationary title-screen fields. The 6,000-field
gameplay replay took 133.097 and 132.691 seconds. Every field observation and
all 71 retained video, audio, and EEPROM files matched. The ordinary Clang
Release runs kept strict floating-point settings, high process QoS, and
performance-core affinity. Their inputs and executables retained their hashes.

Those pairs observed 1.03% and 0.30% less wall time. Each candidate was measured
once per workload. Earlier intermediate snapshots ranged from a 1.25%
regression to a 2.35% reduction against the same baseline, while repeated
baseline times varied by about 1.4 to 1.5%. These measurements do not establish
a repeatable speedup.

A separate desktop title-screen run completed in 60.066 seconds with 1,658 VI
fields, 1,566 presentations, and 607 audio underruns. Host and device audio-drop
counters were zero. The application saved its capture and EEPROM and exited
successfully, but approximately 27.6 VI fields per second is below full speed.
The matched replay recorded zero audio-timeline discontinuities; it does not
establish normal audible playback. Issues #48 and #47 remain open.

Reports, replay comparisons, and the desktop capture are retained under
`.work/validation/continuation-20260923/`. The result record and two guide
clarifications were added after validation; implementation and test bytes were
unchanged.

## Scalar SSE binary arithmetic (2026-09-23)

ADD, SUB, MUL, and DIV now use scalar SSE on x64 while preserving guest
rounding, exception flags, result normalization, register mapping, and latency.
The calling thread's MXCSR is restored after arithmetic. Potential guest traps
retain a full environment scope, including traps whose younger instruction
fetch delivers a callback that changes the host environment. Other targets
retain the portable arithmetic path.

Strict Windows Clang 21.1.5 and MSVC desktop builds and Linux Clang ASan/UBSan
passed 1,343 core regressions, the 4,637-case default cartridge including cold
and warm boots, the 6,273-case extended cartridge, and both execution-mode
comparisons. Windows passed all 14 CTest groups; Linux passed all nine core
groups. Formatting, validation-tool tests, and source/input integrity checks
passed. The sanitizer run reported no diagnostic.

The added arithmetic test compares result bits and exception flags with the
portable path across all rounding modes, signed zeros, finite boundaries,
subnormals, and infinities. It checks host-state restoration with denormal
controls and existing exception flags set. The exception-fetch callback test
now covers invalid arithmetic input, overflow, and exact subnormal results.

On an i7-13700H, the 2,000-field stationary title-screen replay took 74.164
seconds before the change and 71.667 afterward. A 6,000-field gameplay replay
took 141.358 and 138.447 seconds. Each pair matches every field observation and
all retained video, audio, and EEPROM files: 19 files for the title screen and
52 for gameplay. These pairs show about 3.4% and 2.1% less elapsed time. They
used ordinary Clang Release builds, strict floating-point settings, high process
QoS, and performance-core affinity. Executable and input hashes were unchanged.

Evidence is retained under `.work/validation/fpu-binary-20260923/`. All 416
source files matched the validated snapshot when copied to the feature branch.
This result record was added afterward; executable source and tests were
unchanged. Full-speed gameplay and normal audible playback remain unresolved
in #48 and #47.

## Packed vector carry arithmetic (2026-09-22)

The VADD/VSUB packed arithmetic change passed strict Windows Clang and MSVC
Release validation with the desktop enabled, and Linux Clang ASan/UBSan
validation. All three runs passed 1,331 core regressions, the 4,637-case default
cartridge, both cold and warm boots, and the 6,273-case extended cartridge.
Windows passed all 14 CTest groups; Linux passed all nine core groups. Both
cartridge execution-mode comparisons matched, including registers and report
timestamps. Formatting, validation-tool tests, and source/input integrity checks
passed. The sanitizer run reported no diagnostic.

The added encoded tests cover all pairs of eight values at and near the signed
endpoints and zero, with carry clear and set. They observe the saturated result,
all accumulator slices, and control flags, including destinations that overwrite
either input.

The 6,000-field Mario replay matches all field observations and all 52 retained
video, audio, and EEPROM files. It took 173.606 seconds for 100.631 seconds of
emulated time, or 58.0% of real time. The preceding baseline run took 175.482
seconds; this single pair does not establish a whole-game speedup.

A separate encoded VADD/VSUB loop ran 600 million RSP clocks per measurement.
Six runs of each build, alternating their order, produced median times of
3.401 seconds for a freshly built baseline and 3.320 seconds for the packed
change. Final PC and stored lanes matched in every run. Both benchmarks used
high process QoS and performance-core affinity. The focused result does not
establish full-speed gameplay or uninterrupted audible playback.

Evidence is retained under `.work/validation/vector-carry-20260922/`. All 411
checked source files matched the Linux validation copy before this result
record was added. Executable source and tests were unchanged after validation.
The acceptance work in #48 and #47 remains open.

## Cached execution and reset deadline follow-up (2026-09-22)

The next 411-file source snapshot passed strict Windows Clang and MSVC Release
validation with the desktop enabled, and Linux Clang ASan/UBSan validation.
All three runs passed 1,330 hardware and host regressions, the 4,637-case default
cartridge, both cold and warm boots, and the 6,273-case extended cartridge.
Windows passed all 14 CTest groups; Linux passed all nine core groups. The
stepped and batched cartridge runs matched their register state and report
timestamps. Formatting and all 39 validation-tool tests passed. Source and
input checks passed, and the sanitizer log contains no diagnostic.

Coverage includes COP1 transfers and comparisons during cached execution,
host floating-point state restoration, local RSP execution windows, decoded
scalar operations, packed vector comparisons and clipping, shared texture
division, texture-tap selection, combiner dependencies, and cached VI rows.
The reset-release regression checks that a one-RCP-clock PIF deadline interrupts
ordinary CPU stepping before the third instruction retires. Worker tests cover
native-thread exit and explicit shutdown before thread-local teardown.

The retained 6,000-field Mario replay took 133.636 seconds for about 100.63
seconds of emulated time, or 75.3% of real time. It matches all field records
and retained video, audio, and EEPROM files from the preceding replay. A
separate profile-guided build took 135.969 seconds and produced the same
outputs. These are bounded replay measurements with high process QoS and
performance-core affinity, not a claim of sustained full-speed desktop play.
The ordinary Clang desktop completed a timed launch, capture, save, and exit;
the profile-guided desktop also completed a 30-second run without hanging.

Evidence is retained locally under
`.work/validation/continuation-20260922/`. The checked source matches the Linux
copy byte for byte. This result record was updated after validation; executable
source and tests were unchanged. Full-speed gameplay and uninterrupted audible
playback remain open acceptance work in #48 and #47.

## CPU, RSP, and raster execution optimizations (2026-09-22)

The optimized core passed the shared validator on Windows with Clang 21.1.5 and
MSVC, and on Linux with Clang 18 plus AddressSanitizer and
UndefinedBehaviorSanitizer. The Windows Release builds include the SDL3 frontend
and use interprocedural optimization. All three builds retained strict warnings.
The Linux run checked formatting with clang-format 22.1.0.

| Check | Windows Clang | Windows MSVC | Linux ASan/UBSan |
|---|---:|---:|---:|
| Hardware and host regressions | 1,243/1,243 | 1,243/1,243 | 1,243/1,243 |
| Prepared default cartridge | 4,637/4,637 | 4,637/4,637 | 4,637/4,637 |
| Default cold and warm boot | 4,637 each | 4,637 each | 4,637 each |
| Prepared extended cartridge | 6,273/6,273 | 6,273/6,273 | 6,273/6,273 |
| CTest groups | 14/14 | 14/14 | 9/9 |

A subsequent strict Linux GCC 13.3.0 build passed all nine CTest groups after
making the masked RSP decode-cache index conversion explicit. It also passed
1,243 core regressions, both default boot paths, and both complete cartridge
suites with the same instruction and cycle totals below.

Extended results were Base 4,649/4,649, Timing 1,604/1,604, Cycle 13/13,
CP0 hazards 5/5, and quirks 2/2. Default execution took 329,399,327 instructions
and 822,037,286 CPU cycles; extended execution took 363,753,813 instructions and
896,469,179 CPU cycles. These totals matched across all three configurations.
The full stepped-versus-batched cartridge comparisons also matched: 3,673
report observations for the default image and 3,876 for the extended image,
including registers, program counters, cycles, and instruction counts.

The runs checked the same 394 source files and matching cartridge and firmware
bytes. Their source and input fingerprints remained unchanged through each
validator, and the sanitizer run reported no diagnostic. A separate Windows
build with the packed RSP dispatch disabled passed all 1,243 hardware and host
regressions through the scalar fallback.

New coverage includes cached CPU memory widths and signedness, a load changing
its own base register, rejected alignment and external-doubleword accesses,
cached SP memory during local RSP execution, and IMEM edits during a latched
operand stall. Vector checks cover every element selection, all three operands
sharing a register, carry propagation through both accumulator boundaries, and
48-bit wrap. The blender-divider regression checks all 32,768 table inputs.

### Super Mario 64 replay

The final Windows builds replayed 6,000 fields with the same scripted input,
cartridge, firmware, and EEPROM configuration. The run reaches the outdoor
scene and covers 100.631 seconds of emulated CPU time. These measurements used
an Intel Core i7-13700H, high process QoS, and the performance-core affinity mask
`0xfff`. The runs were sequential, with profiling disabled and no concurrent
build or validation process.

| Compiler | Wall time | Emulated time / wall time |
|---|---:|---:|
| Windows Clang Release | 173.462 seconds | 58.0% |
| Windows MSVC Release | 220.829 seconds | 45.6% |

Both runs match all 6,000 field records and 52 retained video, audio, and save
files against the preceding checked replay. Each executes 6,201,555,337
instructions in 9,434,118,569 CPU cycles, produces 3,225,577 DAC samples and
4,830,263 resampled playback frames, and records zero audio-timeline
discontinuities. Neither run freezes or reports a PIF failure.

The fastest final result remains below real time. A matching audio timeline does
not establish uninterrupted audible playback on a slow host, and this bounded
replay does not complete the physical-input, save/reload gameplay, or 30-minute
acceptance checks. Issue #48 remains open.

The shared reports and comparisons are retained under
`.work/validation/fullspeed-review-20260922/`. These results establish regression
coverage for the tested execution paths. The independent conformance, sustained
gameplay, physical-controller, and release checks in issues #37, #38, #40, and
#48 remain separate requirements.

## Deferred RCP device clocks

The September 21, 2026 runs following this change keep device clocks behind the
CPU while it executes from cache. A device read or write, a scheduled peripheral
edge, a buffered store, or a running RSP catches those clocks up before anything
is observed. The RDP clock and the RDRAM row tracker still advance with a running
RSP. VI, PI, SI, AI, EEPROM, flash, and the RI refresh counter wait until their
next edge.

Two regressions in `tests/rcp/test_deferred_devices.cpp` compare a machine that
settles after every instruction with one that lets the devices lag, including
overlapping PI and SP DMA while VI lines run, and a cartridge-bus write after a
long cached countdown. A write-buffer entry is queued only after that catch-up.

Windows MSVC Release, Clang 21.1.5 Ninja Release, and Clang ASan/UBSan with leak
detection each built the same working tree. Hardware regressions are 1,154
passed, including the two new cases. Instruction and CPU-cycle counts match the
earlier fully passing extended record at `6465fd1`. Formatting used clang-format
21.1.5. The Clang Release run used `tools/ci/validate.py` with both prepared
cartridges, the PIF image, and the prepared test source. The sanitizer run used
the same script without cartridges. The MSVC CTest set also includes the
desktop adapter checks from that build. The sanitizer log has no AddressSanitizer,
UndefinedBehaviorSanitizer, or leak diagnostics.

| Check | MSVC Release | Clang Release | Clang ASan/UBSan |
| --- | --- | --- | --- |
| Hardware and host regressions | 1,154 passed | 1,154 passed | 1,154 passed |
| Validation-tool regressions | CTest only | passed in `validate.py` | passed in `validate.py` |
| Default cartridge | 4,637 passed | 4,637 passed | not run |
| Cold boot followed by warm reset | 4,637 passed on each boot | 4,637 passed on each boot | not run |
| Extended Base, including all twelve triangle cases | 4,649 passed | 4,649 passed | not run |
| Extended Timing | 1,604 passed | 1,604 passed | not run |
| Cycle, CP0-hazards, and Poorly-understood-quirk | 13, five, and two passed | 13, five, and two passed | not run |
| Source and input integrity | Verified on the Clang `validate.py` run | Verified | Verified |

Default execution uses 329,399,327 instructions and 822,037,286 CPU cycles.
Extended execution uses 363,753,813 instructions and 896,469,179 CPU cycles.
Those totals match the row-open record. Input hashes are unchanged:

| Input | Bytes | SHA-256 |
| --- | ---: | --- |
| Prepared default cartridge | 2,609,128 | `353bb2d2132b8ca6038ecb7dc5f2b71426fd269cf93995e745ebd0be28c1f3f3` |
| Prepared extended cartridge | 2,633,384 | `5c490faffc0329ede6ae4a877d5ac2a15a86e42a5af8bfe546e0ff52fb3ce170` |
| NTSC PIF firmware | 1,984 | `fa7b09795ef1e54461e59f6f2d902368133e3f1cd980e34383e6a780d74beffd` |

A 600-field Super Mario 64 (USA) capture on the MSVC Release build, EEPROM 4
Kbit, completed 676,601,989 steps and 972,094,613 CPU cycles in 36.6 seconds
and produced 309,814 audio samples. That is about 28 percent of the 93.75 MHz
CPU clock. Playable speed remains open under #48.

## RDRAM row opening and the in-flight refresh hold

The September 21, 2026 runs following `6465fd1dd94d6c88ae3da06ea4242376544f36d0`
cover two changes to uncached CPU reads. An uncached read whose 2 KiB row is not
open in its 1 MiB bank now takes four more cycles while RI opens it, and the VI
line-buffer fill keeps the framebuffer row open in its bank, so an uncached load
in the bank VI is displaying from measures 36 cycles against 32 elsewhere. A
refresh that begins while a single-word uncached read is in flight no longer
holds that read for the recovery interval; cache refills and writebacks keep the
hold. The [CPU timing guide](../hardware/cpu-timing.md#uncached-rdram-reads),
[RDRAM interface guide](../hardware/rdram-interface.md#open-rows-and-request-timing),
and [video timing guide](../hardware/video-timing.md#framebuffer-fetches-and-rdram-rows)
describe the model and its limits.

Eleven regressions were added: ten in `tests/cpu/test_rdram_rows.cpp` for the
row wait across all eight banks, other-bank and chip-register requests, the
cached refill path, refresh closing rows, VI fetches in the same and another
bank, the fetch interval, tick-size independence, and reset; and one in
`tests/cpu/test_rdram_refresh_overlap.cpp` for the uncached word that completes
ahead of a refresh. The refresh-stall regression's expectation moved from 112 to
116 cycles because the first read after a refresh opens its row again.

Windows MSVC Release, Linux Clang Release, and Clang ASan/UBSan with leak
detection each validated the same clone of that revision, 301 tracked files with
a clean status. Nothing changed after the runs except this record. The Linux git
status lists line-ending differences for the Windows checkout; the file bytes
are the ones both platforms compiled.

| Check | Result in each configuration |
| --- | --- |
| Hardware and host regressions | 1,090 passed |
| Validation-tool regressions | 36 passed |
| Default cartridge | 4,637 passed |
| Cold boot followed by warm reset | 4,637 passed on each boot |
| Extended Base, including all twelve triangle cases | 4,649 passed |
| Extended Timing | 1,604 passed |
| Cycle, CP0-hazards, and Poorly-understood-quirk | 13, five, and two passed |
| Source and input integrity | Verified |

Formatting, strict builds, storage checks, and capture-runner checks pass. All
seven CTest entries pass and each full validator returns exit 0. This is the
first record in which the prepared extended cartridge passes every enabled
case. The sanitizer run reports no AddressSanitizer, UndefinedBehaviorSanitizer,
or leak diagnostics. Default execution uses 329,399,327 instructions and
822,037,286 CPU cycles; extended execution uses 363,753,813 instructions and
896,469,179 CPU cycles in all three configurations.

The test revision remains `9a8b9f7d94ee2f6f57d7feed70c98c22cdc30e6c` with the
maintained fixture corrections. Input hashes are unchanged:

| Input | Bytes | SHA-256 |
| --- | ---: | --- |
| Prepared default cartridge | 2,609,128 | `353bb2d2132b8ca6038ecb7dc5f2b71426fd269cf93995e745ebd0be28c1f3f3` |
| Prepared extended cartridge | 2,633,384 | `5c490faffc0329ede6ae4a877d5ac2a15a86e42a5af8bfe546e0ff52fb3ce170` |
| NTSC PIF firmware | 1,984 | `fa7b09795ef1e54461e59f6f2d902368133e3f1cd980e34383e6a780d74beffd` |

### How the refresh hold was found

With only the row change, the retained original extended image reported the
VI-disabled uncached average at `a0100000` as 33.543 against 32.54 plus or
minus 1.0, while the prepared image passed the same case. A probe of the 3,001
loads in that test showed the row wait adding 0.14 cycles per load on average,
loads that began during a refresh adding 0.46, and the hold for a refresh that
began during the load adding 0.79. The hardware figure leaves 0.54 for all of
these together. Removing the hold from uncached reads puts the model at about
32.6 on that image and leaves the prepared image fully passing. Removing the
hold from cache refills as well would drop the prepared image's VI-enabled
same-bank cache average to 42.231 against 43.25 plus or minus 1.0, so the
refill paths keep it.

### Replay of the original retained image

The original extended image with SHA-256
`441bc0b4409034c0c4cffdb658005cae9033c53c0fef69763ffbd89dcf30b089` was replayed
on the Windows Clang Release build of the same source. It executes 358,173,663
instructions and 793,786,086 CPU cycles. The VI-enabled uncached median and the
VI-disabled uncached average both pass. Two timing cases remain: the VI-disabled
cache-load average at `0x80300000` (43.03 against 42.5 plus or minus 0.5), and
the original clock sampler (133,300 against 133,333 plus or minus 20), which the
maintained fixture corrects. The eleven triangle-fixture disagreements remain
as recorded earlier. Issue #5 stays open for the cache average on that layout.
Shared-bus arbitration between requesters is still not modeled under #4.

## Integer execution and instruction-cache overlap

The September 21, 2026 runs following
`cf283aaaea020dba8c6f7e315a135f9caab9bb5a` cover concurrent integer execution
and instruction-cache refill. A 69-cycle `DDIV` at the end of a cached line
previously took 117 cycles when the next line was cold. It now overlaps the
shorter refill and finishes in 69 cycles. Multiplication and shorter division
still wait when the refill lasts longer. The
[CPU timing guide](../hardware/cpu-timing.md#integer-execution-and-instruction-cache-overlap)
describes the processor-manual basis and the remaining floating-point and
buffered-store limits.

Five of the six new regressions fail on the preceding core; all six pass with
the correction. They cover all eight integer multiply/divide instructions,
three system-clock phases, warm and cold successor lines, delay-slot branch
targets, Count/Compare, and refresh that either fits within or outlasts division.
A separate seven-case focused run also checks a dependent cached load before
the divide, confirming that its issue interlock completes before the concurrent
waits. The additional probe is outside the maintained regression count.

Windows MSVC Release, Linux Clang Release, and Clang ASan/UBSan with leak
detection each validated the same frozen set of 299 files. All 169 tracked
cartridge-source files and the ROM/PIF inputs retained their hashes. Only this
record and clarifications to the CPU guide changed after the full runs.

| Check | Result in each configuration |
| --- | --- |
| Hardware and host regressions | 1,079 passed |
| Validation-tool regressions | 36 passed |
| Default cartridge | 4,637 passed |
| Cold boot followed by warm reset | 4,637 passed on each boot |
| Extended Base, including all twelve triangle cases | 4,649 passed |
| Extended Timing | One failed of 1,604 |
| Cycle, CP0-hazards, and Poorly-understood-quirk | 13, five, and two passed |
| Source and input integrity | Verified |

Formatting, strict builds, storage checks, and capture-runner checks pass.
All three configurations agree on the complete failure block and execution
counts. The sanitizer run reports no AddressSanitizer,
UndefinedBehaviorSanitizer, or leak diagnostics. Each full validator returns
exit 8 for the same VI-enabled uncached load: median 32 against 36 plus or
minus one. The test remains enabled.

The test revision remains `9a8b9f7d94ee2f6f57d7feed70c98c22cdc30e6c` with
the maintained fixture corrections. Input hashes are unchanged from the
edge-switch restoration runs:

| Input | Bytes | SHA-256 |
| --- | ---: | --- |
| Prepared default cartridge | 2,609,128 | `353bb2d2132b8ca6038ecb7dc5f2b71426fd269cf93995e745ebd0be28c1f3f3` |
| Prepared extended cartridge | 2,633,384 | `5c490faffc0329ede6ae4a877d5ac2a15a86e42a5af8bfe546e0ff52fb3ce170` |
| NTSC PIF firmware | 1,984 | `fa7b09795ef1e54461e59f6f2d902368133e3f1cd980e34383e6a780d74beffd` |

Default execution uses 329,396,986 instructions and 822,176,138 CPU cycles.
Extended execution uses 363,666,509 instructions and 896,399,132 CPU cycles.

### Replay of the original retained image

The original extended image with SHA-256
`441bc0b4409034c0c4cffdb658005cae9033c53c0fef69763ffbd89dcf30b089` was replayed
on both the preceding core and the corrected Clang build, without changing its
bytes. Both runs report the same eleven triangle-fixture disagreements and
three timing failures. The latter are the VI-disabled cache-load average at
`0x80300000` (43.03 against 42.5 plus or minus 0.5), the VI-enabled uncached
median (32 against 36 plus or minus one), and the original clock sampler
(133,300 against 133,333 plus or minus 20).

The ten cache-load cases pass in the prepared image, but the retained original
still fails that one average. Issue #5 therefore remains open. The integer
overlap correction does not resolve VI arbitration, floating-point refill
overlap, or the remaining retained-image timing case. These results do not
establish a complete accuracy pass or release readiness.

## Edge-switch restoration and cache request phase

The September 21, 2026 correction restores the minor-edge switch at the initial
whole-row origin. A middle Y coordinate before `YH & ~3` is never reached, so
the upper edge remains active. A switch inside that initial whole row can occur
before the first visible sample. Vertical clipping preserves this origin.
The earlier change and its cartridge oracle used the same incorrect immediate
selection; their passing results did not establish that behavior.

The restored condition and corrected cartridge oracle retain every triangle
case and geometry input. Literal packets and fill spans cover the missed
switch, equality at the origin, fractional top and middle positions, and
clipping after a reachable switch. Four focused checks fail on the preceding
renderer; all 27 focused triangle checks pass after restoration. Physical
captures for malformed coordinate orders remain part of broader conformance.

The same validated source includes the cache-miss SClock synchronization and
deferred instruction-refill timing described in [CPU timing](../hardware/cpu-timing.md).
Those changes preserve the pending CPU/system-clock phase through the request
and evaluate refresh after the older instruction has completed.

Windows MSVC Release, Linux Clang Release, and Clang ASan/UBSan with leak
detection each validated the same 297-file snapshot and the same prepared
cartridges. All 169 tracked cartridge-source files retained their hashes.
Only this result record was revised after those runs.

| Check | Result in each configuration |
| --- | --- |
| Hardware and host regressions | 1,073 passed |
| Validation-tool regressions | 36 passed |
| Default cartridge | 4,637 passed |
| Cold boot followed by warm reset | 4,637 passed on each boot |
| Extended Base, including all twelve triangle cases | 4,649 passed |
| Extended Timing | 1 failed of 1,604 |
| Cycle, CP0-hazards, and Poorly-understood-quirk | 13, five, and two passed |
| Source and input integrity | Verified |

Formatting, strict builds, storage checks, and capture-runner checks pass.
All three runs agree on the complete failure blocks and execution counts.
The sanitizer run reports no AddressSanitizer, UndefinedBehaviorSanitizer, or
leak diagnostics. Full validation still returns exit 8 for the remaining
extended timing failure, which stays enabled:

```text
Test 'Timing: Load from uncached (with VI enabled)' with '(true, 24, 36.3)' failed: Actual: 32 but expected: 36 (+/- 1). Median cycle count
```

The test source is pinned at `9a8b9f7d94ee2f6f57d7feed70c98c22cdc30e6c` with
the [maintained corrections](cartridge-fixtures.md). The triangle oracle's
SHA-256 is `a2542fe2ff16224c28cdfb9993f33711f0925054c8aa2cc362aaf0ef1709b691`.

| Input | Bytes | SHA-256 |
| --- | ---: | --- |
| Default cartridge | 2,609,128 | `353bb2d2132b8ca6038ecb7dc5f2b71426fd269cf93995e745ebd0be28c1f3f3` |
| Extended cartridge | 2,633,384 | `5c490faffc0329ede6ae4a877d5ac2a15a86e42a5af8bfe546e0ff52fb3ce170` |
| NTSC PIF firmware | 1,984 | `fa7b09795ef1e54461e59f6f2d902368133e3f1cd980e34383e6a780d74beffd` |

Default execution is 329,396,925 instructions and 822,176,054
CPU cycles. Extended execution is 363,664,080 instructions and
896,402,897 CPU cycles. Earlier input hashes and measurements below
remain historical records. This correction does not establish full accuracy,
playable-game compatibility, or release readiness.

## Cache clock phase and refill ordering

The September 21, 2026 runs following
`e5087dd178a79eabed5a2a486fc9a6c3f657b4a2` cover shared SClock synchronization
for instruction and data cache misses. The synchronization uses the current
CPU/RCP phase, including pending CPU cycles and the two fixed cycles before
the request reaches the synchronization point. The nominal 40- and 48-cycle
refill waits remain unchanged.

Speculative instruction refills now calculate their phase and refresh overlap
after the older instruction commits. Regressions cover both caches, explicit
instruction-cache fill, dirty replacement, all three carried phases, pending
cycles, Count visibility, two queued refills, and refresh beginning on the
optional synchronization cycle. The [CPU timing guide](../hardware/cpu-timing.md)
describes the timing rule and its limits.

Windows MSVC Release, Linux Clang Release, and Clang ASan/UBSan with leak
detection each ran the full validator against the same 297-file source snapshot
and prepared cartridge inputs. All 36 validation-tool tests, Clang formatting,
and strict builds passed. The three runs agree on these results:

| Check | Result in each configuration |
| --- | --- |
| Hardware and host regressions | 1,071 passed |
| Default cartridge | 4,637 passed |
| Cold boot followed by warm reset | 4,637 passed on each boot |
| Extended base category | 4,649 passed |
| Extended timing category | 1 failed of 1,604 |
| Cycle, CP0-hazard, and quirk categories | 20 passed |
| Concurrent storage, Unicode paths, and capture checks | Passed |
| Source and input integrity | Verified |

The remaining assertion is the VI-enabled same-bank uncached-load median:
32 against 36 plus or minus 1. All ten cache-miss assertions pass. Each full
validator returns exit 8 because `nemu64_extended` still fails. The sanitizer
run reports no ASan, UBSan, or leak diagnostics. Default execution uses
329,396,925 instructions and 822,176,054 CPU cycles; extended execution uses
363,886,662 instructions and 896,163,285 CPU cycles.

These runs use the locally prepared default and extended images recorded in
the next section: SHA-256 `353bb2d2132b8ca6038ecb7dc5f2b71426fd269cf93995e745ebd0be28c1f3f3`
and `083e8e93e4e154122ece8b6fdb5d211aa379cb3b9d4113074109f8b82d63cbc8`.
Their 169 tracked source files and the supplied 1,984-byte PIF input retain
their recorded hashes throughout validation.

A separate Windows Clang replay uses the exact extended image downloaded from
the completed hosted runs for `e5087dd`: 2,632,872 bytes, SHA-256
`e1c4bdd741b8adc8c076c0976fe24fc907ffa521a0ddd05339bcbf16ce108b30`.
The unchanged earlier core reproduces all eleven hosted timing failures and
869,864,933 CPU cycles on that image. The cache-phase changes clear its ten
cache-miss failures, leaving the same uncached-load assertion. This replay
executes 360,309,635 instructions and 870,608,565 CPU cycles.

The original uncorrected extended image, SHA-256
`441bc0b4409034c0c4cffdb658005cae9033c53c0fef69763ffbd89dcf30b089`, remains a
separate diagnostic. It retains the eleven triangle-fixture failures, the
original clock-measurement failure, the same-bank uncached-load failure, and
one VI-disabled cache average at physical `0x00300000` (43.03 against 42.5
plus or minus 0.5). The prepared-image passes therefore do not establish
identical timing on every cartridge layout. Shared RDRAM arbitration and
VI request timing remain under issues #4, #5, #6, and #39.

This result record was added after validation. No implementation, test,
fixture, or build file changed after the three runs.

## Triangle edges and cartridge fixtures

The September 21, 2026 runs following revision
`e6f2685bbf05712317cf9d7598df86349c384c3d` cover the lower-edge fix in issue #59
and the maintained [cartridge fixture corrections](cartridge-fixtures.md).
Those runs used a lower-edge selector that ignored whether `YM` could be
reached from the initial row. The literal-packet regression encoded that same
assumption and therefore did not establish the malformed-coordinate behavior.

The cartridge retains all twelve experimental triangle cases and the clock
test's tolerance of 20. The corrections repair command color packing,
framebuffer color conversion, coverage/edge calculations in the CPU oracle,
and the clock test's sampling sequence. See the [triangle guide](rdp-triangle-fixtures.md)
and [clock sampling analysis](cartridge-build-layout.md#cpurdp-clock-sampling).

Windows MSVC Release, Linux Clang Release, and Clang ASan/UBSan with leak
detection each ran `tools/ci/validate.py` against the same 294-file source
snapshot and prepared cartridge inputs. All 169 tracked cartridge-source files
retained their recorded hashes during compilation and validation. The validator
also recorded 376 generated files in the cartridge checkout. File bytes match
across platforms; the aggregate source fingerprints differ because Windows
and the Linux mount report different executable permission flags.

| Check | Windows Release | Linux Release | Linux sanitizers |
| --- | --- | --- | --- |
| Hardware and host regressions | 1,063 passed | 1,063 passed | 1,063 passed |
| Default cartridge | 4,637 passed | 4,637 passed | 4,637 passed |
| Cold boot followed by warm reset | 4,637 passed on each boot | 4,637 passed on each boot | 4,637 passed on each boot |
| Extended base category, including twelve triangle cases | 4,649 passed | 4,649 passed | 4,649 passed |
| Extended timing category | 11 failed of 1,604 | Same 11 failed | Same 11 failed |
| Cycle, CP0-hazard, and quirk categories | 20 passed | 20 passed | 20 passed |
| Concurrent storage, Unicode paths, and capture checks | Passed | Passed | Passed |
| Source and input integrity | Verified | Verified | Verified |

All 36 validation-tool tests, Clang formatting, and strict builds passed.
Six of seven CTest cases passed; `nemu64_extended` failed, and each full
validator returned exit 8. The sanitizer run reported no AddressSanitizer,
UndefinedBehaviorSanitizer, or leak diagnostics. Complete failure blocks,
group counts, and emulated execution counts match across the three runs.

The remaining timing failures are the two VI-enabled cache averages
(41.316 and 41.347 against 43.25 plus or minus 1), eight VI-disabled cache
averages (41.319 through 41.816 against 42.5 plus or minus 0.5), and the
VI-enabled same-bank uncached-load median (32 against 36 plus or minus 1).
The revised clock test passes. Default execution uses 329,397,980 instructions
and 819,200,814 CPU cycles; extended execution uses 366,093,273 instructions
and 896,048,096 CPU cycles.

The prepared source starts from cartridge revision
`9a8b9f7d94ee2f6f57d7feed70c98c22cdc30e6c`. Its four changed files and exact
before/after hashes are recorded in `tests/cartridge/fixtures/manifest.json`.
Rust `nightly-2026-07-16` and `nust64` 0.4.1 produced these inputs:

| Input | Bytes | SHA-256 |
| --- | ---: | --- |
| Prepared default cartridge | 2,609,128 | `353bb2d2132b8ca6038ecb7dc5f2b71426fd269cf93995e745ebd0be28c1f3f3` |
| Prepared extended cartridge | 2,633,384 | `083e8e93e4e154122ece8b6fdb5d211aa379cb3b9d4113074109f8b82d63cbc8` |
| NTSC PIF firmware | 1,984 | `fa7b09795ef1e54461e59f6f2d902368133e3f1cd980e34383e6a780d74beffd` |

The older unmodified cartridge results below retain their original input
hashes. Rebuilding with the corrected fixtures changes the instruction and
data layout, exposing additional cache-timing assertions. These runs therefore
establish the rendering and clock-test corrections while full validation
remains unsuccessful. This result record and Git attributes for stable fixture
line endings were added after the runs; no implementation, test, fixture, or
build file changed after validation.

## Controller Pak banking

The September 21, 2026 runs cover banked Controller Pak storage following
revision `0e251e66c10681dfa17dbb963dfb0edbf4d691ae`. Each port can use one through
62 banks of 32 KiB. Joybus writes to `0x8000` select a bank, and the runner's
`--pak-banks PORT:COUNT` option configures the capacity before loading its raw
Pak image. The default remains one bank. See [Controller Pak behavior](../hardware/controller-pak.md)
and [persistent storage](../hardware/storage.md) for packet and file rules.

Fourteen new regressions bring the hardware and host suite to 1,062 cases.
Windows MSVC Release, Linux Clang Release, and Clang ASan/UBSan with leak
detection each ran `tools/ci/validate.py` against the same frozen set of 287
files. The compiler configurations and exact cartridge/PIF hashes remain those
listed in [Inputs and commands](#inputs-and-commands). The result record was
updated after validation; the implementation, tests, and build configuration
retain their validated bytes.

| Check | Windows Release | Linux Release | Linux sanitizers |
| --- | --- | --- | --- |
| Hardware and host regressions | 1,062 passed | 1,062 passed | 1,062 passed |
| Default cartridge | 4,637 passed | 4,637 passed | 4,637 passed |
| Cold boot followed by warm reset | 4,637 passed on each boot | 4,637 passed on each boot | 4,637 passed on each boot |
| Concurrent storage, Unicode paths, and capture checks | Passed | Passed | Passed |
| Extended cartridge | 15 failed | Same 15 failed | Same 15 failed |
| Source and input integrity | Verified | Verified | Verified |

Clang formatting, strict builds, and all 23 validation-script tests passed.
Six of seven CTest cases passed; `nemu64_extended` failed and each full validator
returned exit 8. The sanitizer run reported no AddressSanitizer,
UndefinedBehaviorSanitizer, or leak diagnostics.

Every complete extended diagnostic block matches the retained baseline,
including pixel arrays and timing values. The group counts remain Base 11
failures of 4,649, Timing four of 1,604, Cycle zero of 13, CP0-hazards zero of
five, and Poorly-understood-quirk zero of two. Default execution remains
328,128,181 instructions and 741,446,373 CPU cycles; extended execution remains
359,483,384 instructions and 793,706,393 CPU cycles.

The eight core banking tests cover all four ports and capacities through 62
banks, independent data, unavailable selections, packet lengths and CRCs,
attachment changes, resizing, resets, and bank selection at SI completion.
Forcing every accepted bank-selection write to bank zero makes six of those
eight tests fail; restoring the production selection logic passes all eight.
Six host tests cover option parsing,
capacity validation, complete file round trips, and rejection of wrong-sized
images without changing storage or the input file.

A separate Windows runner check completed 36 process invocations for twelve
port/capacity combinations: all four ports with one, four, and 62 banks. Each
combination created the expected file size, preserved patterned data on reload,
and rejected a truncated image without overwriting it. The paths included
spaces and non-ASCII characters. This used synthetic cartridge and PIF inputs
to test runner storage behavior, not game compatibility.

The earlier Super Mario 64 capture below was not repeated for this storage
change. Banked Pak hardware captures and serial timing remain separate
validation work. The four extended timing failures and eleven experimental
triangle disagreements remain enabled, and full validation still fails.

## GameCube controller and VI clipping coverage

Earlier September 21, 2026 runs cover the GameCube controller protocol and
two VI clipping regressions, following revision
`52487af95f90ad7c3a86b4804f2cf4ee64b214fa`. Nine controller tests and two VI tests
bring the local suite to 1,048 cases. The [GameCube guide](../hardware/gamecube-controller.md)
describes raw input, packet modes, origins, rumble, and reset behavior. The
[video guide](../hardware/video-scanout.md) documents first-visible-line filtering;
the production VI renderer is unchanged.

Windows MSVC Release, Linux Clang Release, and Clang ASan/UBSan with leak
detection each ran `tools/ci/validate.py` against the same frozen set of 283
files. The compiler configurations and exact cartridge/PIF hashes are listed
in [Inputs and commands](#inputs-and-commands). Final edits after validation
affect only this record and the GameCube guide. The intervening parent commit
`a0f8817` clarifies the earlier Super Mario 64 capture without changing code or
tests.

| Check | Windows Release | Linux Release | Linux sanitizers |
| --- | --- | --- | --- |
| Hardware and host regressions | 1,048 passed | 1,048 passed | 1,048 passed |
| Default cartridge | 4,637 passed | 4,637 passed | 4,637 passed |
| Cold boot followed by warm reset | 4,637 passed on each boot | 4,637 passed on each boot | 4,637 passed on each boot |
| Concurrent storage, Unicode paths, and capture checks | Passed | Passed | Passed |
| Extended cartridge | 15 failed | Same 15 failed | Same 15 failed |
| Source and input integrity | Verified | Verified | Verified |

Clang formatting, strict builds, and all 23 validation-script tests passed.
Six of seven CTest cases passed; only `nemu64_extended` failed. All three full
validators returned exit 8. The sanitizer run reported no AddressSanitizer,
UndefinedBehaviorSanitizer, or leak diagnostics.

All 15 complete extended diagnostic blocks match the retained `6d8f70b`
baseline, including the rendered pixel arrays. The group counts remain Base
11 failures of 4,649, Timing four of 1,604, Cycle zero of 13, CP0-hazards zero of
five, and Poorly-understood-quirk zero of two. The timing measurements remain
41.274, 41.668, 32, and 133,299 against the expectations recorded below.
Default execution remains 328,128,181 instructions and 741,446,373 CPU cycles;
extended execution remains 359,483,384 instructions and 793,706,393 CPU cycles.

The controller regressions check literal packets on all four ports, analog
packing modes, malformed requests, response lengths, origin and motor state,
hotplug, and changes becoming visible at SI completion. The VI regressions
cover NTSC and PAL in both framebuffer sizes. They retain normal lower-neighbor
filtering on the first visible line after vertical clipping; moving the
repeated-row guard to the unclipped window start makes them fail.

The Super Mario 64 capture below belongs to the earlier `de56bb6` builds and
was not repeated for this controller change. The complete extended suite still
fails, so these results do not establish release readiness.

## LLD fault-priority coverage

The earlier September 21, 2026 runs add three encoded CPU regressions to revision
`de56bb60445937748d4d13b6b87a210da0b151ca`. The cases check a doubleword-misaligned
`LLD` against missing, invalid, and valid TLB mappings from a branch delay slot.
They verify AdEL, BadVAddr, EPC and BD, and preservation of the load destination
and linked-load state. The production core is unchanged. The
[CPU guide](../hardware/cpu-timing.md#lld-alignment-fault-priority) identifies the
processor-manual rule; the [documentation index](../README.md) maps the hardware
guides to source and test folders.

Windows MSVC Release, Linux Clang Release, and Clang ASan/UBSan with leak
detection each ran `tools/ci/validate.py` from the same isolated source copy.
The compiler configurations and exact cartridge/PIF hashes are listed in
[Inputs and commands](#inputs-and-commands). The 279-file source snapshot
contains the preceding 277 files plus the CPU test file and documentation index.
The final publication changes after these runs affect Markdown only: the
result record and line-ending normalization. No C++ source, test, or build
configuration changed after validation.

| Check | Windows Release | Linux Release | Linux sanitizers |
| --- | --- | --- | --- |
| Hardware and host regressions | 1,037 passed | 1,037 passed | 1,037 passed |
| Default cartridge | 4,637 passed | 4,637 passed | 4,637 passed |
| Cold boot followed by warm reset | 4,637 passed on each boot | 4,637 passed on each boot | 4,637 passed on each boot |
| Concurrent storage, Unicode paths, and capture checks | Passed | Passed | Passed |
| Extended cartridge | 15 failed | Same 15 failed | Same 15 failed |
| Source and input integrity | Verified | Verified | Verified |

All format checks, strict builds, and 23 validation-script tests passed.
Six of seven CTest cases passed; only `nemu64_extended` failed. All three full
validators returned exit 8. The sanitizer run reported no AddressSanitizer,
UndefinedBehaviorSanitizer, or leak diagnostics.

Every complete extended diagnostic block matches the retained `6d8f70b`
baseline, including pixel arrays and timing measurements. The group counts
remain Base 11 failures of 4,649, Timing four of 1,604, Cycle zero of 13,
CP0-hazards zero of five, and Poorly-understood-quirk zero of two. Default
execution remains 328,128,181 instructions and 741,446,373 CPU cycles; extended
execution remains 359,483,384 instructions and 793,706,393 CPU cycles. These
results add exception-priority coverage while retaining the existing accuracy
failures described below.

## FPU environment and RDP control fixes

The September 21, 2026 runs cover floating-point environment isolation and
preservation of partial RDP commands across FLUSH control writes, following
revision `4d42b940b60f3850cb3da38fcbd9da8c94af42a7`. Three FPU regressions and
one RDP regression bring the local suite to 1,034 cases. See
[floating-point execution](../hardware/floating-point.md) and
[RDP command streams](../hardware/rdp-command-stream.md) for the implemented behavior.

### Inputs and commands

The test source is `thelemmy/nemu64-test` at
`9a8b9f7d94ee2f6f57d7feed70c98c22cdc30e6c`. The extended image enables
`timing,cycle,cop0hazard,poorly_understood_quirk,experimental_rdp`. All three runs
use the exact retained hosted images, with their original assertions unchanged.

| Input | Bytes | SHA-256 |
| --- | ---: | --- |
| Default cartridge | 2,608,400 | `c4b6d452355bf89ed8409ef24ea02d3aadb7c6dfeed991f252700ca1f88114c5` |
| Extended cartridge | 2,635,352 | `441bc0b4409034c0c4cffdb658005cae9033c53c0fef69763ffbd89dcf30b089` |
| NTSC PIF firmware | 1,984 | `fa7b09795ef1e54461e59f6f2d902368133e3f1cd980e34383e6a780d74beffd` |

All configurations used `tools/ci/validate.py` with both cartridge paths, the
PIF path, strict compiler warnings, clang-format 22.1.0, and two build jobs.
Windows used MSVC 19.43.34809.0, the Visual Studio 17 2022 generator, and Release.
Linux used Clang 18.1.3 with the Unix Makefiles generator: Release for the normal
run, and RelWithDebInfo with AddressSanitizer, UndefinedBehaviorSanitizer, and
leak detection for the instrumented run. The [testing guide](../testing.md) gives
the command forms.

The runs used a frozen set of 277 files. The Linux checkout contained the same
file bytes as the Windows source. Final changes after validation affected only
this result record and the wording of the RDP command-stream guide; source and
tests were unchanged.

### Results

| Check | Windows Release | Linux Release | Linux sanitizers |
| --- | --- | --- | --- |
| Hardware and host regressions | 1,034 passed | 1,034 passed | 1,034 passed |
| Concurrent storage replacement | Passed | Passed | Passed |
| Default cartridge | 4,637 passed | 4,637 passed | 4,637 passed |
| Cold boot followed by warm reset | 4,637 passed on each boot | 4,637 passed on each boot | 4,637 passed on each boot |
| Unicode runner and storage paths | Passed | Passed | Passed |
| Compatibility capture repeatability and failure handling | Passed | Passed | Passed |
| Extended cartridge | 15 failed | Same 15 failed | Same 15 failed |
| Source and input integrity | Verified | Verified | Verified |

The format, configure, build, and 23 validation-script tests passed in each
configuration. The instrumented run reported no AddressSanitizer,
UndefinedBehaviorSanitizer, or leak diagnostics. Six of seven CTest cases passed;
only `nemu64_extended` failed. CTest and all three full validators returned exit 8.
None of the full runs is a pass.

GNU Make reported timestamp-skew warnings for generated files and newly built
libraries on the WSL mount. A subsequent build in each Linux configuration
compiled and linked nothing, and all seven executable/archive outputs retained
their SHA-256 hashes. Timestamp warnings for regenerated dependency files remained;
the compiler reported no C++ warnings.

### Regression evidence

On the preceding core, the new subnormal-comparison test fails with host
denormal-as-zero enabled, and the unmasked host division-trap test terminates
with exit 136. Both pass after environment isolation. The host-state restoration
test passes on both versions and checks behavior that the fix must preserve.
The focused FPU run passes all 34 cases after the change.

The new RDP regression fails on the preceding core when a parameter word raises
a false SyncFull interrupt after FLUSH discards the partial packet. It passes
after preserving the buffer. Its 784 combinations cover every doubleword split
of all ten multiword opcodes, both head and tail memory sources, wrapped DMEM
addresses, and FLUSH control precedence. FLUSH is cleared before submitting the
tail; these cases do not establish pipeline-drain or command-DMA timing.

### Remaining cartridge failures

All 15 complete diagnostic blocks match the retained `6d8f70b` baseline and the
preceding `4d42b94` results, including the rendered pixel arrays. The extended
summary contains 11 failures among
4,649 base cases and four failures among 1,604 timing cases. All 13 cycle cases,
five CP0-hazard cases, and two partially characterized hardware cases pass.

The VI-enabled cache-miss averages remain 41.274 and 41.668 against
43.25 +/- 1.0. The VI-enabled same-bank uncached-load case remains 32 against
36 +/- 1. The CPU/RDP clock measurement remains 133,299 against 133,333 +/- 20.
All eight VI-disabled cache-miss cases pass. The eleven experimental FilledTriangle
disagreements are described in the [fixture audit](rdp-triangle-fixtures.md).

Default execution remains 328,128,181 instructions and 741,446,373 CPU cycles.
Extended execution remains 359,483,384 instructions and 793,706,393 CPU cycles.
A different cartridge layout can change timing results; see the
[build comparison](cartridge-build-layout.md) before comparing other images.

### Bounded cartridge capture

The earlier validated Windows and Clang Release builds at `de56bb6` each completed
a 240-field capture of Super Mario 64 (USA), configured with EEPROM 4 Kbit and a
gamepad on port 1.
The supplied 8,388,608-byte cartridge has SHA-256
`17ce077343c6133f8c9f2d6d6d9a4ab62c8cd2aa57c40aea1f490b4c8bb21d91`;
the PIF is the input recorded above.

Both captures executed 364,144,878 instructions and 407,911,044 CPU cycles,
producing 120,352 stereo sample pairs, including 39,953 nonzero pairs. Metadata,
field records, the final image, and audio bytes match between compilers and the
retained `6d8f70b` capture. Input files and executables retained their hashes.
The final image shows the title logo. Gameplay, audible playback quality,
physical controls, and save/reload behavior still require separate validation.

Shared VI contention, the CPU/RDP timing discrepancy, and the triangle fixture
disagreements remain release blockers. These runs do not establish a completed
1.0 release. [Compatibility captures](compatibility-captures.md) have separate
acceptance criteria.
