# VI line-latching results

The VI timing change preserves the duration selected at the start of a line.
Previously, a register write or video-type change could replace that duration
while the line was in progress. RI refresh now follows the latched boundary.

All 692 local regressions and all 4,637 default cartridge tests pass. The
extended cartridge image still reports 22 failures: the same 11 timing cases
and 11 [experimental triangle fixtures](rdp-triangle-fixtures.md). No passing
test became a failure, and no failing test was disabled or given a different
expectation. Extended CTest continues to return failure.

Ten timing averages changed relative to revision `954318e`. The uncached
same-bank median remains unchanged. Values below are CPU cycles; the first
ten rows are averages and the final row is a median.

| Case | Before | After | Expected |
| --- | ---: | ---: | ---: |
| Cache miss, VI enabled, same bank | 41.299 | 41.298 | 43.25 ± 1.0 |
| Cache miss, VI enabled, other bank | 41.555 | 41.557 | 43.25 ± 1.0 |
| Cache miss, VI disabled, `0x80000000` | 41.619 | 41.689 | 42.5 ± 0.5 |
| Cache miss, VI disabled, `0x80100000` | 41.828 | 41.680 | 42.5 ± 0.5 |
| Cache miss, VI disabled, `0x80200000` | 41.752 | 41.728 | 42.5 ± 0.5 |
| Cache miss, VI disabled, `0x80300000` | 41.880 | 41.832 | 42.5 ± 0.5 |
| Cache miss, VI disabled, `0x80400000` | 41.688 | 41.712 | 42.5 ± 0.5 |
| Cache miss, VI disabled, `0x80500000` | 41.545 | 41.529 | 42.5 ± 0.5 |
| Cache miss, VI disabled, `0x80600000` | 41.439 | 41.649 | 42.5 ± 0.5 |
| Cache miss, VI disabled, `0x80700000` | 41.552 | 41.590 | 42.5 ± 0.5 |
| Uncached load, VI enabled, same bank | 32 | 32 | 36 ± 1 |

Two temporary comparison builds separated the timing changes. Replacing the
latched period with the old per-tick register lookup restored every previous
value. Restoring immediate blank-counter clearing while retaining the latched
period produced the new values. Both builds used the same core library and
unmodified extended cartridge image. This isolates the measurement shift to
line-duration latching; it does not resolve the memory-response timing gap.

Windows/Linux Clang release and sanitizer runs reproduce the new values.
Linux GCC and Windows MSVC also pass the local regressions and default suite.
The test images, firmware, and optional groups remain those recorded in the
[accuracy baseline](accuracy-baseline.md). Logs are retained locally as
`.work/local-ci-vi-timing-*.log`; comparison outputs are
`.work/vi-timing-unlatched.log` and `.work/vi-timing-immediate-blank.log`.

The new regressions cover writes during normal and leap lines, short blank
pulses, odd-field preservation, nine-bit counter wrap, interrupt acknowledgment,
reset, and refresh deadlines. Ten-field traces exercise NTSC/PAL progressive
and interlaced counts. A separate trace changes VI registers while refresh,
SP DMA, and audio DMA are active, comparing bulk and single-cycle advances.

VI framebuffer traffic still does not participate in memory arbitration.
Timed output delivery, scanout register latches, and line-buffer retention also
remain unfinished. Issues #5, #6, #23, and #24 remain open. These results update
the measured state; they are not new acceptance thresholds.
