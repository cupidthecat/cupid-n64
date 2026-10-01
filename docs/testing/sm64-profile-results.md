# Super Mario 64 profile validation (2026-09-29)

The profile-use build of revision `6843133` passes the complete prepared hardware
suites and preserves the retained Super Mario 64 captures. It remains below
full speed. The title run improves, but the controller replay does not show a
speed improvement over its earlier ordinary build capture.

## Training and validation

Clang 21.1.5 collected counters from a 7,200-field NTSC courtyard replay with
the supplied USA cartridge, PIF, EEPROM-4Kbit configuration, and scripted
controller input. Every field, execution observation, audio timestamp, and the
complete PCM stream match the retained ordinary capture. Instrumented execution
time is excluded from performance comparisons.

The [profile build procedure](profile-guided-builds.md) produced a checked
package with these SHA-256 hashes:

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `core.profdata` | 345,384 | `db352f80b95f85c4ce2a34e761f11dbb26a09ff63b93c0a96584c2a849b5a90b` |
| `core.json` | 26,132 | `66197da8ca3bf74cc661a974fe53bebf047ffafb60dee4d321b9572f07f97ea7` |

Windows Clang Release desktop validation then passed all 1,534 regressions,
4,637 prepared default cartridge cases, 6,273 prepared extended cases, 67 tooling
tests, clang-format 22.1.0, and all 15 CTest groups. Stepped/batched comparisons
and default cold/warm boots passed. The validator confirmed unchanged source,
firmware, cartridge, SDL, and profile inputs. The ordinary compiler and sanitizer
results remain recorded in [CPU store validation](cpu-store-results.md).

The report is retained at
`.work/reports/sm64-profile-use/2aa97cfbb0d04649b5224f627debf574/report.json`.
Prepared-image fixture corrections remain documented in
[cartridge fixtures](cartridge-fixtures.md); original-image acceptance remains
open in #5 and #39.

## Execution measurements

Measurements use the Windows 11 Intel Core i7-13700H host, performance-core
affinity, disabled process power throttling, Clang Release, and interprocedural
optimization. Builds and other emulator benchmarks did not run during these
captures. The measured controller sequence differs from the training replay.

| Capture | Fields | Ordinary build | Profile-use build | Profile-use fields per host second |
| --- | ---: | ---: | ---: | ---: |
| Title | 1,200 | 36.163 s | 30.163 s | 39.8 |
| Controller replay | 3,000 | 96.305 s | 99.018 s | 30.3 |

The title builds ran in the same measurement batch. The controller
comparison uses an earlier retained ordinary capture, so its elapsed times do
not establish a controlled speed difference. Repeated ordinary title captures
also vary substantially with host conditions.

Both profile-use captures preserve every video field, complete PCM stream,
execution observation, and controller replay EEPROM byte. Retained comparisons
are `.work/pgo-use-title-comparison.json` and
`.work/pgo-use-gameplay-comparison.json`. The training comparison is
`.work/pgo-training-courtyard-comparison.json`.

These results do not meet sustained full-speed gameplay or normal audible
playback acceptance. Those remain open in #48 and #47. This record was added
after the validated source snapshot; the implementation and test inputs are
unchanged.
