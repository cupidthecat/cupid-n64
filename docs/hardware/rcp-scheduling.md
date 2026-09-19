# RCP event scheduling

When RI refresh is enabled, horizontal video boundaries and refresh recovery
completion also bound scheduler advances. This keeps refresh waits independent
of the size of a CPU clock update. See [RDRAM interface state](rdram-interface.md)
for the current CPU and DMA timing limits.

`System::advance` converts CPU cycles to RCP cycles with a persistent fractional
clock. An active RSP executes at one-cycle boundaries. When it is halted, the
scheduler can advance farther, but stops at the next SP DMA row, buffered CPU
store, or peripheral event.

Peripheral deadlines include PI and SI completion, EEPROM busy expiry, and audio
samples. `Bus::tick` also honors these deadlines when called directly. Device
clocks advance before event callbacks, so an audio callback observes its sample's
DP clock rather than the beginning or end of a larger CPU batch.

This ordering preserves memory visibility between devices. An SP DMA that
finishes before an audio sample supplies that sample's data. A later SI write
cannot change an earlier audio sample. The RSP's direct tick path likewise
interleaves DMA progress with instructions instead of completing a whole DMA
batch before executing those instructions.

`tests/rcp/test_synchronization.cpp` compares large and one-cycle advances,
checks SP/SI/audio ordering, and samples the DP clock at audio and RSP events.

The scheduler orders events using each device's existing transfer model. It does
not yet arbitrate shared RDRAM bandwidth or model every DMA bus beat. SP transfers
remain row-based, and PI data transfer still occurs separately from its busy
completion delay. These limits are distinct from the batching errors covered by
the scheduling regressions.
