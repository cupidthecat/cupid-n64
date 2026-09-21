# Recorded validation results

## LLD fault-priority coverage

The latest September 21, 2026 runs add three encoded CPU regressions to revision
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

The validated Windows and Clang Release builds each completed a 240-field capture
of Super Mario 64 (USA), configured with EEPROM 4 Kbit and a gamepad on port 1.
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
