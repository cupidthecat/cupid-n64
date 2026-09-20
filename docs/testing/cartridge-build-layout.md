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

The hosted image fails VI-disabled cache-miss averages at `0x80000000`,
`0x80600000`, and `0x80700000`, in addition to the two VI-enabled averages,
the uncached read sharing VI's bank, and the CPU/RDP clock test. Both local
Linux images fail the `0x80000000` case and CPU/RDP clock test, alongside the
three timing failures in the recorded Windows image. Matching the image size
after remapping paths did not reproduce the CI hash or all its measurements.

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

The original failures remain enabled and unresolved. Do not compensate by
changing the CPU/RCP clock ratio, excluding failed groups, or treating a
different ROM's result as validation of the failing image. Replay the exact
hosted input from `accuracy-test-inputs`, with the supplied firmware, when
investigating the remaining timing differences.
