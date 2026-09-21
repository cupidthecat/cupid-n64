# Cartridge build layout and timing

Test-source revision and enabled features are necessary provenance, but they
do not identify a cartridge binary. Builds from the pinned revision
`9a8b9f7d94ee2f6f57d7feed70c98c22cdc30e6c` produce different instruction and
data layouts on Windows and Linux. Embedded Rust toolchain and dependency
paths also change the image. Compare input hashes before treating two runs
as a host-portability comparison.

## Observed extended images

These images enable the default group plus `timing,cycle,cop0hazard,poorly_understood_quirk,experimental_rdp`.
The local replay used emulator revision `edd925f`; the hosted run below used
the preceding revision `cb8221f`.

| Build | Bytes | SHA-256 | Failed base / timing cases |
| --- | ---: | --- | --- |
| Recorded Windows build | 2,635,312 | `9a85cf5b8ea89af4170abb0a14fb9b415232b3904e1257f99036485665478c5c` | 11 / 3 |
| CI run 35539925294 | 2,635,352 | `441bc0b4409034c0c4cffdb658005cae9033c53c0fef69763ffbd89dcf30b089` | 11 / 7 |
| Local Linux build | 2,635,864 | `235f835c04a1a1a14691018e0f61723488381de0b2e12bc93e721086be63f312` | 11 / 5 |
| Local Linux build with CI toolchain path prefixes | 2,635,352 | `ec02db86e612cce6d190ef6b19895668fe684139338e0d533bf5eca375c67e4e` | 11 / 5 |

All four images retain the original assertions. The eleven base failures are
the [experimental triangle cases](rdp-triangle-fixtures.md). Cycle, CP0-hazard,
and partially characterized hardware categories pass.

At those revisions, the hosted image fails VI-disabled cache-miss averages at `0x80000000`,
`0x80600000`, and `0x80700000`, in addition to the two VI-enabled averages,
the uncached read sharing VI's bank, and the CPU/RDP clock test. Both local
Linux images fail the `0x80000000` case and CPU/RDP clock test, alongside the
three timing failures in the recorded Windows image. Matching the image size
after remapping paths did not reproduce the CI hash or all its measurements.

## SP DMA clock correction

Revision `6d8f70b` corrects SP DMA row countdowns to use RCP cycles. Replaying
the same hosted extended image, SHA-256
`441bc0b4409034c0c4cffdb658005cae9033c53c0fef69763ffbd89dcf30b089`,
now passes all eight VI-disabled cache-miss cases. Windows MSVC, Linux Clang,
and sanitizer runs agree with the completed push and PR workflows
`35582647081` and `35582651851`.

The two VI-enabled averages remain outside their unchanged tolerance:

| Parameter | Before the DMA correction | After the DMA correction | Expected |
| --- | ---: | ---: | --- |
| `true` | 41.272 | 41.274 | 43.25 +/- 1.0 |
| `false` | 41.552 | 41.668 | 43.25 +/- 1.0 |

The uncached same-bank VI measurement remains 32, and the CPU/RDP clock
measurement remains 133,299. The extended summary has 11 base failures and
four timing failures; the other enabled groups pass. The original assertions
and cartridge bytes are unchanged. Correcting DMA timing changes when the
cartridge reaches later measurements, but does not implement VI memory
requests or shared RDRAM arbitration.

## CPU/RDP clock sampling

The local Linux image reproduces the CPU/RDP test's reported difference of
133,300 clocks against 133,333 with tolerance 20. Its initial MFC0 Count is
at `0x8012bb34`, and its initial DPC_CLOCK load is at `0x8012bb44`. The trace
contains a 48-CPU-cycle instruction-cache refill across `0x8012bb3c` to
`0x8012bb40`, between those samples. Count includes that interval; the later
DPC_CLOCK starting sample excludes it. The hot polling loop has no matching
refill between its final pair of samples.

In the recorded Windows image, the corresponding instructions are at
`0x8012bb0c` and `0x8012bb1c`, within one cache line. In a separate diagnostic
run of the local Linux image, preloading only the `0x8012bb40` instruction-cache
line before the initial Count sample changes the measured difference to
133,336. This isolates a cache-state contribution to the failure. It does not
establish the complete hardware pipeline timing or turn the unmodified
cartridge run into a pass.

The maintained [fixture correction](cartridge-fixtures.md) places MFC0 Count
and the DPC_CLOCK load in one assembly block, aligned to a 32-byte instruction
cache line. Both endpoints use the same helper. The expected DPC difference
is calculated from the actual wrapping Count difference, including any
polling overshoot; the tolerance remains 20.

The diagnostic cartridge with SHA-256
`f138e19bf660e962d2cc225012f6e246910f056af001c64c2a1de6711867a889`
contains the pair at `0x80134bc0` and `0x80134bc4`. Its clock test passes with
the unchanged emulator core. A diagnostic core that doubles DPC_CLOCK
increments fails the corrected test with 266,680 clocks against 133,340
plus or minus 20, over 100,005 Count ticks.

These original-image failure records remain useful when reproducing earlier
results. Replay the exact hosted input from `accuracy-test-inputs`, with the
supplied firmware, when investigating a report tied to that image. Results
from the prepared fixtures carry their own source manifest and ROM hashes.
