# Cartridge timing after PIF boot polling

Timed PIF boot-command processing retains the same 22 extended cartridge
failures as revision `a20e59f`: eleven experimental triangle cases and eleven
memory-timing cases. No external test or expected value was changed.

Six existing VI-disabled load-miss averages changed. Each case still expects
42.5 CPU cycles with a tolerance of 0.5:

| Address | Before boot polling | With boot polling |
| --- | ---: | ---: |
| `0x80200000` | 41.728 | 41.809 |
| `0x80300000` | 41.832 | 41.645 |
| `0x80400000` | 41.712 | 41.663 |
| `0x80500000` | 41.529 | 41.616 |
| `0x80600000` | 41.649 | 41.654 |
| `0x80700000` | 41.590 | 41.550 |

A diagnostic build changed only the PIF polling interval to one RCP cycle,
while retaining the new command stages, checksum enforcement, and timeout.
Its 22 failure reports, including every measured value, matched `a20e59f`.
This isolates the six changes to elapsed boot-poll time. The diagnostic interval
was not retained. It does not establish which physical timing model is correct.

The production interval remains the coarse model described in
[PIF boot control](../hardware/pif-boot.md). The outstanding cache/refill and
memory-arbitration work remains under issues #4, #10, and #12. These measurements
are evidence of changed clock phase, not a resolution of those failures.

The pinned extended cartridge is built from test commit
`9a8b9f7d94ee2f6f57d7feed70c98c22cdc30e6c`; its SHA-256 is
`9a85cf5b8ea89af4170abb0a14fb9b415232b3904e1257f99036485665478c5c`.
Release and sanitizer validation use `tools/ci/validate.py` with the default and
extended cartridges plus the supplied NTSC PIF ROM.
