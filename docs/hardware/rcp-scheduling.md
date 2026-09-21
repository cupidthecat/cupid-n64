# RCP event scheduling

When RI refresh is enabled, horizontal video boundaries and refresh recovery
completion also bound scheduler advances. This keeps refresh waits independent
of the size of a CPU clock update. See [RDRAM interface state](rdram-interface.md)
for the current CPU and DMA timing limits.

`System::advance` converts CPU cycles to RCP cycles with a persistent fractional
clock. An active RSP processes an issue group, dependency stall or branch bubble
at each one-cycle boundary. An eligible scalar/vector pair shares that boundary; see
[signal processor instruction timing](rsp-pipeline.md). When it is halted, the
scheduler can advance farther, but stops at the next SP DMA row, buffered CPU
store, or peripheral event.

The inverse conversion rounds an RCP wait up to the first reachable CPU clock
while retaining that fractional phase. A zero wait costs no cycles at any phase.
The arithmetic avoids overflowing intermediate products; waits beyond the
64-bit CPU clock horizon saturate at that horizon. Current CPU callers use
short refresh and buffered-store waits. The larger values are arithmetic
boundary coverage, not a claim of running a cartridge for that many cycles.
`tests/rcp/test_clock_conversion.cpp` checks all three phases, minimal rounding,
zero waits, large representable values, and saturation.

Peripheral deadlines include PI progress and completion, SI completion, EEPROM
busy expiry, and audio samples. `Bus::tick` also honors these deadlines when
called directly. Audio and video output are queued while devices advance, then
delivered after the boundary finishes. In `System::advance`, that includes SP
execution/DMA and buffered CPU stores. Direct `Bus::tick` calls deliver after the
bus devices finish.

EEPROM busy time advances before SI completes at a shared boundary. A newly
delivered write therefore retains its full interval, while a previous write
finishing at that clock is already ready for the next command. The EEPROM
regressions cover both cases through SI read completion; configuration by CPU
store or write DMA does not execute the command. See
[EEPROM behavior](eeprom.md).

Callbacks observe the boundary's DP clock and completed device state. A transfer
started by a callback retains its full delay; it cannot consume cycles from
before the write. Video callbacks can observe PI and SP completion at the same
clock. Video snapshots and audio samples retain the data captured during their
device stage, even if a later device stage changes memory before delivery.

A zero-duration VI leap resumes after the preceding field callback, without
advancing any other device clock again. This preserves the callback's CURRENT
value and lets its register writes affect the following line. Reset discards
queued output. Callbacks must not recursively advance the system.

Audio deadlines use the period latched for the active DAC interval. If video
callbacks are registered, idle DAC boundaries also bound the advance so a callback
cannot start a buffer retroactively. Without an observable sample or video
callback, idle audio periods can advance in bulk. See [audio timing](audio-timing.md).

This ordering preserves memory visibility between devices. An SP DMA that
finishes before an audio sample supplies that sample's data. A later SI write
cannot change an earlier audio sample. The RSP's direct tick path likewise
interleaves DMA progress with instructions instead of completing a whole DMA
batch before executing those instructions.

Blocking CPU RDRAM transfers consult the existing VI-line deadline before their
response completes. A transfer that crosses an enabled refresh boundary includes
that recovery interval, while the scheduler still processes the boundary and
device clocks in normal event order. The overlap check does not add a separate
scheduler event.

`tests/rcp/test_synchronization.cpp` compares large and one-cycle advances,
checks SP/SI/audio ordering, and samples the DP clock at audio and RSP events.
`test_output_scheduling.cpp` checks callback-started PI DMA, PI/SI I/O, SI
payload visibility, and SP DMA from both video and audio output. It also checks
coincident DAC starts, buffered CPU divider-write ordering, completion visibility
at the callback boundary, and reset with output pending. Traces cover direct bus
and CPU-driven advances, bulk and single-cycle calls, and PI deadlines in both
video regions.

The scheduler orders events using each device's existing transfer model. It does
not yet arbitrate shared RDRAM bandwidth or model every DMA halfword edge. SP
transfers remain row-based. PI transfers stop at cartridge page and internal
buffer progress deadlines so memory, cartridge side effects, address registers,
and completion state cannot move ahead of the sampled clock. These limits are
distinct from the blocking CPU refresh timing and batching rules covered by the
scheduling regressions.
