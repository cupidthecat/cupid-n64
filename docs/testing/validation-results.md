# Recorded validation results

The September 21, 2026 runs cover the RSP halt/resume correction following
revision `6d8f70b78452a92c05d2942a3d0ee140fb66699e`. A pending taken-branch
bubble now spends an elapsed RCP cycle while halted, preserving the stall for
immediate resume. Four encoded regressions bring the local suite to 1,030 cases.
See [RSP instruction timing](../hardware/rsp-pipeline.md) for the cycle model.

## Inputs and commands

The test source is `thelemmy/nemu64-test` at
`9a8b9f7d94ee2f6f57d7feed70c98c22cdc30e6c`. The extended image enables
`timing,cycle,cop0hazard,poorly_understood_quirk,experimental_rdp`. Both runs
use the exact retained hosted images, with their original assertions unchanged.

| Input | Bytes | SHA-256 |
| --- | ---: | --- |
| Default cartridge | 2,608,400 | `c4b6d452355bf89ed8409ef24ea02d3aadb7c6dfeed991f252700ca1f88114c5` |
| Extended cartridge | 2,635,352 | `441bc0b4409034c0c4cffdb658005cae9033c53c0fef69763ffbd89dcf30b089` |
| NTSC PIF firmware | 1,984 | `fa7b09795ef1e54461e59f6f2d902368133e3f1cd980e34383e6a780d74beffd` |

Both configurations used `tools/ci/validate.py` with both cartridge paths, the
PIF path, strict compiler warnings, clang-format 22.1.0, and two build jobs.
The compiler was Clang 18.1.3 with the Unix Makefiles generator: Release for the
normal run, and RelWithDebInfo with AddressSanitizer, UndefinedBehaviorSanitizer,
and leak detection for the instrumented run. The
[testing guide](../testing.md) gives the command forms.

## Results

| Check | Linux Release | Linux sanitizers |
| --- | --- | --- |
| Hardware and host regressions | 1,030 passed | 1,030 passed |
| Concurrent storage replacement | Passed | Passed |
| Default cartridge | 4,637 passed | 4,637 passed |
| Cold boot followed by warm reset | 4,637 passed on each boot | 4,637 passed on each boot |
| Unicode runner and storage paths | Passed | Passed |
| Compatibility capture repeatability and failure handling | Passed | Passed |
| Extended cartridge | 15 failed | Same 15 failed |
| Source and input integrity | Verified | Verified |

The format, configure, build, and validation-script tests passed. The instrumented
run reported no AddressSanitizer, UndefinedBehaviorSanitizer, or leak diagnostics.
CTest and the full validators returned exit 8 because the extended cartridge
failed; neither full run is a pass.

Windows MSVC Release separately passed the targeted RSP group, 47 of 47 cases,
including all four new regressions, with clang-format checked. The local Windows
full-ROM invocation did not start, so this record assigns it no result.

The final BREAK, MTC0 HALT, and single-step regressions fail at their resumed
target PC on the preceding core. The MTC0 case reaches that mismatch after
checking DMA source bytes, completion, addresses, and all eight copied bytes.
The reserved SPECIAL regression passes on the preceding core and retains the
existing decoder behavior.

## Remaining cartridge failures

All 15 complete diagnostic blocks match the preceding `6d8f70b` run, including
the rendered pixel arrays. The extended summary contains 11 failures among
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

Shared VI contention, the CPU/RDP timing discrepancy, and the triangle fixture
disagreements remain release blockers. These runs do not establish a completed
1.0 release. [Compatibility captures](compatibility-captures.md) have separate
acceptance criteria.
