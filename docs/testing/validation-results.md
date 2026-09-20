# Recorded validation results

The September 20, 2026 run tested hardware revision
`ee9205bcbc25da68e2be881ceeea338c12fae2a0`. This revision includes exclusive
temporary-file reservation for persistent storage, bus register decoding,
RSP COP0 aliases, RDP color-key alpha, raw triangle fixtures, and the refresh
phase correction for CPU memory requests.

## Inputs and commands

The test source is `thelemmy/nemu64-test` at
`9a8b9f7d94ee2f6f57d7feed70c98c22cdc30e6c`. The extended image enables
`timing,cycle,cop0hazard,poorly_understood_quirk,experimental_rdp`. The images
and assertions were unchanged during these runs.

| Input | Bytes | SHA-256 |
| --- | ---: | --- |
| Default cartridge | 2,608,400 | `e9d93dc1f854af7c60b9d576e15615766ce9e693a9ce794140c3736603732769` |
| Extended cartridge | 2,635,312 | `9a85cf5b8ea89af4170abb0a14fb9b415232b3904e1257f99036485665478c5c` |
| NTSC PIF firmware | 1,984 | `fa7b09795ef1e54461e59f6f2d902368133e3f1cd980e34383e6a780d74beffd` |

Each platform used the shared `tools/ci/validate.py` entry point with both
cartridge paths, the PIF path, the pinned test checkout, strict compiler
warnings, and clang-format 22.1.0. The Linux builds used Clang 18.1.3 in Release
and RelWithDebInfo with AddressSanitizer and UndefinedBehaviorSanitizer. The
Windows Release build used MSVC 19.43 and the Visual Studio 2022 generator.
The [testing guide](../testing.md) gives the full command forms.

## Results

| Check | Linux Release | Linux sanitizers | Windows MSVC |
| --- | --- | --- | --- |
| Local hardware and host regressions | 994 passed | 994 passed | 994 passed |
| Concurrent storage replacement | Passed | Passed | Passed |
| Default cartridge | 4,637 passed | 4,637 passed | 4,637 passed |
| Cold boot followed by warm reset | 4,637 passed on each boot | 4,637 passed on each boot | 4,637 passed on each boot |
| Unicode runner and storage paths | Passed | Passed | Passed |
| Extended cartridge | 14 failed | Same 14 failed | Same 14 failed |

The strict builds produced no compiler warnings. The sanitizer run produced no
AddressSanitizer or UndefinedBehaviorSanitizer diagnostics. CTest returned
failure on every platform because the extended cartridge failed.

The extended summary contains 11 failures among 4,649 base cases and three
failures among 1,604 timing cases. All 13 cycle cases, five CP0-hazard cases,
and two partially characterized hardware cases pass. The three timing failures
are the VI-enabled cache-miss averages with both parameter values and the
uncached load that contends with VI in the same bank. The eleven rendering
failures are the experimental FilledTriangle cases documented in the
[fixture audit](rdp-triangle-fixtures.md).

The eight VI-disabled cache-miss cases that failed in the earlier run now pass.
Shared VI contention and the experimental triangle disagreements remain release
blockers. These results do not establish desktop gameplay, host audio quality,
physical gamepad compatibility, or a completed 1.0 release. Those scenarios
require their own recorded acceptance runs.
